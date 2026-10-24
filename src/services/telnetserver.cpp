#include "services/telnetserver.h"

#include <QDir>
#include <QHostAddress>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <functional>
#include <utility>

namespace {

constexpr char kIac = char(0xFF);
constexpr char kWill = char(0xFB);
constexpr char kDo = char(0xFD);
constexpr char kSb = char(0xFA);
constexpr char kSe = char(0xF0);
constexpr char kEchoOption = char(0x01);
constexpr char kSgaOption = char(0x03);

// 把命令输出的 LF 统一成 CRLF，避免 telnet 客户端里换行错位
QByteArray toTelnetNewlines(const QByteArray &data)
{
    QByteArray out;
    out.reserve(data.size() + data.size() / 8);
    char previous = 0;
    for (const char byte : data) {
        if (byte == '\n' && previous != '\r') {
            out.append('\r');
        }
        out.append(byte);
        previous = byte;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// 单个 Telnet 会话
// ---------------------------------------------------------------------------
class TelnetSession : public QObject
{
    Q_OBJECT

public:
    TelnetSession(QTcpSocket *socket, std::function<void(const QString &)> logger,
                  QObject *parent)
        : QObject(parent)
        , m_socket(socket)
        , m_log(std::move(logger))
    {
        m_socket->setParent(this);
        connect(m_socket, &QTcpSocket::readyRead, this, &TelnetSession::onReadyRead);
        connect(m_socket, &QTcpSocket::disconnected, this, [this] {
            if (m_process) {
                m_process->kill();
            }
            m_log(tr("Telnet 会话断开：%1").arg(peerText()));
            deleteLater();
        });

        m_cwd = QDir::currentPath();
        m_log(tr("Telnet 会话建立：%1").arg(peerText()));

        // 服务端负责回显，SGA 用于关闭客户端的分段等待
        QByteArray greeting;
        greeting.append(kIac).append(kWill).append(kEchoOption);
        greeting.append(kIac).append(kWill).append(kSgaOption);
        greeting.append("\r\nNTerminal Telnet 服务\r\n");
        greeting.append("命令以本机当前用户权限执行，输入 exit 退出。\r\n\r\n");
        send(greeting);
        showPrompt();
    }

private:
    QString peerText() const
    {
        return QStringLiteral("%1:%2").arg(m_socket->peerAddress().toString())
            .arg(m_socket->peerPort());
    }

    void send(const QByteArray &data)
    {
        if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->write(data);
        }
    }

    void sendText(const QString &text)
    {
        send(text.toUtf8());
    }

    void showPrompt()
    {
        sendText(QStringLiteral("%1> ").arg(QDir::toNativeSeparators(m_cwd)));
    }

    // ---- Telnet 协议字节流：剥离 IAC 协商，只把用户数据交给命令行 ----
    void onReadyRead()
    {
        const QByteArray data = m_socket->readAll();
        QByteArray userData;
        for (int i = 0; i < data.size(); ++i) {
            const char byte = data.at(i);
            if (byte != kIac) {
                userData.append(byte);
                continue;
            }
            if (i + 1 >= data.size()) {
                break;   // 命令字节被拆到下一次读取，忽略
            }
            const char command = data.at(++i);
            if (command == kIac) {
                userData.append(char(0xFF));
            } else if (command == kWill || command == char(0xFC) || command == kDo
                       || command == char(0xFE)) {
                ++i;   // 跳过选项字节
            } else if (command == kSb) {
                // 子协商（NAWS 等）：跳到 IAC SE
                while (i + 1 < data.size()) {
                    if (char(data.at(i)) == kIac && char(data.at(i + 1)) == kSe) {
                        ++i;
                        break;
                    }
                    ++i;
                }
            }
        }
        handleUserData(userData);
    }

    void handleUserData(const QByteArray &data)
    {
        for (const char byte : data) {
            const unsigned char ch = static_cast<unsigned char>(byte);
            if (m_escapeState > 0) {
                // 方向键/功能键的 ESC 序列，直接丢弃
                if (ch >= 0x40 && ch <= 0x7E) {
                    m_escapeState = 0;
                }
                continue;
            }
            if (ch == 0x1B) {
                m_escapeState = 1;
                continue;
            }
            if (ch == '\r' || ch == '\n') {
                if (ch == '\n' && m_lastWasCr) {
                    m_lastWasCr = false;
                    continue;
                }
                m_lastWasCr = ch == '\r';
                send("\r\n");
                const QString line = QString::fromUtf8(m_line);
                m_line.clear();
                execute(line);
                continue;
            }
            m_lastWasCr = false;
            if (ch == 0x7F || ch == 0x08) {
                if (!m_line.isEmpty()) {
                    // 按 UTF-8 字符删除：连同后续续字节一起去掉
                    m_line.chop(1);
                    while (!m_line.isEmpty()
                           && (static_cast<unsigned char>(m_line.back()) & 0xC0) == 0x80) {
                        m_line.chop(1);
                    }
                    send("\b \b");
                }
            } else if (ch == 0x03) {   // Ctrl+C
                m_line.clear();
                if (m_process) {
                    m_process->kill();
                }
                send("^C\r\n");
                if (!m_process && m_pending.isEmpty()) {
                    showPrompt();
                }
            } else if (ch == 0x15) {   // Ctrl+U 清行
                m_line.clear();
                send("\r\x1B[K");
                showPrompt();
            } else if (ch >= 0x20) {
                m_line.append(char(ch));
                send(QByteArray(1, char(ch)));
            }
        }
    }

    void execute(const QString &line)
    {
        const QString command = line.trimmed();
        if (command.isEmpty()) {
            showPrompt();
            return;
        }
        if (command.compare(QLatin1String("exit"), Qt::CaseInsensitive) == 0
            || command.compare(QLatin1String("quit"), Qt::CaseInsensitive) == 0) {
            sendText(QStringLiteral("再见\r\n"));
            m_socket->disconnectFromHost();
            return;
        }
        if (m_process) {
            // 上一条命令仍在执行：排队
            m_pending.append(command);
            return;
        }
        if (command.compare(QLatin1String("cd"), Qt::CaseInsensitive) == 0
            || command.startsWith(QLatin1String("cd "), Qt::CaseInsensitive)
            || command.startsWith(QLatin1String("chdir "), Qt::CaseInsensitive)) {
            QString target = command.mid(command.indexOf(QLatin1Char(' ')) + 1).trimmed();
            if (command.compare(QLatin1String("cd"), Qt::CaseInsensitive) == 0) {
                target = QDir::homePath();
            }
            if (!target.isEmpty()) {
                // 兼容 cmd 的 cd /d D:\path 写法
                if (target.startsWith(QLatin1String("/d "), Qt::CaseInsensitive)
                    || target.compare(QLatin1String("/d"), Qt::CaseInsensitive) == 0) {
                    target = target.mid(2).trimmed();
                }
                if (target.startsWith(QLatin1Char('"')) && target.endsWith(QLatin1Char('"'))) {
                    target = target.mid(1, target.size() - 2);
                }
                if (target.isEmpty()) {
                    target = QDir::rootPath();
                }
                const QString path = QDir(m_cwd).absoluteFilePath(target);
                if (QDir(path).exists()) {
                    m_cwd = QDir::cleanPath(path);
                } else {
                    sendText(QStringLiteral("目录不存在\r\n"));
                }
            }
            showPrompt();
            return;
        }
        if (command.compare(QLatin1String("cls"), Qt::CaseInsensitive) == 0) {
            send("\x1B[2J\x1B[H");
            showPrompt();
            return;
        }

        m_log(tr("Telnet 执行命令（%1）：%2").arg(peerText(), command));

        m_process = new QProcess(this);
        m_process->setWorkingDirectory(m_cwd);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
            if (m_process) {
                send(toTelnetNewlines(m_process->readAllStandardOutput()));
            }
        });
        connect(m_process, &QProcess::finished, this,
                [this](int exitCode, QProcess::ExitStatus status) {
                    if (m_process) {
                        send(toTelnetNewlines(m_process->readAllStandardOutput()));
                        if (status == QProcess::CrashExit || exitCode != 0) {
                            sendText(QStringLiteral("[退出码 %1]\r\n").arg(exitCode));
                        }
                        m_process->deleteLater();
                        m_process = nullptr;
                    }
                    showPrompt();
                    if (!m_pending.isEmpty() && !m_process) {
                        const QString next = m_pending.takeFirst();
                        execute(next);
                    }
                });
        connect(m_process, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError error) {
                    // FailedToStart 不会触发 finished，需要在这里收尾
                    if (error == QProcess::FailedToStart && m_process) {
                        sendText(QStringLiteral("无法启动 cmd.exe\r\n"));
                        m_process->deleteLater();
                        m_process = nullptr;
                        showPrompt();
                        if (!m_pending.isEmpty()) {
                            execute(m_pending.takeFirst());
                        }
                    }
                });
        m_process->start(QStringLiteral("cmd.exe"),
                         {QStringLiteral("/Q"), QStringLiteral("/D"), QStringLiteral("/C"),
                          QStringLiteral("chcp 65001>nul & ") + command});
    }

    QTcpSocket *m_socket = nullptr;
    QProcess *m_process = nullptr;
    QString m_cwd;
    QByteArray m_line;
    QStringList m_pending;
    bool m_lastWasCr = false;
    int m_escapeState = 0;
    std::function<void(const QString &)> m_log;
};

// ---------------------------------------------------------------------------
// TelnetServer
// ---------------------------------------------------------------------------
TelnetServer::TelnetServer(QObject *parent)
    : ServerService(parent)
{
}

QString TelnetServer::displayName() const
{
    return tr("Telnet 服务器");
}

QString TelnetServer::description() const
{
    return tr("为 telnet 客户端提供命令 shell（以当前 Windows 用户权限执行）");
}

QJsonObject TelnetServer::defaultConfig() const
{
    QJsonObject config;
    config.insert(QStringLiteral("port"), defaultPort());
    return config;
}

bool TelnetServer::startServer()
{
    const int listenPort = port();
    if (listenPort < 1 || listenPort > 65535) {
        setError(tr("端口无效"));
        return false;
    }

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &TelnetServer::onNewConnection);
    if (!m_server->listen(QHostAddress::Any, quint16(listenPort))) {
        setError(tr("无法监听端口 %1：%2").arg(listenPort).arg(m_server->errorString()));
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }

    log(tr("Telnet 服务已启动：端口 %1").arg(listenPort));
    return true;
}

void TelnetServer::stopServer()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    for (TelnetSession *session : std::as_const(m_sessions)) {
        session->deleteLater();
    }
    m_sessions.clear();
}

void TelnetServer::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        auto *session = new TelnetSession(
            socket, [this](const QString &line) { log(line); }, this);
        m_sessions.append(session);
        connect(session, &QObject::destroyed, this, [this, session] {
            m_sessions.removeAll(session);
        });
    }
}

#include "telnetserver.moc"
