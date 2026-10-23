#include "ui/portscannerdialog.h"
#include "ui/iconprovider.h"
#include "theme/thememanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QDateTime>

// 常用端口列表
const QList<int> PortScannerDialog::COMMON_PORTS = {
    21,    // FTP
    22,    // SSH
    23,    // Telnet
    25,    // SMTP
    53,    // DNS
    80,    // HTTP
    110,   // POP3
    143,   // IMAP
    443,   // HTTPS
    445,   // SMB
    3306,  // MySQL
    3389,  // RDP
    5432,  // PostgreSQL
    5900,  // VNC
    8080,  // HTTP Alternate
    8443   // HTTPS Alternate
};

PortScannerDialog::PortScannerDialog(QWidget *parent)
    : QDialog(parent)
    , m_socket(nullptr)
    , m_timeoutTimer(nullptr)
    , m_currentPortIndex(0)
    , m_openPortCount(0)
    , m_scanning(false)
{
    setupUi();
    setWindowTitle(tr("端口扫描"));
    resize(600, 500);
}

PortScannerDialog::~PortScannerDialog()
{
    stopPortScan();
}

void PortScannerDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(14);

    // 头部：标题 + 说明
    QLabel *titleLabel = new QLabel(tr("端口扫描"), this);
    titleLabel->setProperty("class", "sectionTitle");
    QLabel *hintLabel = new QLabel(tr("探测目标主机的常用端口或指定端口范围，仅用于你拥有权限的网络"), this);
    hintLabel->setProperty("class", "hint");
    hintLabel->setWordWrap(true);
    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(hintLabel);

    // 目标主机设置
    QGroupBox *targetGroup = new QGroupBox(tr("目标主机"), this);
    QGridLayout *targetLayout = new QGridLayout(targetGroup);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->setHorizontalSpacing(10);
    targetLayout->setVerticalSpacing(10);
    targetLayout->setColumnStretch(1, 1);
    targetLayout->setColumnStretch(2, 1);
    targetLayout->setColumnStretch(3, 1);

    targetLayout->addWidget(new QLabel(tr("主机/IP:"), this), 0, 0);
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setPlaceholderText(tr("例如：192.168.1.1 或 example.com"));
    targetLayout->addWidget(m_hostEdit, 0, 1, 1, 3);

    m_pingButton = new QPushButton(IconProvider::themedIcon("globe"), tr("Ping"), this);
    m_pingButton->setProperty("class", "tonal");
    connect(m_pingButton, &QPushButton::clicked, this, &PortScannerDialog::onPingHost);
    targetLayout->addWidget(m_pingButton, 0, 4);

    mainLayout->addWidget(targetGroup);

    // 端口范围设置
    QGroupBox *portGroup = new QGroupBox(tr("端口范围"), this);
    QGridLayout *portLayout = new QGridLayout(portGroup);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->setHorizontalSpacing(10);
    portLayout->setVerticalSpacing(10);
    portLayout->setColumnStretch(3, 1);

    m_commonPortsCheck = new QCheckBox(tr("仅扫描常用端口"), this);
    m_commonPortsCheck->setChecked(true);
    connect(m_commonPortsCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_startPortSpin->setEnabled(!checked);
        m_endPortSpin->setEnabled(!checked);
    });
    portLayout->addWidget(m_commonPortsCheck, 0, 0, 1, 4);

    portLayout->addWidget(new QLabel(tr("起始端口:"), this), 1, 0);
    m_startPortSpin = new QSpinBox(this);
    m_startPortSpin->setRange(1, 65535);
    m_startPortSpin->setValue(1);
    m_startPortSpin->setEnabled(false);
    portLayout->addWidget(m_startPortSpin, 1, 1);

    portLayout->addWidget(new QLabel(tr("结束端口:"), this), 1, 2);
    m_endPortSpin = new QSpinBox(this);
    m_endPortSpin->setRange(1, 65535);
    m_endPortSpin->setValue(1024);
    m_endPortSpin->setEnabled(false);
    portLayout->addWidget(m_endPortSpin, 1, 3);

    mainLayout->addWidget(portGroup);

    // 进度条
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);

    // 控制按钮
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    m_startButton = new QPushButton(IconProvider::themedIcon("play"), tr("开始扫描"), this);
    m_startButton->setProperty("class", "primary");
    connect(m_startButton, &QPushButton::clicked, this, &PortScannerDialog::onStartScan);
    buttonLayout->addWidget(m_startButton);

    m_stopButton = new QPushButton(IconProvider::themedIcon("stop"), tr("停止"), this);
    m_stopButton->setProperty("class", "ghost");
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &PortScannerDialog::onStopScan);
    buttonLayout->addWidget(m_stopButton);

    buttonLayout->addStretch();
    QPushButton *closeButton = new QPushButton(tr("关闭"), this);
    closeButton->setProperty("class", "ghost");
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    // 日志输出
    QLabel *logLabel = new QLabel(tr("扫描结果:"), this);
    logLabel->setProperty("class", "hint");
    mainLayout->addWidget(logLabel);

    m_logText = new QTextEdit(this);
    m_logText->setReadOnly(true);
    m_logText->setPlaceholderText(tr("扫描结果将显示在这里…"));
    QFont monoFont = m_logText->font();
    monoFont.setFamilies({"JetBrains Mono", "Consolas", "Cascadia Mono"});
    monoFont.setPixelSize(12);
    m_logText->setFont(monoFont);
    mainLayout->addWidget(m_logText, 1);
}

void PortScannerDialog::onStartScan()
{
    QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::warning(this, tr("错误"), tr("请输入主机名或 IP 地址。"));
        return;
    }

    m_currentHost = host;
    m_portsToScan.clear();

    if (m_commonPortsCheck->isChecked()) {
        m_portsToScan = COMMON_PORTS;
    } else {
        int startPort = m_startPortSpin->value();
        int endPort = m_endPortSpin->value();

        if (startPort > endPort) {
            QMessageBox::warning(this, tr("错误"), tr("起始端口必须小于或等于结束端口。"));
            return;
        }

        for (int port = startPort; port <= endPort; ++port) {
            m_portsToScan.append(port);
        }
    }

    startPortScan();
}

void PortScannerDialog::onStopScan()
{
    stopPortScan();
    appendLog(tr("扫描已被用户停止。"), "orange");
}

void PortScannerDialog::onPingHost()
{
    QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::warning(this, tr("错误"), tr("请输入主机名或 IP 地址。"));
        return;
    }

    pingHost(host);
}

void PortScannerDialog::startPortScan()
{
    m_scanning = true;
    m_currentPortIndex = 0;
    m_openPortCount = 0;

    m_startButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_hostEdit->setEnabled(false);
    m_commonPortsCheck->setEnabled(false);
    m_startPortSpin->setEnabled(false);
    m_endPortSpin->setEnabled(false);
    m_pingButton->setEnabled(false);

    m_progressBar->setRange(0, m_portsToScan.size());
    m_progressBar->setValue(0);

    m_logText->clear();
    appendLog(tr("=== 端口扫描开始 ==="), "blue");
    appendLog(tr("目标: %1").arg(m_currentHost), "blue");
    appendLog(tr("正在扫描 %1 个端口...").arg(m_portsToScan.size()), "blue");
    appendLog(tr("时间: %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")), "blue");
    appendLog("");

    // 创建socket和定时器
    if (!m_socket) {
        m_socket = new QTcpSocket(this);
        connect(m_socket, &QTcpSocket::connected, this, &PortScannerDialog::onSocketConnected);
        connect(m_socket, &QTcpSocket::errorOccurred, this, &PortScannerDialog::onSocketError);
    }

    if (!m_timeoutTimer) {
        m_timeoutTimer = new QTimer(this);
        m_timeoutTimer->setSingleShot(true);
        connect(m_timeoutTimer, &QTimer::timeout, this, &PortScannerDialog::onSocketTimeout);
    }

    scanNextPort();
}

void PortScannerDialog::stopPortScan()
{
    m_scanning = false;

    if (m_socket) {
        m_socket->abort();
    }

    if (m_timeoutTimer) {
        m_timeoutTimer->stop();
    }

    m_startButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_hostEdit->setEnabled(true);
    m_commonPortsCheck->setEnabled(true);
    m_startPortSpin->setEnabled(!m_commonPortsCheck->isChecked());
    m_endPortSpin->setEnabled(!m_commonPortsCheck->isChecked());
    m_pingButton->setEnabled(true);
}

void PortScannerDialog::scanNextPort()
{
    if (!m_scanning || m_currentPortIndex >= m_portsToScan.size()) {
        // 扫描完成
        if (m_scanning) {
            appendLog("");
            appendLog(tr("=== 扫描完成 ==="), "blue");
            appendLog(tr("共扫描端口: %1").arg(m_portsToScan.size()), "blue");
            appendLog(tr("发现开放端口: %1").arg(m_openPortCount), "green");
            appendLog(tr("时间: %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")), "blue");
        }
        stopPortScan();
        return;
    }

    int port = m_portsToScan[m_currentPortIndex];
    m_progressBar->setValue(m_currentPortIndex + 1);

    // 开始连接
    m_socket->abort();
    m_socket->connectToHost(m_currentHost, port);

    // 设置超时（500ms）
    m_timeoutTimer->start(500);
}

void PortScannerDialog::onSocketConnected()
{
    m_timeoutTimer->stop();

    int port = m_portsToScan[m_currentPortIndex];
    QString serviceName = getServiceName(port);

    appendLog(tr("端口 %1 开放 - %2").arg(port).arg(serviceName), "green");
    m_openPortCount++;

    m_socket->disconnectFromHost();

    m_currentPortIndex++;
    QTimer::singleShot(10, this, &PortScannerDialog::scanNextPort);
}

void PortScannerDialog::onSocketError()
{
    m_timeoutTimer->stop();

    // 端口关闭或无法访问（不记录，避免日志过多）
    m_currentPortIndex++;
    QTimer::singleShot(10, this, &PortScannerDialog::scanNextPort);
}

void PortScannerDialog::onSocketTimeout()
{
    // 超时，认为端口关闭
    m_socket->abort();
    m_currentPortIndex++;
    QTimer::singleShot(10, this, &PortScannerDialog::scanNextPort);
}

void PortScannerDialog::appendLog(const QString &message, const QString &color)
{
    // 颜色使用主题令牌，保证浅色/深色主题下都清晰可读
    const AppTheme theme = ThemeManager::instance()->currentAppTheme();
    QColor c = theme.windowText;
    if (color == QLatin1String("blue")) {
        c = theme.accent;
    } else if (color == QLatin1String("green")) {
        c = theme.success;
    } else if (color == QLatin1String("red")) {
        c = theme.danger;
    } else if (color == QLatin1String("orange")) {
        c = theme.warning;
    }

    QString html = QStringLiteral("<span style=\"color:%1;\">%2</span>")
                       .arg(c.name(),
                            message.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    m_logText->append(html);
}

void PortScannerDialog::pingHost(const QString &host)
{
    appendLog("");
    appendLog(tr("=== 正在 Ping %1 ===").arg(host), "blue");

    QProcess *pingProcess = new QProcess(this);

#ifdef Q_OS_WIN
    QString program = "ping";
    QStringList arguments;
    arguments << "-n" << "4" << host;
#else
    QString program = "ping";
    QStringList arguments;
    arguments << "-c" << "4" << host;
#endif

    connect(pingProcess, &QProcess::readyReadStandardOutput, this, [this, pingProcess]() {
        QString output = QString::fromLocal8Bit(pingProcess->readAllStandardOutput());
        appendLog(output.trimmed(), "black");
    });

    connect(pingProcess, &QProcess::readyReadStandardError, this, [this, pingProcess]() {
        QString error = QString::fromLocal8Bit(pingProcess->readAllStandardError());
        appendLog(error.trimmed(), "red");
    });

    connect(pingProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, pingProcess](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitCode == 0) {
            appendLog(tr("Ping 完成。"), "green");
        } else {
            appendLog(tr("Ping 失败或主机不可达。"), "red");
        }
        pingProcess->deleteLater();
    });

    pingProcess->start(program, arguments);
}

QString PortScannerDialog::getServiceName(int port)
{
    static const QMap<int, QString> serviceNames = {
        {21, "FTP"},
        {22, "SSH"},
        {23, "Telnet"},
        {25, "SMTP"},
        {53, "DNS"},
        {80, "HTTP"},
        {110, "POP3"},
        {143, "IMAP"},
        {443, "HTTPS"},
        {445, "SMB"},
        {3306, "MySQL"},
        {3389, "RDP"},
        {5432, "PostgreSQL"},
        {5900, "VNC"},
        {8080, "HTTP-Alt"},
        {8443, "HTTPS-Alt"}
    };

    return serviceNames.value(port, tr("未知"));
}
