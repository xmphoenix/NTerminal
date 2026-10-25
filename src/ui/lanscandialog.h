#ifndef LANSCANDIALOG_H
#define LANSCANDIALOG_H

#include <QDialog>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QSet>
#include <QStringList>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QProcess;
class QUdpSocket;
class QCloseEvent;
class QNetworkAccessManager;
class QUrl;

// ============================================================================
// 局域网设备扫描（工具 → 局域网设备扫描）
//
// 选择本机的一个 IPv4 网段后，用并发 Ping 探测存活主机，再读取系统 ARP
// 表补全不响应 ICMP 的设备；设备名称依次尝试 NetBIOS（NBNS，端口 137）
// 和反向 DNS，MAC 地址来自 ARP 表。
// ============================================================================
class LanScanDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LanScanDialog(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct NetworkTarget {
        QString label;
        QHostAddress address;
        int prefixLength = 24;
    };

    void setupUi();
    void refreshInterfaces();
    void updateReadyState();
    void startScan();
    void stopScan();
    void startNextPings();
    void onPingFinished(const QString &ip, bool alive);
    void setScanningUi(bool scanning);

    void readArpTable();
    void parseArpOutput(const QString &output);
    void applyArpEntries(const QList<QPair<QString, QString>> &entries);

    void finishLiveness();
    void beginNameResolution();
    void sendNbnsQueries();
    void readNbnsResponses();
    void startSsdpDiscovery();
    void readSsdpResponses();
    void fetchSsdpDescription(const QHostAddress &device, const QUrl &location);
    void resolveRemainingNames();
    void markUnresolvedNames();

    void addOrUpdateDevice(const QString &ip, const QString &mac = QString());
    void updateDeviceName(const QString &ip, const QString &name, int priority);
    int rowForIp(const QString &ip) const;

    QStringList buildAddressList(const NetworkTarget &target, bool *truncated) const;
    static QStringList pingArguments(const QString &ip);
    static QByteArray nbnsNodeStatusRequest();
    static QString parseNbnsNodeStatus(const QByteArray &data);
    static QString normalizeMac(const QString &mac);
    static void suppressProcessWindow(QProcess *process);

    QComboBox *m_interfaceCombo = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QTableWidget *m_table = nullptr;

    QList<NetworkTarget> m_targets;
    QSet<QString> m_scanTargets;   // 本轮扫描的地址集合，用于过滤 ARP 表
    QSet<QString> m_alive;         // 已确认存活的设备 IP
    QSet<QString> m_pingAlive;     // 响应 Ping 的 IP（仅作 ARP 不可用时的回退）
    QHash<QString, QString> m_arpEntries;   // ARP 表：IP -> MAC
    QStringList m_pendingPings;
    QHash<QProcess *, QString> m_activePings;
    QHash<QString, int> m_namePriority;   // 0=未解析 1=DNS 2=SSDP 3=NetBIOS
    QHash<QString, QString> m_localMacs;  // 本机地址 -> MAC（ARP 表不含本机）
    QProcess *m_arpProcess = nullptr;
    QUdpSocket *m_nbnsSocket = nullptr;
    QUdpSocket *m_ssdpSocket = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QSet<QString> m_ssdpQueried;

    int m_completedPings = 0;
    int m_totalPings = 0;
    bool m_scanning = false;
    bool m_pingStartFailed = false;
    bool m_arpSucceeded = false;
    QString m_scanSubnet;
};

#endif // LANSCANDIALOG_H
