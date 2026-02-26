#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QSerialPort>
#include <QSerialPortInfo>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QGroupBox;
class QStackedWidget;
class QFormLayout;

class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    enum ConnectionType {
        Serial,
        Telnet,
        SSH
    };

    struct ConnectionSettings {
        QString sessionName;
        ConnectionType type;
        
        // Serial settings
        QString portName;
        qint32 baudRate;
        QSerialPort::DataBits dataBits;
        QSerialPort::Parity parity;
        QSerialPort::StopBits stopBits;
        QSerialPort::FlowControl flowControl;
        
        // Telnet/SSH settings
        QString host;
        quint16 port;
        QString username;
        QString password;
        bool useSshKey;
        QString sshKeyPath;
    };

    explicit ConnectionDialog(QWidget *parent = nullptr);
    ~ConnectionDialog();

    ConnectionSettings getSettings() const;

private slots:
    void onConnectionTypeChanged(int index);
    void refreshSerialPorts();
    void browseSshKey();
    void onSerialPortChanged(int index);
    void onTelnetHostChanged(const QString &text);
    void onSshHostChanged(const QString &text);

private:
    void setupUi();
    void createSerialSettingsWidget();
    void createTelnetSettingsWidget();
    void createSshSettingsWidget();

    QLineEdit *m_sessionNameEdit;
    QComboBox *m_typeComboBox;
    
    QStackedWidget *m_settingsStack;
    
    // Serial settings widgets
    QComboBox *m_portComboBox;
    QComboBox *m_baudRateComboBox;
    QComboBox *m_dataBitsComboBox;
    QComboBox *m_parityComboBox;
    QComboBox *m_stopBitsComboBox;
    QComboBox *m_flowControlComboBox;
    
    // Telnet settings widgets
    QLineEdit *m_telnetHostEdit;
    QSpinBox *m_telnetPortSpinBox;
    QLineEdit *m_telnetUsernameEdit;
    QLineEdit *m_telnetPasswordEdit;
    
    // SSH settings widgets
    QLineEdit *m_sshHostEdit;
    QSpinBox *m_sshPortSpinBox;
    QLineEdit *m_sshUsernameEdit;
    QLineEdit *m_sshPasswordEdit;
    QCheckBox *m_useSshKeyCheckBox;
    QLineEdit *m_sshKeyPathEdit;
    QPushButton *m_browseKeyButton;
    
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    
    ConnectionSettings m_currentSettings;
};

#endif // CONNECTIONDIALOG_H