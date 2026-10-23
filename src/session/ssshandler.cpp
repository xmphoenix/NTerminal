#include "ssshandler.h"
#include <QDebug>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

namespace {

// 远端资源快照脚本（参考 MobaXterm 的做法）：
// 远端用一个常驻循环每秒打印一段带 @@ 标记的快照。
// 只用 POSIX 工具（/proc、hostname、who、ps、df、cut），不需要 pty，
// 因此输出原样进入 exec 通道，不会回显到用户终端。
const char *kStatsCommand =
    "export LC_ALL=C\n"
    "while :; do\n"
    "  echo '@@BEGIN'\n"
    "  echo '@@HOST'; hostname\n"
    "  echo '@@CPU'; head -n 1 /proc/stat\n"
    "  echo '@@MEM'; grep -E '^(MemTotal|MemAvailable|MemFree|SwapTotal|SwapFree):' /proc/meminfo\n"
    "  echo '@@NET'; cat /proc/net/dev\n"
    "  echo '@@PROC'; ps -e --no-headers 2>/dev/null | wc -l\n"
    "  echo '@@USERS'; who 2>/dev/null\n"
    "  echo '@@UPTIME'; cut -d' ' -f1 /proc/uptime\n"
    "  echo '@@LOAD'; cat /proc/loadavg\n"
    "  echo '@@DISK'; df -Pk / 2>/dev/null | tail -n 1\n"
    "  echo '@@END'\n"
    "  sleep 1\n"
    "done\n";

quint64 kvToBytes(const QString &value)
{
    // /proc/meminfo 的数值单位是 kB
    const QStringList parts = value.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    return parts.isEmpty() ? 0 : parts.first().toULongLong() * 1024ULL;
}

} // namespace

SshHandler::SshHandler(const Common::ConnectionSettings &settings, QObject *parent)
    : ConnectionHandler(parent)
    , m_session(nullptr)
    , m_channel(nullptr)
    , m_statsChannel(nullptr)
    , m_settings(settings)
    , m_notifier(nullptr)
    , m_connected(false)
    , m_columns(200)
    , m_rows(24)
    , m_statsDrainTimer(nullptr)
    , m_prevCpuTotal(0)
    , m_prevCpuIdle(0)
    , m_prevNetRx(0)
    , m_prevNetTx(0)
    , m_prevStatsMs(0)
{
    m_statsDrainTimer = new QTimer(this);
    m_statsDrainTimer->setInterval(200);
    connect(m_statsDrainTimer, &QTimer::timeout, this, &SshHandler::readStatsData);
}

SshHandler::~SshHandler()
{
    close();
}

bool SshHandler::open()
{
    m_session = ssh_new();
    if (!m_session) {
        emit errorOccurred("Failed to create SSH session");
        return false;
    }
    
    ssh_options_set(m_session, SSH_OPTIONS_HOST, m_settings.host.toUtf8().constData());
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &m_settings.port);
    ssh_options_set(m_session, SSH_OPTIONS_USER, m_settings.username.toUtf8().constData());
    
    int timeout = 30;
    ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT, &timeout);
    
    qDebug() << "[SshHandler] Connecting to" << m_settings.host << ":" << m_settings.port;
    
    int rc = ssh_connect(m_session);
    if (rc != SSH_OK) {
        QString error = QString("SSH connection failed: %1").arg(ssh_get_error(m_session));
        emit errorOccurred(error);
        cleanup();
        return false;
    }
    
    if (!authenticate()) {
        cleanup();
        return false;
    }
    
    if (!setupChannel()) {
        cleanup();
        return false;
    }
    
    int socketFd = ssh_get_fd(m_session);
    if (socketFd >= 0) {
        m_notifier = new QSocketNotifier(socketFd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &SshHandler::onSocketActivated);
    }
    
    m_connected = true;
    m_statsClock.start();
    setupStatsChannel();
    qDebug() << "[SshHandler] Connected";
    emit connected();
    return true;
}

void SshHandler::close()
{
    m_connected = false;
    closeStatsChannel();
    
    if (m_notifier) {
        delete m_notifier;
        m_notifier = nullptr;
    }
    
    cleanup();
    emit disconnected();
}

bool SshHandler::isOpen() const
{
    return m_session != nullptr && m_channel != nullptr;
}

bool SshHandler::isConnected() const
{
    return m_connected && m_channel != nullptr;
}

void SshHandler::sendData(const QByteArray &data)
{
    if (!m_channel || !m_connected) {
        return;
    }
    
    ssh_channel_write(m_channel, data.constData(), data.size());
}

QString SshHandler::connectionInfo() const
{
    return QString("SSH: %1@%2:%3").arg(m_settings.username, m_settings.host).arg(m_settings.port);
}

void SshHandler::onSocketActivated(int socket)
{
    Q_UNUSED(socket);
    checkData();
}

bool SshHandler::authenticate()
{
    int rc = ssh_userauth_password(m_session, nullptr, m_settings.password.toUtf8().constData());
    
    if (rc == SSH_AUTH_SUCCESS) {
        qDebug() << "[SshHandler] Password authentication successful";
        return true;
    }
    
    rc = ssh_userauth_publickey_auto(m_session, nullptr, nullptr);
    if (rc == SSH_AUTH_SUCCESS) {
        qDebug() << "[SshHandler] Public key authentication successful";
        return true;
    }
    
    QString error = QString("Authentication failed: %1").arg(ssh_get_error(m_session));
    emit errorOccurred(error);
    return false;
}

bool SshHandler::setupChannel()
{
    m_channel = ssh_channel_new(m_session);
    if (!m_channel) {
        emit errorOccurred("Failed to create SSH channel");
        return false;
    }
    
    int rc = ssh_channel_open_session(m_channel);
    if (rc != SSH_OK) {
        QString error = QString("Failed to open SSH channel: %1").arg(ssh_get_error(m_session));
        emit errorOccurred(error);
        return false;
    }
    
    rc = ssh_channel_request_pty_size(m_channel, "xterm-256color", m_columns, m_rows);
    if (rc != SSH_OK) {
        QString error = QString("Failed to request PTY: %1").arg(ssh_get_error(m_session));
        emit errorOccurred(error);
        return false;
    }
    
    rc = ssh_channel_request_shell(m_channel);
    if (rc != SSH_OK) {
        QString error = QString("Failed to request shell: %1").arg(ssh_get_error(m_session));
        emit errorOccurred(error);
        return false;
    }
    
    qDebug() << "[SshHandler] Channel setup complete";
    return true;
}

void SshHandler::checkData()
{
    if (!m_channel || !m_connected) {
        return;
    }
    
    char buffer[4096];
    int nbytes = ssh_channel_read_nonblocking(m_channel, buffer, sizeof(buffer), 0);
    
    if (nbytes > 0) {
        emit dataReceived(QByteArray(buffer, nbytes));
    }
    
    if (ssh_channel_is_eof(m_channel)) {
        qDebug() << "[SshHandler] Channel EOF";
        close();
    }
}

void SshHandler::cleanup()
{
    closeStatsChannel();

    if (m_channel) {
        if (ssh_channel_is_open(m_channel)) {
            ssh_channel_close(m_channel);
        }
        ssh_channel_free(m_channel);
        m_channel = nullptr;
    }
    
    if (m_session) {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
    }
}

void SshHandler::setTerminalSize(int columns, int rows)
{
    m_columns = columns;
    m_rows = rows;

    if (m_channel && ssh_channel_is_open(m_channel)) {
        ssh_channel_change_pty_size(m_channel, columns, rows);
        qDebug() << "[SshHandler] Terminal size changed to:" << columns << "x" << rows;
    }
}

// ============================================================================
// 远端系统资源采集
// ============================================================================

void SshHandler::setupStatsChannel()
{
    if (!m_session || !m_connected || m_statsChannel) {
        return;
    }

    m_statsChannel = ssh_channel_new(m_session);
    if (!m_statsChannel) {
        return;
    }

    if (ssh_channel_open_session(m_statsChannel) != SSH_OK) {
        ssh_channel_free(m_statsChannel);
        m_statsChannel = nullptr;
        qDebug() << "[SshHandler] Stats channel unavailable:" << ssh_get_error(m_session);
        return;
    }

    if (ssh_channel_request_exec(m_statsChannel, kStatsCommand) != SSH_OK) {
        ssh_channel_close(m_statsChannel);
        ssh_channel_free(m_statsChannel);
        m_statsChannel = nullptr;
        qDebug() << "[SshHandler] Stats command rejected:" << ssh_get_error(m_session);
        return;
    }

    m_statsBuffer.clear();
    m_statsSections.clear();
    m_statsSection.clear();
    m_prevCpuTotal = 0;
    m_prevCpuIdle = 0;
    m_prevNetRx = 0;
    m_prevNetTx = 0;
    m_prevStatsMs = 0;
    m_statsDrainTimer->start();
}

void SshHandler::closeStatsChannel()
{
    if (m_statsDrainTimer) {
        m_statsDrainTimer->stop();
    }
    if (m_statsChannel) {
        if (ssh_channel_is_open(m_statsChannel)) {
            ssh_channel_close(m_statsChannel);
        }
        ssh_channel_free(m_statsChannel);
        m_statsChannel = nullptr;
    }
    m_statsBuffer.clear();
    m_statsSections.clear();
    m_statsSection.clear();
}

void SshHandler::readStatsData()
{
    if (!m_statsChannel) {
        return;
    }

    // 只做非阻塞读取，绝不能卡住主终端通道
    char buffer[8192];
    for (;;) {
        const int n = ssh_channel_read_nonblocking(m_statsChannel, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;
        }
        m_statsBuffer.append(buffer, n);
        if (n < static_cast<int>(sizeof(buffer))) {
            break;
        }
    }

    int newline = -1;
    while ((newline = m_statsBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_statsBuffer.left(newline);
        m_statsBuffer.remove(0, newline + 1);
        handleStatsLine(QString::fromUtf8(line).trimmed());
    }

    // 远端输出不是预期格式时别让缓冲无限增长
    if (m_statsBuffer.size() > 64 * 1024) {
        m_statsBuffer.clear();
    }

    if (ssh_channel_is_eof(m_statsChannel)) {
        closeStatsChannel();
    }
}

void SshHandler::handleStatsLine(const QString &line)
{
    if (line.isEmpty()) {
        return;
    }
    if (line == QLatin1String("@@BEGIN")) {
        m_statsSections.clear();
        m_statsSection.clear();
        return;
    }
    if (line == QLatin1String("@@END")) {
        publishStats();
        m_statsSections.clear();
        m_statsSection.clear();
        return;
    }
    if (line.startsWith(QLatin1String("@@"))) {
        m_statsSection = line.mid(2);
        return;
    }
    if (!m_statsSection.isEmpty()) {
        m_statsSections[m_statsSection].append(line);
    }
}

void SshHandler::publishStats()
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));

    // ---- CPU：两次 /proc/stat 采样求差 ----
    quint64 cpuTotal = 0;
    quint64 cpuIdle = 0;
    const QStringList cpuLines = m_statsSections.value(QStringLiteral("CPU"));
    if (!cpuLines.isEmpty()) {
        const QStringList fields = cpuLines.first().split(whitespace, Qt::SkipEmptyParts);
        // fields[0] == "cpu"，后面依次是 user nice system idle iowait ...
        for (int i = 1; i < fields.size(); ++i) {
            cpuTotal += fields.at(i).toULongLong();
        }
        if (fields.size() > 4) {
            cpuIdle += fields.at(4).toULongLong();   // idle
        }
        if (fields.size() > 5) {
            cpuIdle += fields.at(5).toULongLong();   // iowait 也视为空闲
        }
    }

    int cpuPercent = -1;
    if (m_prevCpuTotal > 0 && cpuTotal > m_prevCpuTotal) {
        const double totalDelta = double(cpuTotal - m_prevCpuTotal);
        const double idleDelta = cpuIdle > m_prevCpuIdle
                                     ? double(cpuIdle - m_prevCpuIdle) : 0.0;
        cpuPercent = qBound(0, qRound(100.0 * (1.0 - idleDelta / totalDelta)), 100);
    }
    m_prevCpuTotal = cpuTotal;
    m_prevCpuIdle = cpuIdle;

    // ---- 内存 ----
    qint64 memTotal = 0;
    qint64 memAvailable = 0;
    qint64 memFree = 0;
    qint64 swapTotal = 0;
    qint64 swapFree = 0;
    for (const QString &line : m_statsSections.value(QStringLiteral("MEM"))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            continue;
        }
        const QString key = line.left(colon).trimmed();
        const qint64 bytes = static_cast<qint64>(kvToBytes(line.mid(colon + 1)));
        if (key == QLatin1String("MemTotal")) {
            memTotal = bytes;
        } else if (key == QLatin1String("MemAvailable")) {
            memAvailable = bytes;
        } else if (key == QLatin1String("MemFree")) {
            memFree = bytes;
        } else if (key == QLatin1String("SwapTotal")) {
            swapTotal = bytes;
        } else if (key == QLatin1String("SwapFree")) {
            swapFree = bytes;
        }
    }
    if (memAvailable <= 0) {
        memAvailable = memFree;
    }
    const qint64 memUsed = memTotal > memAvailable ? memTotal - memAvailable : 0;
    const qint64 swapUsed = swapTotal > swapFree ? swapTotal - swapFree : 0;

    // ---- 网络：各网卡收发字节累计，按两次采样的时间差换算速率 ----
    quint64 netRx = 0;
    quint64 netTx = 0;
    for (const QString &line : m_statsSections.value(QStringLiteral("NET"))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            continue;
        }
        if (line.left(colon).trimmed() == QLatin1String("lo")) {
            continue;
        }
        const QStringList fields = line.mid(colon + 1).split(whitespace, Qt::SkipEmptyParts);
        if (fields.size() >= 9) {
            netRx += fields.at(0).toULongLong();
            netTx += fields.at(8).toULongLong();
        }
    }

    const qint64 nowMs = m_statsClock.isValid() ? m_statsClock.elapsed() : 0;
    double rxRate = 0.0;
    double txRate = 0.0;
    if (m_prevStatsMs > 0 && nowMs > m_prevStatsMs) {
        const double seconds = (nowMs - m_prevStatsMs) / 1000.0;
        if (netRx >= m_prevNetRx) {
            rxRate = double(netRx - m_prevNetRx) / seconds;
        }
        if (netTx >= m_prevNetTx) {
            txRate = double(netTx - m_prevNetTx) / seconds;
        }
    }
    m_prevNetRx = netRx;
    m_prevNetTx = netTx;
    m_prevStatsMs = nowMs;

    // ---- 其他 ----
    QJsonObject stats;
    const QStringList hostLines = m_statsSections.value(QStringLiteral("HOST"));
    stats[QStringLiteral("host")] = hostLines.isEmpty()
        ? m_settings.host : hostLines.first().trimmed();
    stats[QStringLiteral("cpu")] = cpuPercent;

    stats[QStringLiteral("memTotal")] = static_cast<double>(memTotal);
    stats[QStringLiteral("memUsed")] = static_cast<double>(memUsed);
    stats[QStringLiteral("swapTotal")] = static_cast<double>(swapTotal);
    stats[QStringLiteral("swapUsed")] = static_cast<double>(swapUsed);

    stats[QStringLiteral("netRx")] = rxRate;
    stats[QStringLiteral("netTx")] = txRate;

    const QStringList procLines = m_statsSections.value(QStringLiteral("PROC"));
    stats[QStringLiteral("procs")] = procLines.isEmpty() ? 0 : procLines.first().toInt();

    // who 的每行是一个登录会话；用户数按会话数统计，同时带上用户名列表
    // （参考 MobaXterm 的 "ybzhang (x2)" 展示）
    const QStringList userLines = m_statsSections.value(QStringLiteral("USERS"));
    QStringList userNames;
    for (const QString &line : userLines) {
        const QString name = line.split(whitespace, Qt::SkipEmptyParts).value(0);
        if (!name.isEmpty() && !userNames.contains(name)) {
            userNames.append(name);
        }
    }
    stats[QStringLiteral("users")] = userLines.size();
    stats[QStringLiteral("userNames")] = userNames.join(QStringLiteral(", "));

    const QStringList uptimeLines = m_statsSections.value(QStringLiteral("UPTIME"));
    stats[QStringLiteral("uptime")] = uptimeLines.isEmpty()
        ? 0.0 : uptimeLines.first().toDouble();

    const QStringList loadLines = m_statsSections.value(QStringLiteral("LOAD"));
    if (!loadLines.isEmpty()) {
        const QStringList load = loadLines.first().split(whitespace, Qt::SkipEmptyParts);
        stats[QStringLiteral("load1")] = load.size() > 0 ? load.at(0).toDouble() : 0.0;
        stats[QStringLiteral("load5")] = load.size() > 1 ? load.at(1).toDouble() : 0.0;
        stats[QStringLiteral("load15")] = load.size() > 2 ? load.at(2).toDouble() : 0.0;
    }

    // df -P：Filesystem 1024-blocks Used Available Capacity Mounted-on
    // 文件系统名可能带空格，从行尾往前取字段更稳
    const QStringList diskLines = m_statsSections.value(QStringLiteral("DISK"));
    if (!diskLines.isEmpty()) {
        const QStringList fields = diskLines.last().split(whitespace, Qt::SkipEmptyParts);
        if (fields.size() >= 6) {
            QString percentText = fields.at(fields.size() - 2);
            percentText.remove(QLatin1Char('%'));
            stats[QStringLiteral("diskMount")] = fields.last();
            stats[QStringLiteral("diskPercent")] = percentText.toInt();
            stats[QStringLiteral("diskAvail")] =
                double(fields.at(fields.size() - 3).toLongLong() * 1024);
            stats[QStringLiteral("diskUsed")] =
                double(fields.at(fields.size() - 4).toLongLong() * 1024);
            stats[QStringLiteral("diskTotal")] =
                double(fields.at(fields.size() - 5).toLongLong() * 1024);
        }
    }

    // 完全没有可用数据（例如远端不是 Linux）时不打扰状态栏
    if (memTotal <= 0 && cpuPercent < 0) {
        return;
    }

    emit systemStatsUpdated(stats);
}
