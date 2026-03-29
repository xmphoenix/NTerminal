#include "portscannerdialog.h"
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
    setWindowTitle("Port Scanner");
    resize(600, 500);
}

PortScannerDialog::~PortScannerDialog()
{
    stopPortScan();
}

void PortScannerDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 目标主机设置
    QGroupBox *targetGroup = new QGroupBox("Target", this);
    QGridLayout *targetLayout = new QGridLayout(targetGroup);

    targetLayout->addWidget(new QLabel("Host/IP:", this), 0, 0);
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setPlaceholderText("e.g., 192.168.1.1 or example.com");
    targetLayout->addWidget(m_hostEdit, 0, 1, 1, 3);

    m_pingButton = new QPushButton("Ping", this);
    connect(m_pingButton, &QPushButton::clicked, this, &PortScannerDialog::onPingHost);
    targetLayout->addWidget(m_pingButton, 0, 4);

    mainLayout->addWidget(targetGroup);

    // 端口范围设置
    QGroupBox *portGroup = new QGroupBox("Port Range", this);
    QGridLayout *portLayout = new QGridLayout(portGroup);

    m_commonPortsCheck = new QCheckBox("Scan Common Ports Only", this);
    m_commonPortsCheck->setChecked(true);
    connect(m_commonPortsCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_startPortSpin->setEnabled(!checked);
        m_endPortSpin->setEnabled(!checked);
    });
    portLayout->addWidget(m_commonPortsCheck, 0, 0, 1, 4);

    portLayout->addWidget(new QLabel("Start Port:", this), 1, 0);
    m_startPortSpin = new QSpinBox(this);
    m_startPortSpin->setRange(1, 65535);
    m_startPortSpin->setValue(1);
    m_startPortSpin->setEnabled(false);
    portLayout->addWidget(m_startPortSpin, 1, 1);

    portLayout->addWidget(new QLabel("End Port:", this), 1, 2);
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
    m_startButton = new QPushButton("Start Scan", this);
    connect(m_startButton, &QPushButton::clicked, this, &PortScannerDialog::onStartScan);
    buttonLayout->addWidget(m_startButton);

    m_stopButton = new QPushButton("Stop", this);
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked, this, &PortScannerDialog::onStopScan);
    buttonLayout->addWidget(m_stopButton);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    // 日志输出
    QLabel *logLabel = new QLabel("Scan Results:", this);
    mainLayout->addWidget(logLabel);

    m_logText = new QTextEdit(this);
    m_logText->setReadOnly(true);
    m_logText->setFont(QFont("Courier New", 9));
    mainLayout->addWidget(m_logText);
}

void PortScannerDialog::onStartScan()
{
    QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please enter a host or IP address.");
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
            QMessageBox::warning(this, "Error", "Start port must be less than or equal to end port.");
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
    appendLog("Scan stopped by user.", "orange");
}

void PortScannerDialog::onPingHost()
{
    QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please enter a host or IP address.");
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
    appendLog(QString("=== Port Scan Started ==="), "blue");
    appendLog(QString("Target: %1").arg(m_currentHost), "blue");
    appendLog(QString("Scanning %1 ports...").arg(m_portsToScan.size()), "blue");
    appendLog(QString("Time: %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")), "blue");
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
            appendLog(QString("=== Scan Completed ==="), "blue");
            appendLog(QString("Total ports scanned: %1").arg(m_portsToScan.size()), "blue");
            appendLog(QString("Open ports found: %1").arg(m_openPortCount), "green");
            appendLog(QString("Time: %1").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")), "blue");
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

    appendLog(QString("Port %1 is OPEN - %2").arg(port).arg(serviceName), "green");
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
    QString html = QString("<span style='color:%1;'>%2</span>").arg(color).arg(message);
    m_logText->append(html);
}

void PortScannerDialog::pingHost(const QString &host)
{
    appendLog("");
    appendLog(QString("=== Pinging %1 ===").arg(host), "blue");

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
            appendLog("Ping completed successfully.", "green");
        } else {
            appendLog("Ping failed or host unreachable.", "red");
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

    return serviceNames.value(port, "Unknown");
}
