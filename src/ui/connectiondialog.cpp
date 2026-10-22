#include "ui/connectiondialog.h"
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QStackedWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QSerialPortInfo>
#include <QMessageBox>
#include <QDebug>

namespace {
// 文件内简写：协议类型常量（实现细节，公共接口见 connectiondialog.h）
using ConnectionType = Common::ConnectionType;
using ConnectionSettings = Common::ConnectionSettings;
constexpr ConnectionType Serial = ConnectionType::Serial;
constexpr ConnectionType Telnet = ConnectionType::Telnet;
constexpr ConnectionType SSH    = ConnectionType::SSH;
constexpr ConnectionType Rlogin = ConnectionType::Rlogin;
constexpr ConnectionType SFTP   = ConnectionType::SFTP;
constexpr ConnectionType FTP    = ConnectionType::FTP;
constexpr ConnectionType TFTP   = ConnectionType::TFTP;
}

ConnectionDialog::ConnectionDialog(QWidget *parent)
    : QDialog(parent)
    , m_sessionNameEdit(nullptr)
    , m_typeComboBox(nullptr)
    , m_settingsStack(nullptr)
    , m_portComboBox(nullptr)
    , m_baudRateComboBox(nullptr)
    , m_dataBitsComboBox(nullptr)
    , m_parityComboBox(nullptr)
    , m_stopBitsComboBox(nullptr)
    , m_flowControlComboBox(nullptr)
    , m_telnetHostEdit(nullptr)
    , m_telnetPortSpinBox(nullptr)
    , m_telnetUsernameEdit(nullptr)
    , m_telnetPasswordEdit(nullptr)
    , m_sshHostEdit(nullptr)
    , m_sshPortSpinBox(nullptr)
    , m_sshUsernameEdit(nullptr)
    , m_sshPasswordEdit(nullptr)
    , m_useSshKeyCheckBox(nullptr)
    , m_sshKeyPathEdit(nullptr)
    , m_browseKeyButton(nullptr)
    , m_rloginHostEdit(nullptr)
    , m_rloginPortSpinBox(nullptr)
    , m_rloginUsernameEdit(nullptr)
    , m_rloginPasswordEdit(nullptr)
    , m_sftpHostEdit(nullptr)
    , m_sftpPortSpinBox(nullptr)
    , m_sftpUsernameEdit(nullptr)
    , m_sftpPasswordEdit(nullptr)
    , m_useSftpKeyCheckBox(nullptr)
    , m_sftpKeyPathEdit(nullptr)
    , m_browseSftpKeyButton(nullptr)
    , m_ftpHostEdit(nullptr)
    , m_ftpPortSpinBox(nullptr)
    , m_ftpUsernameEdit(nullptr)
    , m_ftpPasswordEdit(nullptr)
    , m_tftpHostEdit(nullptr)
    , m_tftpPortSpinBox(nullptr)
    , m_okButton(nullptr)
    , m_cancelButton(nullptr)
{
    setWindowTitle("New Connection");
    setupUi();

    m_currentSettings.type = Serial;
    m_currentSettings.sessionName = "New Connection";
    m_currentSettings.portName = "";
    m_currentSettings.baudRate = 115200;
    m_currentSettings.dataBits = QSerialPort::Data8;
    m_currentSettings.parity = QSerialPort::NoParity;
    m_currentSettings.stopBits = QSerialPort::OneStop;
    m_currentSettings.flowControl = QSerialPort::NoFlowControl;
    m_currentSettings.host = "127.0.0.1";
    m_currentSettings.port = 23;
    m_currentSettings.username = "";
    m_currentSettings.password = "";
    m_currentSettings.useSshKey = false;
    m_currentSettings.sshKeyPath = "";
}

ConnectionDialog::~ConnectionDialog()
{
}

void ConnectionDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Session name
    QFormLayout *formLayout = new QFormLayout();
    m_sessionNameEdit = new QLineEdit("New Connection");
    formLayout->addRow("Session Name:", m_sessionNameEdit);

    // Connection type
    m_typeComboBox = new QComboBox();
    m_typeComboBox->addItem("Serial", static_cast<int>(Serial));
    m_typeComboBox->addItem("Telnet", static_cast<int>(Telnet));
    m_typeComboBox->addItem("SSH", static_cast<int>(SSH));
    m_typeComboBox->addItem("Rlogin", static_cast<int>(Rlogin));
    m_typeComboBox->addItem("SFTP", static_cast<int>(SFTP));
    m_typeComboBox->addItem("FTP", static_cast<int>(FTP));
    m_typeComboBox->addItem("TFTP", static_cast<int>(TFTP));
    formLayout->addRow("Connection Type:", m_typeComboBox);

    mainLayout->addLayout(formLayout);

    // Settings stack
    m_settingsStack = new QStackedWidget();

    // Create settings widgets
    createSerialSettingsWidget();
    createTelnetSettingsWidget();
    createSshSettingsWidget();
    createRloginSettingsWidget();
    createSftpSettingsWidget();
    createFtpSettingsWidget();
    createTftpSettingsWidget();

    mainLayout->addWidget(m_settingsStack);

    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_okButton = new QPushButton("OK");
    m_cancelButton = new QPushButton("Cancel");
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect signals
    connect(m_typeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConnectionDialog::onConnectionTypeChanged);
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    
    // Set initial type
    onConnectionTypeChanged(0);
}

void ConnectionDialog::createSerialSettingsWidget()
{
    QWidget *widget = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);

    // Port selection
    m_portComboBox = new QComboBox();
    QPushButton *refreshButton = new QPushButton("Refresh");
    QHBoxLayout *portLayout = new QHBoxLayout();
    portLayout->addWidget(m_portComboBox);
    portLayout->addWidget(refreshButton);
    layout->addRow("Serial Port:", portLayout);

    // Baud rate
    m_baudRateComboBox = new QComboBox();
    m_baudRateComboBox->addItem("1200", 1200);
    m_baudRateComboBox->addItem("2400", 2400);
    m_baudRateComboBox->addItem("4800", 4800);
    m_baudRateComboBox->addItem("9600", 9600);
    m_baudRateComboBox->addItem("19200", 19200);
    m_baudRateComboBox->addItem("38400", 38400);
    m_baudRateComboBox->addItem("57600", 57600);
    m_baudRateComboBox->addItem("115200", 115200);
    m_baudRateComboBox->setCurrentText("115200");
    layout->addRow("Baud Rate:", m_baudRateComboBox);

    // Data bits
    m_dataBitsComboBox = new QComboBox();
    m_dataBitsComboBox->addItem("5", QSerialPort::Data5);
    m_dataBitsComboBox->addItem("6", QSerialPort::Data6);
    m_dataBitsComboBox->addItem("7", QSerialPort::Data7);
    m_dataBitsComboBox->addItem("8", QSerialPort::Data8);
    m_dataBitsComboBox->setCurrentText("8");
    layout->addRow("Data Bits:", m_dataBitsComboBox);

    // Parity
    m_parityComboBox = new QComboBox();
    m_parityComboBox->addItem("None", QSerialPort::NoParity);
    m_parityComboBox->addItem("Odd", QSerialPort::OddParity);
    m_parityComboBox->addItem("Even", QSerialPort::EvenParity);
    m_parityComboBox->addItem("Mark", QSerialPort::MarkParity);
    m_parityComboBox->addItem("Space", QSerialPort::SpaceParity);
    layout->addRow("Parity:", m_parityComboBox);

    // Stop bits
    m_stopBitsComboBox = new QComboBox();
    m_stopBitsComboBox->addItem("1", QSerialPort::OneStop);
    m_stopBitsComboBox->addItem("1.5", QSerialPort::OneAndHalfStop);
    m_stopBitsComboBox->addItem("2", QSerialPort::TwoStop);
    layout->addRow("Stop Bits:", m_stopBitsComboBox);

    // Flow control
    m_flowControlComboBox = new QComboBox();
    m_flowControlComboBox->addItem("None", QSerialPort::NoFlowControl);
    m_flowControlComboBox->addItem("Hardware", QSerialPort::HardwareControl);
    m_flowControlComboBox->addItem("Software", QSerialPort::SoftwareControl);
    layout->addRow("Flow Control:", m_flowControlComboBox);
    
    // Connect refresh button
    connect(refreshButton, &QPushButton::clicked, this, &ConnectionDialog::refreshSerialPorts);
    connect(m_portComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConnectionDialog::onSerialPortChanged);
    
    // Initial port refresh
    refreshSerialPorts();
    
    m_settingsStack->addWidget(widget);
}

void ConnectionDialog::createTelnetSettingsWidget()
{
    QWidget *widget = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);

    m_telnetHostEdit = new QLineEdit("127.0.0.1");
    layout->addRow("Host:", m_telnetHostEdit);

    m_telnetPortSpinBox = new QSpinBox();
    m_telnetPortSpinBox->setRange(1, 65535);
    m_telnetPortSpinBox->setValue(23);
    layout->addRow("Port:", m_telnetPortSpinBox);

    m_telnetUsernameEdit = new QLineEdit();
    layout->addRow("Username:", m_telnetUsernameEdit);

    m_telnetPasswordEdit = new QLineEdit();
    m_telnetPasswordEdit->setEchoMode(QLineEdit::Password);
    layout->addRow("Password:", m_telnetPasswordEdit);
    
    connect(m_telnetHostEdit, &QLineEdit::textChanged, this, &ConnectionDialog::onTelnetHostChanged);
    
    m_settingsStack->addWidget(widget);
}

void ConnectionDialog::createSshSettingsWidget()
{
    QWidget *widget = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);
    
    m_sshHostEdit = new QLineEdit("127.0.0.1");
    layout->addRow("Host:", m_sshHostEdit);
    
    m_sshPortSpinBox = new QSpinBox();
    m_sshPortSpinBox->setRange(1, 65535);
    m_sshPortSpinBox->setValue(22);
    layout->addRow("Port:", m_sshPortSpinBox);
    
    m_sshUsernameEdit = new QLineEdit();
    layout->addRow("Username:", m_sshUsernameEdit);
    
    m_sshPasswordEdit = new QLineEdit();
    m_sshPasswordEdit->setEchoMode(QLineEdit::Password);
    layout->addRow("Password:", m_sshPasswordEdit);
    
    m_useSshKeyCheckBox = new QCheckBox("Use SSH Key");
    layout->addRow("", m_useSshKeyCheckBox);
    
    QHBoxLayout *keyLayout = new QHBoxLayout();
    m_sshKeyPathEdit = new QLineEdit();
    m_browseKeyButton = new QPushButton("Browse...");
    keyLayout->addWidget(m_sshKeyPathEdit);
    keyLayout->addWidget(m_browseKeyButton);
    layout->addRow("SSH Key:", keyLayout);
    
    m_sshKeyPathEdit->setEnabled(false);
    m_browseKeyButton->setEnabled(false);
    
    connect(m_useSshKeyCheckBox, &QCheckBox::toggled, m_sshKeyPathEdit, &QLineEdit::setEnabled);
    connect(m_useSshKeyCheckBox, &QCheckBox::toggled, m_browseKeyButton, &QPushButton::setEnabled);
    connect(m_browseKeyButton, &QPushButton::clicked, this, &ConnectionDialog::browseSshKey);
    connect(m_sshHostEdit, &QLineEdit::textChanged, this, &ConnectionDialog::onSshHostChanged);
    
    m_settingsStack->addWidget(widget);
}

void ConnectionDialog::onConnectionTypeChanged(int index)
{
    ConnectionType type = static_cast<ConnectionType>(m_typeComboBox->itemData(index).toInt());
    m_currentSettings.type = type;

    switch (type) {
    case Serial:
        m_settingsStack->setCurrentIndex(0);
        if (m_portComboBox->isEnabled() && m_portComboBox->currentIndex() >= 0) {
            m_sessionNameEdit->setText(m_portComboBox->currentData().toString());
        } else {
            m_sessionNameEdit->setText("New Serial Connection");
        }
        break;
    case Telnet:
        m_settingsStack->setCurrentIndex(1);
        m_sessionNameEdit->setText(m_telnetHostEdit->text());
        break;
    case SSH:
        m_settingsStack->setCurrentIndex(2);
        m_sessionNameEdit->setText(m_sshHostEdit->text());
        break;
    case Rlogin:
        m_settingsStack->setCurrentIndex(3);
        m_sessionNameEdit->setText(m_rloginHostEdit->text());
        break;
    case SFTP:
        m_settingsStack->setCurrentIndex(4);
        m_sessionNameEdit->setText(m_sftpHostEdit->text());
        break;
    case FTP:
        m_settingsStack->setCurrentIndex(5);
        m_sessionNameEdit->setText(m_ftpHostEdit->text());
        break;
    case TFTP:
        m_settingsStack->setCurrentIndex(6);
        m_sessionNameEdit->setText(m_tftpHostEdit->text());
        break;
    }
}

void ConnectionDialog::refreshSerialPorts()
{
    if (!m_portComboBox) return;
    
    m_portComboBox->clear();
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    
    if (ports.isEmpty()) {
        m_portComboBox->addItem("No serial port detected");
        m_portComboBox->setEnabled(false);
    } else {
        for (const QSerialPortInfo &port : ports) {
            QString description = port.description();
            if (description.isEmpty()) description = "Unknown Device";
            m_portComboBox->addItem(QString("%1 (%2)").arg(port.portName(), description), port.portName());
        }
        m_portComboBox->setEnabled(true);
    }
}

void ConnectionDialog::browseSshKey()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Select SSH Key File", "", "All Files (*)");
    if (!fileName.isEmpty()) {
        m_sshKeyPathEdit->setText(fileName);
    }
}

void ConnectionDialog::onSerialPortChanged(int index)
{
    Q_UNUSED(index);
    ConnectionType type = static_cast<ConnectionType>(m_typeComboBox->currentData().toInt());
    if (type == Serial && m_portComboBox->isEnabled() && m_portComboBox->currentIndex() >= 0) {
        m_sessionNameEdit->setText(m_portComboBox->currentData().toString());
    }
}

void ConnectionDialog::onTelnetHostChanged(const QString &text)
{
    ConnectionType type = static_cast<ConnectionType>(m_typeComboBox->currentData().toInt());
    if (type == Telnet) {
        m_sessionNameEdit->setText(text);
    }
}

void ConnectionDialog::onSshHostChanged(const QString &text)
{
    ConnectionType type = static_cast<ConnectionType>(m_typeComboBox->currentData().toInt());
    if (type == SSH) {
        m_sessionNameEdit->setText(text);
    }
}

void ConnectionDialog::onRloginHostChanged(const QString &text)
{
    ConnectionType type = static_cast<ConnectionType>(m_typeComboBox->currentData().toInt());
    if (type == Rlogin) {
        m_sessionNameEdit->setText(text);
    }
}

void ConnectionDialog::onSftpHostChanged(const QString &text)
{
    ConnectionType type = static_cast<ConnectionType>(m_typeComboBox->currentData().toInt());
    if (type == SFTP) {
        m_sessionNameEdit->setText(text);
    }
}

void ConnectionDialog::onFtpHostChanged(const QString &text)
{
    ConnectionType type = static_cast<ConnectionType>(m_typeComboBox->currentData().toInt());
    if (type == FTP) {
        m_sessionNameEdit->setText(text);
    }
}

void ConnectionDialog::onTftpHostChanged(const QString &text)
{
    ConnectionType type = static_cast<ConnectionType>(m_typeComboBox->currentData().toInt());
    if (type == TFTP) {
        m_sessionNameEdit->setText(text);
    }
}

void ConnectionDialog::createRloginSettingsWidget()
{
    QWidget *widget = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);

    m_rloginHostEdit = new QLineEdit("127.0.0.1");
    layout->addRow("Host:", m_rloginHostEdit);

    m_rloginPortSpinBox = new QSpinBox();
    m_rloginPortSpinBox->setRange(1, 65535);
    m_rloginPortSpinBox->setValue(513);
    layout->addRow("Port:", m_rloginPortSpinBox);

    m_rloginUsernameEdit = new QLineEdit();
    layout->addRow("Username:", m_rloginUsernameEdit);

    m_rloginPasswordEdit = new QLineEdit();
    m_rloginPasswordEdit->setEchoMode(QLineEdit::Password);
    layout->addRow("Password:", m_rloginPasswordEdit);

    connect(m_rloginHostEdit, &QLineEdit::textChanged, this, &ConnectionDialog::onRloginHostChanged);

    m_settingsStack->addWidget(widget);
}

void ConnectionDialog::createSftpSettingsWidget()
{
    QWidget *widget = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);

    m_sftpHostEdit = new QLineEdit("127.0.0.1");
    layout->addRow("Host:", m_sftpHostEdit);

    m_sftpPortSpinBox = new QSpinBox();
    m_sftpPortSpinBox->setRange(1, 65535);
    m_sftpPortSpinBox->setValue(22);
    layout->addRow("Port:", m_sftpPortSpinBox);

    m_sftpUsernameEdit = new QLineEdit();
    layout->addRow("Username:", m_sftpUsernameEdit);

    m_sftpPasswordEdit = new QLineEdit();
    m_sftpPasswordEdit->setEchoMode(QLineEdit::Password);
    layout->addRow("Password:", m_sftpPasswordEdit);

    m_useSftpKeyCheckBox = new QCheckBox("Use SSH Key");
    layout->addRow("", m_useSftpKeyCheckBox);

    QHBoxLayout *keyLayout = new QHBoxLayout();
    m_sftpKeyPathEdit = new QLineEdit();
    m_browseSftpKeyButton = new QPushButton("Browse...");
    keyLayout->addWidget(m_sftpKeyPathEdit);
    keyLayout->addWidget(m_browseSftpKeyButton);
    layout->addRow("SSH Key:", keyLayout);

    m_sftpKeyPathEdit->setEnabled(false);
    m_browseSftpKeyButton->setEnabled(false);

    connect(m_useSftpKeyCheckBox, &QCheckBox::toggled, m_sftpKeyPathEdit, &QLineEdit::setEnabled);
    connect(m_useSftpKeyCheckBox, &QCheckBox::toggled, m_browseSftpKeyButton, &QPushButton::setEnabled);
    connect(m_browseSftpKeyButton, &QPushButton::clicked, [this]() {
        QString fileName = QFileDialog::getOpenFileName(this, "Select SSH Key File", "", "All Files (*)");
        if (!fileName.isEmpty()) {
            m_sftpKeyPathEdit->setText(fileName);
        }
    });
    connect(m_sftpHostEdit, &QLineEdit::textChanged, this, &ConnectionDialog::onSftpHostChanged);

    m_settingsStack->addWidget(widget);
}

void ConnectionDialog::createFtpSettingsWidget()
{
    QWidget *widget = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);

    m_ftpHostEdit = new QLineEdit("127.0.0.1");
    layout->addRow("Host:", m_ftpHostEdit);

    m_ftpPortSpinBox = new QSpinBox();
    m_ftpPortSpinBox->setRange(1, 65535);
    m_ftpPortSpinBox->setValue(21);
    layout->addRow("Port:", m_ftpPortSpinBox);

    m_ftpUsernameEdit = new QLineEdit("anonymous");
    layout->addRow("Username:", m_ftpUsernameEdit);

    m_ftpPasswordEdit = new QLineEdit();
    m_ftpPasswordEdit->setEchoMode(QLineEdit::Password);
    layout->addRow("Password:", m_ftpPasswordEdit);

    connect(m_ftpHostEdit, &QLineEdit::textChanged, this, &ConnectionDialog::onFtpHostChanged);

    m_settingsStack->addWidget(widget);
}

void ConnectionDialog::createTftpSettingsWidget()
{
    QWidget *widget = new QWidget();
    QFormLayout *layout = new QFormLayout(widget);

    m_tftpHostEdit = new QLineEdit("127.0.0.1");
    layout->addRow("Host:", m_tftpHostEdit);

    m_tftpPortSpinBox = new QSpinBox();
    m_tftpPortSpinBox->setRange(1, 65535);
    m_tftpPortSpinBox->setValue(69);
    layout->addRow("Port:", m_tftpPortSpinBox);

    connect(m_tftpHostEdit, &QLineEdit::textChanged, this, &ConnectionDialog::onTftpHostChanged);

    m_settingsStack->addWidget(widget);
}

Common::ConnectionSettings ConnectionDialog::getSettings() const
{
    ConnectionSettings settings = m_currentSettings;
    
    settings.sessionName = m_sessionNameEdit->text();
    
    ConnectionType type = static_cast<ConnectionType>(m_typeComboBox->currentData().toInt());
    settings.type = type;
    
    if (type == Serial) {
        if (m_portComboBox->isEnabled() && m_portComboBox->currentIndex() >= 0) {
            settings.portName = m_portComboBox->currentData().toString();
        }
        settings.baudRate = m_baudRateComboBox->currentData().toInt();
        settings.dataBits = static_cast<QSerialPort::DataBits>(m_dataBitsComboBox->currentData().toInt());
        settings.parity = static_cast<QSerialPort::Parity>(m_parityComboBox->currentData().toInt());
        settings.stopBits = static_cast<QSerialPort::StopBits>(m_stopBitsComboBox->currentData().toInt());
        settings.flowControl = static_cast<QSerialPort::FlowControl>(m_flowControlComboBox->currentData().toInt());
    } else if (type == Telnet) {
        settings.host = m_telnetHostEdit->text();
        settings.port = m_telnetPortSpinBox->value();
        settings.username = m_telnetUsernameEdit->text();
        settings.password = m_telnetPasswordEdit->text();
        
        // Port Warning
        if (settings.port == 22) {
            QMessageBox::warning(nullptr, "Port Warning",
                "You have selected Telnet connection type, but the port is set to 22 (default SSH port).\n\n"
                "Telnet default port is 23.\n\n"
                "If the target service is SSH, please change the connection type to SSH.\n"
                "If the target service is Telnet, please change the port to 23.");
        }
    } else if (type == SSH) {
        settings.host = m_sshHostEdit->text();
        settings.port = m_sshPortSpinBox->value();
        settings.username = m_sshUsernameEdit->text();
        settings.password = m_sshPasswordEdit->text();
        settings.useSshKey = m_useSshKeyCheckBox->isChecked();
        settings.sshKeyPath = m_sshKeyPathEdit->text();

        // Port Warning
        if (settings.port == 23) {
            QMessageBox::warning(nullptr, "Port Warning",
                "You have selected SSH connection type, but the port is set to 23 (default Telnet port).\n\n"
                "SSH default port is 22.\n\n"
                "If the target service is Telnet, please change the connection type to Telnet.\n"
                "If the target service is SSH, please change the port to 22.");
        }
    } else if (type == Rlogin) {
        settings.host = m_rloginHostEdit->text();
        settings.port = m_rloginPortSpinBox->value();
        settings.username = m_rloginUsernameEdit->text();
        settings.password = m_rloginPasswordEdit->text();
    } else if (type == SFTP) {
        settings.host = m_sftpHostEdit->text();
        settings.port = m_sftpPortSpinBox->value();
        settings.username = m_sftpUsernameEdit->text();
        settings.password = m_sftpPasswordEdit->text();
        settings.useSshKey = m_useSftpKeyCheckBox->isChecked();
        settings.sshKeyPath = m_sftpKeyPathEdit->text();
    } else if (type == FTP) {
        settings.host = m_ftpHostEdit->text();
        settings.port = m_ftpPortSpinBox->value();
        settings.username = m_ftpUsernameEdit->text();
        settings.password = m_ftpPasswordEdit->text();
    } else if (type == TFTP) {
        settings.host = m_tftpHostEdit->text();
        settings.port = m_tftpPortSpinBox->value();
    }
    
    return settings;
}
