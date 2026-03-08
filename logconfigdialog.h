#ifndef LOGCONFIGDIALOG_H
#define LOGCONFIGDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>

class LogConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LogConfigDialog(QWidget *parent = nullptr);
    ~LogConfigDialog();

    // 获取配置
    QString logFileNamePattern() const;
    bool overwriteExisting() const;
    bool startLoggingOnConnect() const;
    bool includeTerminalCodes() const;
    QString encoding() const;
    bool enableTimestamp() const;
    QString timestampFormat() const;

    // 设置配置
    void setLogFileNamePattern(const QString &pattern);
    void setOverwriteExisting(bool overwrite);
    void setStartLoggingOnConnect(bool start);
    void setIncludeTerminalCodes(bool include);
    void setEncoding(const QString &encoding);
    void setEnableTimestamp(bool enabled);
    void setTimestampFormat(const QString &format);

private slots:
    void onBrowseClicked();
    void onOk();
    void onCancel();
    void onApply();
    void onTimestampFormatChanged(const QString &format);
    void onResetToDefaults();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    void updateTimestampPreview();
    QString expandFormatPattern(const QString &pattern) const;

    // 日志文件组
    QLineEdit *m_logFileNameEdit;
    QPushButton *m_browseButton;
    QCheckBox *m_overwriteCheck;
    QCheckBox *m_startLoggingOnConnectCheck;
    QCheckBox *m_includeTerminalCodesCheck;
    QComboBox *m_encodingCombo;

    // 时间戳组
    QGroupBox *m_timestampGroup;
    QCheckBox *m_enableTimestampCheck;
    QLineEdit *m_timestampFormatEdit;
    QLabel *m_timestampPreviewLabel;

    // 底部说明
    QLabel *m_formatHelpLabel;

    // 按钮
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    QPushButton *m_applyButton;
    QPushButton *m_resetButton;
};

#endif // LOGCONFIGDIALOG_H