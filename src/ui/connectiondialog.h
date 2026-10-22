#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QSerialPort>
#include <QSerialPortInfo>
#include "common/connectionsettings.h"

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QGroupBox;
class QStackedWidget;
class QFormLayout;

// ============================================================================
// 连接配置对话框（纯 UI 层）
// 输入/编辑 Common::ConnectionSettings，不定义自己的数据结构。
// ============================================================================

class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(QWidget *parent = nullptr);
    ~ConnectionDialog();

    // 返回用户配置（Common::ConnectionSettings 为全项目唯一定义）
    Common::ConnectionSettings getSettings() const;

private slots:
    void onConnectionTypeChanged(int index);
    void refreshSerialPorts();
    void browseSshKey();
    void onSerialPortChanged(int index);
    void onTelnetHostChanged(const QString &text);
    void onSshHostChanged(const QString &text);
    void onRloginHostChanged(const QString &text);
    void onSftpHostChanged(const QString &text);
    void onFtpHostChanged(const QString &text);
    void onTftpHostChanged(const QString &text);

private:
    void setupUi();
    void createSerialSettingsWidget();
    void createTelnetSettingsWidget();
    void createSshSettingsWidget();
    void createRloginSettingsWidget();
    void createSftpSettingsWidget();
    void createFtpSettingsWidget();
    void createTftpSettingsWidget();

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

    // Rlogin settings widgets
    QLineEdit *m_rloginHostEdit;
    QSpinBox *m_rloginPortSpinBox;
    QLineEdit *m_rloginUsernameEdit;
    QLineEdit *m_rloginPasswordEdit;

    // SFTP settings widgets
    QLineEdit *m_sftpHostEdit;
    QSpinBox *m_sftpPortSpinBox;
    QLineEdit *m_sftpUsernameEdit;
    QLineEdit *m_sftpPasswordEdit;
    QCheckBox *m_useSftpKeyCheckBox;
    QLineEdit *m_sftpKeyPathEdit;
    QPushButton *m_browseSftpKeyButton;

    // FTP settings widgets
    QLineEdit *m_ftpHostEdit;
    QSpinBox *m_ftpPortSpinBox;
    QLineEdit *m_ftpUsernameEdit;
    QLineEdit *m_ftpPasswordEdit;

    // TFTP settings widgets
    QLineEdit *m_tftpHostEdit;
    QSpinBox *m_tftpPortSpinBox;
    
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    
    Common::ConnectionSettings m_currentSettings;
};

#endif // CONNECTIONDIALOG_H