#include "ui/lanscandialog.h"
#include "ui/iconprovider.h"

#include <QAbstractSocket>
#include <QCloseEvent>
#include <QComboBox>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QHostInfo>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QUdpSocket>
#include <QVBoxLayout>
#include <QXmlStreamReader>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {

constexpr int kMaxConcurrentPings = 32;
constexpr int kMaxScanHosts = 1024;

QString interfaceDisplayName(const QNetworkInterface &iface)
{
    const QString human = iface.humanReadableName();
    return human.isEmpty() ? iface.name() : human;
}

bool looksVirtual(const QString &name)
{
    static const char *const keywords[] = {
        "vEthernet", "WSL", "Hyper-V", "VMware", "VirtualBox", "TAP-",
        "tun", "singbox", "tailscale", "zerotier", "vpn", "docker", "br-"
    };
    for (const char *keyword : keywords) {
        if (name.contains(QLatin1String(keyword), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

// 物理网卡、常见私网段和可用的主机范围优先；虚拟机网卡与点对点隧道靠后。
int interfaceScore(const QNetworkInterface &iface, const QHostAddress &address, int prefix)
{
    int score = 0;
    switch (iface.type()) {
    case QNetworkInterface::Ethernet:
    case QNetworkInterface::Wifi:
        score += 1000;
        break;
    case QNetworkInterface::Virtual:
        score -= 300;
        break;
    default:
        break;
    }

    if (address.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16)) {
        score += 500;
    } else if (address.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8)) {
        score += 400;
    } else if (address.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12)) {
        score += 300;
    }

    if (prefix >= 24 && prefix <= 30) {
        score += 200;
    } else if (prefix >= 20 && prefix < 24) {
        score += 100;
    } else if (prefix >= 31) {
        score -= 400;
    }

    if (looksVirtual(interfaceDisplayName(iface))) {
        score -= 600;
    }
    return score;
}

// 按数值而不是字符串对 IP 列排序（192.168.1.2 < 192.168.1.10）。
class IpAddressItem : public QTableWidgetItem
{
public:
    explicit IpAddressItem(const QString &text)
        : QTableWidgetItem(text)
    {
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        const quint32 left = QHostAddress(text()).toIPv4Address();
        const quint32 right = QHostAddress(other.text()).toIPv4Address();
        return left < right;
    }
};

} // namespace

LanScanDialog::LanScanDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    refreshInterfaces();
}

void LanScanDialog::setupUi()
{
    setWindowTitle(tr("局域网设备扫描"));
    resize(780, 560);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(10);

    QLabel *titleLabel = new QLabel(tr("局域网设备扫描"), this);
    titleLabel->setProperty("class", "sectionTitle");
    QLabel *hintLabel = new QLabel(
        tr("扫描当前局域网内存活的网络设备，显示 IP 地址、设备名称和 MAC 地址。"
           "基于 Ping 与 ARP，设备名称通过 SSDP/UPnP、NetBIOS 和反向 DNS 解析，"
           "部分设备可能无法显示名称。仅用于你拥有权限的网络。"),
        this);
    hintLabel->setProperty("class", "hint");
    hintLabel->setWordWrap(true);
    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(hintLabel);

    QGroupBox *rangeGroup = new QGroupBox(tr("扫描范围"), this);
    QGridLayout *rangeLayout = new QGridLayout(rangeGroup);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    rangeLayout->setHorizontalSpacing(10);
    rangeLayout->setVerticalSpacing(8);
    rangeLayout->setColumnStretch(1, 1);

    rangeLayout->addWidget(new QLabel(tr("网络接口:"), this), 0, 0);
    m_interfaceCombo = new QComboBox(this);
    rangeLayout->addWidget(m_interfaceCombo, 0, 1);
    m_refreshButton = new QPushButton(IconProvider::themedIcon("reset"), tr("刷新"), this);
    m_refreshButton->setProperty("class", "tonal");
    connect(m_refreshButton, &QPushButton::clicked,
            this, &LanScanDialog::refreshInterfaces);
    rangeLayout->addWidget(m_refreshButton, 0, 2);
    mainLayout->addWidget(rangeGroup);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    m_startButton = new QPushButton(IconProvider::themedIcon("play"), tr("开始扫描"), this);
    m_startButton->setProperty("class", "primary");
    connect(m_startButton, &QPushButton::clicked, this, &LanScanDialog::startScan);
    buttonLayout->addWidget(m_startButton);

    m_stopButton = new QPushButton(IconProvider::themedIcon("stop"), tr("停止"), this);
    m_stopButton->setProperty("class", "ghost");
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &LanScanDialog::stopScan);
    buttonLayout->addWidget(m_stopButton);

    buttonLayout->addStretch();
    QPushButton *closeButton = new QPushButton(tr("关闭"), this);
    closeButton->setProperty("class", "ghost");
    connect(closeButton, &QPushButton::clicked, this, &LanScanDialog::close);
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_progressBar->setFormat(QStringLiteral("%v / %m"));
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setProperty("class", "hint");
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels(
        {tr("IP 地址"), tr("设备名称"), tr("MAC 地址"), tr("状态")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    mainLayout->addWidget(m_table, 1);

    connect(m_interfaceCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { updateReadyState(); });
}

void LanScanDialog::refreshInterfaces()
{
    const QString previous = m_interfaceCombo->currentText();

    m_interfaceCombo->blockSignals(true);
    m_interfaceCombo->clear();
    m_targets.clear();
    m_localMacs.clear();

    QList<QPair<int, NetworkTarget>> candidates;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || (flags & QNetworkInterface::IsLoopBack)
                || (flags & QNetworkInterface::IsPointToPoint)) {
            continue;
        }
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol
                    || address.isLoopback() || address.isLinkLocal()) {
                continue;
            }

            int prefix = entry.prefixLength();
            if (prefix < 0 || prefix > 32) {
                prefix = 24;
            }

            const QString localMac = normalizeMac(iface.hardwareAddress());
            if (!localMac.isEmpty()) {
                m_localMacs.insert(address.toString(), localMac);
            }

            NetworkTarget target;
            target.address = address;
            target.prefixLength = prefix;
            target.label = QStringLiteral("%1  —  %2/%3")
                               .arg(interfaceDisplayName(iface), address.toString())
                               .arg(prefix);
            candidates.append({interfaceScore(iface, address, prefix), target});
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const QPair<int, NetworkTarget> &left,
                        const QPair<int, NetworkTarget> &right) {
        return left.first > right.first;
    });
    for (const QPair<int, NetworkTarget> &candidate : candidates) {
        m_targets.append(candidate.second);
        m_interfaceCombo->addItem(candidate.second.label);
    }

    if (!previous.isEmpty()) {
        const int index = m_interfaceCombo->findText(previous);
        if (index >= 0) {
            m_interfaceCombo->setCurrentIndex(index);
        }
    }
    m_interfaceCombo->blockSignals(false);

    if (m_targets.isEmpty()) {
        m_statusLabel->setText(tr("未找到可扫描的 IPv4 网络接口。"));
        m_startButton->setEnabled(false);
        m_progressBar->setRange(0, 1);
        m_progressBar->setValue(0);
        return;
    }

    updateReadyState();
}

void LanScanDialog::updateReadyState()
{
    if (m_scanning) {
        return;
    }

    const int index = m_interfaceCombo->currentIndex();
    if (index < 0 || index >= m_targets.size()) {
        m_statusLabel->setText(tr("未找到可扫描的 IPv4 网络接口。"));
        m_startButton->setEnabled(false);
        return;
    }

    bool truncated = false;
    const QStringList hosts = buildAddressList(m_targets.at(index), &truncated);
    if (hosts.isEmpty()) {
        m_statusLabel->setText(tr("没有可扫描的地址。"));
        m_startButton->setEnabled(false);
        return;
    }

    if (truncated) {
        m_statusLabel->setText(tr("就绪：网络较大，仅扫描本机附近的 %1 个地址。")
                                   .arg(hosts.size()));
    } else {
        m_statusLabel->setText(tr("就绪：将扫描 %1 个地址。").arg(hosts.size()));
    }
    m_startButton->setEnabled(true);
    m_progressBar->setRange(0, hosts.size());
    m_progressBar->setValue(0);
}

QStringList LanScanDialog::buildAddressList(const NetworkTarget &target, bool *truncated) const
{
    if (truncated) {
        *truncated = false;
    }

    const quint32 address = target.address.toIPv4Address();
    const int prefix = qBound(0, target.prefixLength, 32);
    const quint32 mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    const quint32 network = address & mask;
    const quint32 broadcast = network | ~mask;

    quint32 first = network;
    quint32 last = broadcast;
    if (prefix < 31) {
        ++first;
        --last;
    }
    if (first > last) {
        return {};
    }

    const quint64 available = quint64(last) - quint64(first) + 1;
    quint64 count = available;
    if (count > quint64(kMaxScanHosts)) {
        count = kMaxScanHosts;
        if (truncated) {
            *truncated = true;
        }
        const quint64 local = address;
        quint64 start = local > quint64(first) + count / 2 ? local - count / 2
                                                           : quint64(first);
        if (start + count - 1 > quint64(last)) {
            start = quint64(last) - count + 1;
        }
        first = quint32(start);
    }

    QStringList result;
    result.reserve(int(count));
    for (quint64 i = 0; i < count; ++i) {
        result.append(QHostAddress(quint32(quint64(first) + i)).toString());
    }
    return result;
}

void LanScanDialog::startScan()
{
    if (m_scanning) {
        return;
    }

    const int index = m_interfaceCombo->currentIndex();
    if (index < 0 || index >= m_targets.size()) {
        return;
    }

    bool truncated = false;
    const QStringList hosts = buildAddressList(m_targets.at(index), &truncated);
    if (hosts.isEmpty()) {
        QMessageBox::information(this, tr("局域网设备扫描"), tr("没有可扫描的地址。"));
        return;
    }

    m_scanSubnet = QStringLiteral("%1/%2")
                       .arg(m_targets.at(index).address.toString())
                       .arg(m_targets.at(index).prefixLength);
    m_scanTargets.clear();
    for (const QString &ip : hosts) {
        m_scanTargets.insert(ip);
    }
    m_pendingPings = hosts;
    m_totalPings = hosts.size();
    m_completedPings = 0;
    m_alive.clear();
    m_pingAlive.clear();
    m_arpEntries.clear();
    m_namePriority.clear();
    m_ssdpQueried.clear();
    m_pingStartFailed = false;
    m_arpSucceeded = false;

    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    m_progressBar->setRange(0, m_totalPings);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("正在扫描 %1…").arg(m_scanSubnet));

    m_scanning = true;
    setScanningUi(true);
    startNextPings();
}

void LanScanDialog::stopScan()
{
    if (!m_scanning) {
        return;
    }
    m_scanning = false;

    const QList<QProcess *> processes = m_activePings.keys();
    m_activePings.clear();
    for (QProcess *process : processes) {
        process->disconnect(this);
        process->kill();
        process->deleteLater();
    }
    m_pendingPings.clear();

    if (m_arpProcess) {
        m_arpProcess->disconnect(this);
        m_arpProcess->kill();
        m_arpProcess->deleteLater();
        m_arpProcess = nullptr;
    }

    setScanningUi(false);
    m_table->setSortingEnabled(true);
    m_table->sortItems(0, Qt::AscendingOrder);

    if (m_alive.isEmpty()) {
        m_statusLabel->setText(tr("扫描已停止。"));
    } else {
        m_statusLabel->setText(tr("扫描已停止：发现 %1 台设备。").arg(m_alive.size()));
    }
    beginNameResolution();
}

void LanScanDialog::setScanningUi(bool scanning)
{
    m_startButton->setEnabled(!scanning && !m_targets.isEmpty());
    m_stopButton->setEnabled(scanning);
    m_interfaceCombo->setEnabled(!scanning);
    m_refreshButton->setEnabled(!scanning);
}

void LanScanDialog::startNextPings()
{
    while (m_scanning && m_activePings.size() < kMaxConcurrentPings
           && !m_pendingPings.isEmpty()) {
        const QString ip = m_pendingPings.takeFirst();
        QProcess *process = new QProcess(this);
        suppressProcessWindow(process);
        process->setStandardOutputFile(QProcess::nullDevice());
        process->setStandardErrorFile(QProcess::nullDevice());

        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, process, ip](int exitCode, QProcess::ExitStatus status) {
            if (m_activePings.remove(process) == 0) {
                return;
            }
            process->deleteLater();
            onPingFinished(ip, status == QProcess::NormalExit && exitCode == 0);
        });
        connect(process, &QProcess::errorOccurred, this,
                [this, process, ip](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) {
                return;
            }
            if (m_activePings.remove(process) == 0) {
                return;
            }
            process->deleteLater();
            m_pingStartFailed = true;
            onPingFinished(ip, false);
        });

        m_activePings.insert(process, ip);
        process->start(QStringLiteral("ping"), pingArguments(ip));
    }
}

QStringList LanScanDialog::pingArguments(const QString &ip)
{
#ifdef Q_OS_WIN
    return {QStringLiteral("-n"), QStringLiteral("1"),
            QStringLiteral("-w"), QStringLiteral("400"), ip};
#elif defined(Q_OS_MACOS)
    return {QStringLiteral("-c"), QStringLiteral("1"),
            QStringLiteral("-W"), QStringLiteral("500"), ip};
#else
    return {QStringLiteral("-c"), QStringLiteral("1"),
            QStringLiteral("-W"), QStringLiteral("1"), ip};
#endif
}

void LanScanDialog::suppressProcessWindow(QProcess *process)
{
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#else
    Q_UNUSED(process);
#endif
}

void LanScanDialog::onPingFinished(const QString &ip, bool alive)
{
    if (!m_scanning) {
        return;
    }

    ++m_completedPings;
    m_progressBar->setValue(m_completedPings);

    if (alive) {
        m_pingAlive.insert(ip);
    }

    startNextPings();
    if (m_pendingPings.isEmpty() && m_activePings.isEmpty()) {
        readArpTable();
    }
}

void LanScanDialog::readArpTable()
{
    if (!m_scanning) {
        return;
    }
    m_statusLabel->setText(tr("正在读取 ARP 表…"));

#ifdef Q_OS_LINUX
    QList<QPair<QString, QString>> entries;
    QFile file(QStringLiteral("/proc/net/arp"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_arpSucceeded = true;
        const QList<QByteArray> lines = file.readAll().split('\n');
        for (const QByteArray &raw : lines) {
            const QString line = QString::fromLatin1(raw).simplified();
            if (line.isEmpty() || line.startsWith(QLatin1String("IP address"))) {
                continue;
            }
            const QStringList fields = line.split(QLatin1Char(' '));
            if (fields.size() < 4 || fields.at(2) == QLatin1String("0x0")) {
                continue;
            }
            entries.append({fields.at(0), normalizeMac(fields.at(3))});
        }
    }
    applyArpEntries(entries);
    finishLiveness();
#else
    if (m_arpProcess) {
        return;
    }

    QProcess *arp = new QProcess(this);
    m_arpProcess = arp;
    suppressProcessWindow(arp);
    connect(arp, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, arp](int exitCode, QProcess::ExitStatus status) {
        const QString output = QString::fromLocal8Bit(arp->readAllStandardOutput());
        if (m_arpProcess == arp) {
            m_arpProcess = nullptr;
        }
        arp->deleteLater();
        m_arpSucceeded = status == QProcess::NormalExit && exitCode == 0;
        parseArpOutput(output);
        finishLiveness();
    });
    connect(arp, &QProcess::errorOccurred, this,
            [this, arp](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        if (m_arpProcess == arp) {
            m_arpProcess = nullptr;
        }
        arp->deleteLater();
        finishLiveness();
    });
#ifdef Q_OS_WIN
    arp->start(QStringLiteral("arp"), {QStringLiteral("-a")});
#else
    arp->start(QStringLiteral("arp"), {QStringLiteral("-an")});
#endif
#endif
}

void LanScanDialog::parseArpOutput(const QString &output)
{
    QList<QPair<QString, QString>> entries;
#if defined(Q_OS_WIN)
    static const QRegularExpression re(QStringLiteral(
        R"((\d{1,3}(?:\.\d{1,3}){3})\s+([0-9A-Fa-f]{2}(?:-[0-9A-Fa-f]{2}){5}))"));
#elif defined(Q_OS_MACOS)
    static const QRegularExpression re(QStringLiteral(
        R"(\((\d{1,3}(?:\.\d{1,3}){3})\)\s+at\s+([0-9A-Fa-f]{1,2}(?::[0-9A-Fa-f]{1,2}){5}))"));
#else
    Q_UNUSED(output);
    return;
#endif

    QRegularExpressionMatchIterator it = re.globalMatch(output);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        entries.append({match.captured(1), normalizeMac(match.captured(2))});
    }
    applyArpEntries(entries);
}

void LanScanDialog::applyArpEntries(const QList<QPair<QString, QString>> &entries)
{
    for (const QPair<QString, QString> &entry : entries) {
        if (!m_scanTargets.contains(entry.first) || entry.second.isEmpty()) {
            continue;
        }
        m_arpEntries.insert(entry.first, entry.second);
    }
}

void LanScanDialog::addOrUpdateDevice(const QString &ip, const QString &mac)
{
    int row = rowForIp(ip);
    if (row < 0) {
        row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new IpAddressItem(ip));
        m_table->setItem(row, 1, new QTableWidgetItem(tr("解析中…")));
        m_table->setItem(row, 2, new QTableWidgetItem(QStringLiteral("—")));
        m_table->setItem(row, 3, new QTableWidgetItem(tr("在线")));
    }

    m_alive.insert(ip);
    const QString displayMac = mac.isEmpty() ? m_localMacs.value(ip) : mac;
    if (!displayMac.isEmpty()) {
        if (QTableWidgetItem *item = m_table->item(row, 2)) {
            item->setText(displayMac);
        }
    }
}

void LanScanDialog::updateDeviceName(const QString &ip, const QString &name, int priority)
{
    if (name.isEmpty()) {
        return;
    }
    if (m_namePriority.value(ip, 0) > priority) {
        return;
    }

    m_namePriority.insert(ip, priority);
    const int row = rowForIp(ip);
    if (row < 0) {
        return;
    }
    if (QTableWidgetItem *item = m_table->item(row, 1)) {
        item->setText(name);
    }
}

int LanScanDialog::rowForIp(const QString &ip) const
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem *item = m_table->item(row, 0);
        if (item && item->text() == ip) {
            return row;
        }
    }
    return -1;
}

void LanScanDialog::finishLiveness()
{
    if (!m_scanning) {
        return;
    }
    m_scanning = false;

    // 存活判定以 ARP 表为准：同网段设备必然出现在 ARP 缓存中，而某些
    // 代理 ARP / 隧道会对任意地址回复 ICMP，只看 Ping 结果会大面积误报。
    if (m_arpSucceeded) {
        for (auto it = m_arpEntries.cbegin(); it != m_arpEntries.cend(); ++it) {
            addOrUpdateDevice(it.key(), it.value());
        }
        for (auto it = m_localMacs.cbegin(); it != m_localMacs.cend(); ++it) {
            if (m_scanTargets.contains(it.key())) {
                addOrUpdateDevice(it.key(), it.value());
            }
        }
    } else {
        for (const QString &ip : std::as_const(m_pingAlive)) {
            addOrUpdateDevice(ip);
        }
    }

    m_progressBar->setValue(m_totalPings);
    setScanningUi(false);
    m_table->setSortingEnabled(true);
    m_table->sortItems(0, Qt::AscendingOrder);

    if (m_alive.isEmpty()) {
        m_statusLabel->setText(tr("扫描完成：未发现设备。"));
    } else {
        m_statusLabel->setText(tr("扫描完成：发现 %1 台设备。").arg(m_alive.size()));
    }
    if (!m_arpSucceeded) {
        m_statusLabel->setText(m_statusLabel->text() + QLatin1Char(' ')
                               + tr("（ARP 表不可用，结果可能包含误报）"));
    }
    if (m_pingStartFailed) {
        m_statusLabel->setText(m_statusLabel->text() + QLatin1Char(' ')
                               + tr("（无法启动 ping，请检查系统 PATH）"));
    }

    beginNameResolution();
}

void LanScanDialog::beginNameResolution()
{
    sendNbnsQueries();
    startSsdpDiscovery();
    QTimer::singleShot(800, this, &LanScanDialog::resolveRemainingNames);
    // 名称解析失败时不要一直停留在“解析中…”；稍后到达的结果仍可覆盖“未知”。
    QTimer::singleShot(8000, this, &LanScanDialog::markUnresolvedNames);
}

void LanScanDialog::startSsdpDiscovery()
{
    if (m_alive.isEmpty()) {
        return;
    }

    if (!m_ssdpSocket) {
        m_ssdpSocket = new QUdpSocket(this);
        connect(m_ssdpSocket, &QUdpSocket::readyRead,
                this, &LanScanDialog::readSsdpResponses);
    }
    if (m_ssdpSocket->state() != QAbstractSocket::BoundState
            && !m_ssdpSocket->bind(QHostAddress::AnyIPv4, 0)) {
        return;
    }

    const QByteArray message =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: ssdp:all\r\n\r\n";
    m_ssdpSocket->writeDatagram(message,
                                QHostAddress(QStringLiteral("239.255.255.250")), 1900);

    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    }
}

void LanScanDialog::readSsdpResponses()
{
    while (m_ssdpSocket && m_ssdpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_ssdpSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_ssdpSocket->readDatagram(datagram.data(), datagram.size(),
                                   &sender, &senderPort);
        Q_UNUSED(senderPort);

        const QString ip = sender.toString();
        if (!m_alive.contains(ip) || m_ssdpQueried.contains(ip)) {
            continue;
        }

        QString location;
        const QString text = QString::fromUtf8(datagram);
        const QStringList lines = text.split(QStringLiteral("\r\n"));
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("LOCATION:"), Qt::CaseInsensitive)) {
                location = line.mid(int(qstrlen("LOCATION:"))).trimmed();
                break;
            }
        }
        if (location.isEmpty()) {
            continue;
        }

        QUrl url(location);
        if (!url.isValid()
                || (url.scheme() != QLatin1String("http")
                    && url.scheme() != QLatin1String("https"))) {
            continue;
        }

        m_ssdpQueried.insert(ip);
        fetchSsdpDescription(sender, url);
    }
}

void LanScanDialog::fetchSsdpDescription(const QHostAddress &device, const QUrl &location)
{
    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    }

    QNetworkRequest request(location);
    request.setTransferTimeout(2500);
    QNetworkReply *reply = m_network->get(request);
    const QString ip = device.toString();
    connect(reply, &QNetworkReply::finished, this, [this, reply, ip]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }
        const QByteArray data = reply->read(64 * 1024);
        QXmlStreamReader reader(data);
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement()
                    && reader.name() == QLatin1String("friendlyName")) {
                const QString name = reader.readElementText().trimmed();
                if (!name.isEmpty()) {
                    updateDeviceName(ip, name.left(80), 2);
                }
                return;
            }
        }
    });
}

void LanScanDialog::sendNbnsQueries()
{
    if (m_alive.isEmpty()) {
        return;
    }
    if (!m_nbnsSocket) {
        m_nbnsSocket = new QUdpSocket(this);
        if (!m_nbnsSocket->bind(QHostAddress::AnyIPv4, 0)) {
            m_nbnsSocket->deleteLater();
            m_nbnsSocket = nullptr;
            return;
        }
        connect(m_nbnsSocket, &QUdpSocket::readyRead,
                this, &LanScanDialog::readNbnsResponses);
    }

    const QByteArray request = nbnsNodeStatusRequest();
    for (const QString &ip : std::as_const(m_alive)) {
        m_nbnsSocket->writeDatagram(request, QHostAddress(ip), 137);
    }
}

void LanScanDialog::readNbnsResponses()
{
    while (m_nbnsSocket && m_nbnsSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_nbnsSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_nbnsSocket->readDatagram(datagram.data(), datagram.size(),
                                   &sender, &senderPort);
        if (senderPort != 137) {
            continue;
        }
        const QString name = parseNbnsNodeStatus(datagram);
        if (!name.isEmpty()) {
            updateDeviceName(sender.toString(), name, 3);
        }
    }
}

void LanScanDialog::resolveRemainingNames()
{
    for (const QString &ip : std::as_const(m_alive)) {
        if (m_namePriority.value(ip, 0) >= 2) {
            continue;
        }
        QHostInfo::lookupHost(ip, this, [this, ip](const QHostInfo &info) {
            if (info.error() != QHostInfo::NoError) {
                return;
            }
            QString name = info.hostName();
            if (name.endsWith(QLatin1Char('.'))) {
                name.chop(1);
            }
            if (name.isEmpty() || name == ip) {
                return;
            }
            updateDeviceName(ip, name, 1);
        });
    }
}

void LanScanDialog::markUnresolvedNames()
{
    for (const QString &ip : std::as_const(m_alive)) {
        if (m_namePriority.value(ip, 0) != 0) {
            continue;
        }
        const int row = rowForIp(ip);
        if (row < 0) {
            continue;
        }
        if (QTableWidgetItem *item = m_table->item(row, 1)) {
            if (item->text() == tr("解析中…")) {
                item->setText(tr("未知"));
            }
        }
    }
}

QByteArray LanScanDialog::nbnsNodeStatusRequest()
{
    QByteArray request(50, '\0');
    request[0] = char(QRandomGenerator::global()->bounded(256));
    request[1] = char(QRandomGenerator::global()->bounded(256));
    request[5] = 0x01;   // QDCOUNT = 1
    request[12] = 0x20;  // 32 字节的 NetBIOS 名称

    static const char alphabet[] = "ABCDEFGHIJKLMNOP";
    int pos = 13;
    for (int i = 0; i < 16; ++i) {
        const quint8 value = i == 0 ? 0x2A : 0x00;   // "*" + 15 个 0
        request[pos++] = alphabet[(value >> 4) & 0x0F];
        request[pos++] = alphabet[value & 0x0F];
    }
    request[45] = 0x00;  // 名称结束
    request[47] = 0x21;  // type = NBSTAT
    request[49] = 0x01;  // class = IN
    return request;
}

QString LanScanDialog::parseNbnsNodeStatus(const QByteArray &data)
{
    if (data.size() < 12) {
        return {};
    }

    const auto byteAt = [&data](int index) -> quint8 {
        return static_cast<quint8>(data.at(index));
    };
    const auto u16At = [&byteAt](int index) -> int {
        return (byteAt(index) << 8) | byteAt(index + 1);
    };

    const int questionCount = u16At(4);
    const int answerCount = u16At(6);
    if (answerCount <= 0) {
        return {};
    }

    int pos = 12;
    const auto skipName = [&]() -> bool {
        while (pos < data.size()) {
            const quint8 length = byteAt(pos);
            if (length == 0) {
                ++pos;
                return true;
            }
            if ((length & 0xC0) == 0xC0) {
                if (pos + 1 >= data.size()) {
                    return false;
                }
                pos += 2;
                return true;
            }
            if (length & 0xC0) {
                return false;
            }
            pos += 1 + int(length);
            if (pos > data.size()) {
                return false;
            }
        }
        return false;
    };

    for (int i = 0; i < questionCount; ++i) {
        if (!skipName() || pos + 4 > data.size()) {
            return {};
        }
        pos += 4;   // type + class
    }

    for (int i = 0; i < answerCount; ++i) {
        if (!skipName() || pos + 10 > data.size()) {
            return {};
        }
        const int type = u16At(pos);
        pos += 2;
        pos += 2;   // class
        pos += 4;   // TTL
        const int rdLength = u16At(pos);
        pos += 2;
        if (pos + rdLength > data.size()) {
            return {};
        }

        if (type == 0x21 && rdLength >= 1) {
            const int nameCount = byteAt(pos);
            int namePos = pos + 1;
            QString fallback;
            for (int n = 0; n < nameCount && namePos + 18 <= pos + rdLength; ++n) {
                const QByteArray rawName = data.mid(namePos, 15);
                const int suffix = byteAt(namePos + 15);
                const int flags = u16At(namePos + 16);
                namePos += 18;

                const bool groupName = (flags & 0x8000) != 0;
                const QString name = QString::fromLatin1(rawName).trimmed();
                if (name.isEmpty() || groupName) {
                    continue;
                }
                if (suffix == 0x00) {   // 工作站名
                    return name;
                }
                if (fallback.isEmpty()) {
                    fallback = name;
                }
            }
            if (!fallback.isEmpty()) {
                return fallback;
            }
        }
        pos += rdLength;
    }

    return {};
}

QString LanScanDialog::normalizeMac(const QString &mac)
{
    const QStringList parts = mac.split(QRegularExpression(QStringLiteral("[:-]")));
    if (parts.size() != 6) {
        return {};
    }

    QStringList normalized;
    normalized.reserve(6);
    for (const QString &part : parts) {
        bool ok = false;
        const uint value = part.toUInt(&ok, 16);
        if (!ok || value > 0xFF) {
            return {};
        }
        normalized.append(QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper());
    }

    const QString result = normalized.join(QLatin1Char(':'));
    if (result == QLatin1String("00:00:00:00:00:00")) {
        return {};
    }
    return result;
}

void LanScanDialog::closeEvent(QCloseEvent *event)
{
    if (m_scanning) {
        stopScan();
    }
    QDialog::closeEvent(event);
}
