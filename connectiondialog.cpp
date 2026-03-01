#include "connectiondialog.h"
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
    m_typeComboBox->addItem("Serial", Serial);
    m_typeComboBox->addItem("Telnet", Telnet);
    m_typeComboBox->addItem("SSH", SSH);
    formLayout->addRow("Connection Type:", m_typeComboBox);

    mainLayout->addLayout(formLayout);

    // Settings stack
    m_settingsStack = new QStackedWidget();

    // Create settings widgets
    createSerialSettingsWidget();
    createTelnetSettingsWidget();
    createSshSettingsWidget();

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

ConnectionDialog::ConnectionSettings ConnectionDialog::getSettings() const
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
    }
    
    return settings;
}
