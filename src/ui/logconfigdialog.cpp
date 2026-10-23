#include "ui/logconfigdialog.h"
#include "ui/iconprovider.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QSettings>
#include <QDir>
#include <QLabel>
#include <QDateTime>

LogConfigDialog::LogConfigDialog(QWidget *parent)
    : QDialog(parent)
    , m_logFileNameEdit(new QLineEdit(this))
    , m_browseButton(new QPushButton(tr("..."), this))
    , m_overwriteCheck(new QCheckBox(this))
    , m_startLoggingOnConnectCheck(new QCheckBox(this))
    , m_includeTerminalCodesCheck(new QCheckBox(this))
    , m_encodingCombo(new QComboBox(this))
    , m_timestampGroup(new QGroupBox(tr("时间戳"), this))
    , m_enableTimestampCheck(new QCheckBox(this))
    , m_timestampFormatEdit(new QLineEdit(this))
    , m_timestampPreviewLabel(new QLabel(this))
    , m_formatHelpLabel(new QLabel(this))
    , m_okButton(new QPushButton(tr("确定"), this))
    , m_cancelButton(new QPushButton(tr("取消"), this))
    , m_applyButton(new QPushButton(tr("应用"), this))
    , m_resetButton(new QPushButton(tr("恢复默认值"), this))
{
    setupUi();
    loadSettings();
}

LogConfigDialog::~LogConfigDialog()
{
}

void LogConfigDialog::setupUi()
{
    setWindowTitle(tr("日志设置"));
    setMinimumWidth(620);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(10);

    // 头部：标题 + 说明
    QLabel *titleLabel = new QLabel(tr("日志设置"));
    titleLabel->setProperty("class", "sectionTitle");
    QLabel *hintLabel = new QLabel(tr("配置会话日志的文件名、编码与时间戳格式"));
    hintLabel->setProperty("class", "hint");
    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(hintLabel);

    // ===== 日志文件组 =====
    QGroupBox *logFileGroup = new QGroupBox(tr("日志文件"), this);
    QVBoxLayout *logFileLayout = new QVBoxLayout(logFileGroup);
    logFileLayout->setContentsMargins(0, 0, 0, 0);
    logFileLayout->setSpacing(10);

    // 文件路径行
    QHBoxLayout *fileNameLayout = new QHBoxLayout();
    fileNameLayout->setSpacing(8);
    fileNameLayout->addWidget(new QLabel(tr("文件路径 (F):"), this));
    m_logFileNameEdit->setPlaceholderText("%c_%n_%Y-%m-%d_%t.log");
    m_logFileNameEdit->setToolTip(tr("默认：%c_%n_%Y-%m-%d_%t.log\n"
                                      "%c:会话类型，%n:会话名称，%Y-%m-%d:日期，%t:时_分_秒\n"
                                      "%h:小时，%M:分钟，%s:秒，%N:毫秒"));
    fileNameLayout->addWidget(m_logFileNameEdit, 1);
    fileNameLayout->addWidget(m_browseButton);
    logFileLayout->addLayout(fileNameLayout);

    // 文件已存在时覆盖
    m_overwriteCheck->setText(tr("文件已存在时覆盖 (O)"));
    logFileLayout->addWidget(m_overwriteCheck);

    // 连接时开始日志记录
    m_startLoggingOnConnectCheck->setText(tr("连接时开始日志记录 (C)"));
    m_startLoggingOnConnectCheck->setChecked(true);  // 默认选中
    logFileLayout->addWidget(m_startLoggingOnConnectCheck);

    // 包括终端代码
    m_includeTerminalCodesCheck->setText(tr("包括终端代码 (T)"));
    logFileLayout->addWidget(m_includeTerminalCodesCheck);

    // 编码
    QHBoxLayout *encodingLayout = new QHBoxLayout();
    encodingLayout->setSpacing(8);
    encodingLayout->addWidget(new QLabel(tr("编码 (E):"), this));
    m_encodingCombo->addItem("Unicode (UTF-16 LE)", "utf-16le");
    m_encodingCombo->addItem("Unicode (UTF-16 BE)", "utf-16be");
    m_encodingCombo->addItem("UTF-8", "utf-8");
    m_encodingCombo->addItem("GB18030", "gb18030");
    m_encodingCombo->addItem("GBK", "gbk");
    m_encodingCombo->addItem("GB2312", "gb2312");
    m_encodingCombo->addItem("Big5", "big5");
    m_encodingCombo->addItem("ISO-8859-1", "iso-8859-1");
    m_encodingCombo->addItem("Windows-1252", "windows-1252");
    encodingLayout->addWidget(m_encodingCombo);
    encodingLayout->addStretch();
    logFileLayout->addLayout(encodingLayout);

    mainLayout->addWidget(logFileGroup);

    // ===== 时间戳组 =====
    QVBoxLayout *timestampLayout = new QVBoxLayout(m_timestampGroup);
    timestampLayout->setContentsMargins(0, 0, 0, 0);
    timestampLayout->setSpacing(10);

    // 写入日志文件
    m_enableTimestampCheck->setText(tr("写入日志文件 (W)"));
    timestampLayout->addWidget(m_enableTimestampCheck);

    // 格式
    QHBoxLayout *formatLayout = new QHBoxLayout();
    formatLayout->setSpacing(8);
    formatLayout->addWidget(new QLabel(tr("格式 (M):"), this));
    m_timestampFormatEdit->setText("[%Y-%m-%d %h:%M:%s]");
    m_timestampFormatEdit->setPlaceholderText("[%Y-%m-%d %h:%M:%s.%N]");
    connect(m_timestampFormatEdit, &QLineEdit::textChanged, this, &LogConfigDialog::onTimestampFormatChanged);
    formatLayout->addWidget(m_timestampFormatEdit, 1);
    timestampLayout->addLayout(formatLayout);

    // 预览
    QHBoxLayout *previewLayout = new QHBoxLayout();
    previewLayout->setSpacing(8);
    previewLayout->addWidget(new QLabel(tr("预览 (V):"), this));
    m_timestampPreviewLabel->setFrameShape(QFrame::StyledPanel);
    m_timestampPreviewLabel->setMinimumHeight(24);
    m_timestampPreviewLabel->setText(expandFormatPattern(m_timestampFormatEdit->text()));
    previewLayout->addWidget(m_timestampPreviewLabel, 1);
    timestampLayout->addLayout(previewLayout);

    mainLayout->addWidget(m_timestampGroup);

    // ===== 底部格式说明 =====
    m_formatHelpLabel->setText(
        tr("%c: 会话类型 (ssh/telnet/serial/ftp/sftp/tftp/rlogin)，%n: 会话名称，%Y: 年，%m: 月，%d: 日，"
           "%h: 小时，%M: 分钟，%s: 秒，%N: 毫秒，%t: 时_分_秒，%a: 语言环境")
    );
    m_formatHelpLabel->setWordWrap(true);
    m_formatHelpLabel->setProperty("class", "hint");
    mainLayout->addWidget(m_formatHelpLabel);

    // ===== 按钮：重置靠左，其余次操作在左、主操作在右 =====
    m_resetButton->setIcon(IconProvider::themedIcon("reset"));
    m_resetButton->setProperty("class", "ghost");
    m_applyButton->setProperty("class", "tonal");
    m_okButton->setProperty("class", "primary");
    m_okButton->setDefault(true);
    m_cancelButton->setProperty("class", "ghost");

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    buttonLayout->addWidget(m_resetButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_applyButton);
    buttonLayout->addWidget(m_okButton);
    mainLayout->addLayout(buttonLayout);

    // 连接信号
    connect(m_browseButton, &QPushButton::clicked, this, &LogConfigDialog::onBrowseClicked);
    connect(m_okButton, &QPushButton::clicked, this, &LogConfigDialog::onOk);
    connect(m_cancelButton, &QPushButton::clicked, this, &LogConfigDialog::onCancel);
    connect(m_applyButton, &QPushButton::clicked, this, &LogConfigDialog::onApply);
    connect(m_resetButton, &QPushButton::clicked, this, &LogConfigDialog::onResetToDefaults);
}

void LogConfigDialog::loadSettings()
{
    QSettings settings("NTerminal", "NTerminal");

    // 日志文件设置
    QString fileNamePattern = settings.value("log/fileNamePattern", "%c_%n_%Y-%m-%d_%t.log").toString();
    bool overwrite = settings.value("log/overwrite", false).toBool();
    bool startOnConnect = settings.value("log/startOnConnect", true).toBool();  // 默认 true
    bool includeTerminalCodes = settings.value("log/includeTerminalCodes", false).toBool();
    QString encoding = settings.value("log/encoding", "utf-8").toString();

    // 时间戳设置
    bool enableTimestamp = settings.value("log/enableTimestamp", true).toBool();
    QString timestampFormat = settings.value("log/timestampFormat", "[%Y-%m-%d %h:%M:%s]").toString();

    // 应用到 UI
    m_logFileNameEdit->setText(fileNamePattern);
    m_overwriteCheck->setChecked(overwrite);
    m_startLoggingOnConnectCheck->setChecked(startOnConnect);
    m_includeTerminalCodesCheck->setChecked(includeTerminalCodes);

    // 设置编码
    for (int i = 0; i < m_encodingCombo->count(); ++i) {
        if (m_encodingCombo->itemData(i).toString() == encoding) {
            m_encodingCombo->setCurrentIndex(i);
            break;
        }
    }

    m_enableTimestampCheck->setChecked(enableTimestamp);
    m_timestampFormatEdit->setText(timestampFormat);
    updateTimestampPreview();
}

void LogConfigDialog::saveSettings()
{
    QSettings settings("NTerminal", "NTerminal");

    // 日志文件设置
    settings.setValue("log/fileNamePattern", m_logFileNameEdit->text());
    settings.setValue("log/overwrite", m_overwriteCheck->isChecked());
    settings.setValue("log/startOnConnect", m_startLoggingOnConnectCheck->isChecked());
    settings.setValue("log/includeTerminalCodes", m_includeTerminalCodesCheck->isChecked());
    settings.setValue("log/encoding", m_encodingCombo->currentData().toString());

    // 时间戳设置
    settings.setValue("log/enableTimestamp", m_enableTimestampCheck->isChecked());
    settings.setValue("log/timestampFormat", m_timestampFormatEdit->text());

    settings.sync();
}

void LogConfigDialog::onBrowseClicked()
{
    QString currentPath = m_logFileNameEdit->text();
    if (currentPath.isEmpty()) {
        currentPath = QDir::homePath() + "/.nterminal/logs";
    }

    QString dir = QFileDialog::getExistingDirectory(this,
        tr("选择日志目录"),
        currentPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        // 只保存目录，文件名模式保持不变
        QString currentPattern = m_logFileNameEdit->text();
        QString fileName = currentPattern;
        
        // 提取文件名部分（如果有路径）
        int lastSlash = currentPattern.lastIndexOf('/');
        if (lastSlash == -1) {
            lastSlash = currentPattern.lastIndexOf('\\');
        }
        if (lastSlash >= 0) {
            fileName = currentPattern.mid(lastSlash + 1);
        }
        
        m_logFileNameEdit->setText(dir + "/" + fileName);
    }
}

void LogConfigDialog::onOk()
{
    saveSettings();
    accept();
}

void LogConfigDialog::onCancel()
{
    reject();
}

void LogConfigDialog::onApply()
{
    saveSettings();
}

void LogConfigDialog::onResetToDefaults()
{
    // 重置为默认值
    m_logFileNameEdit->setText("%c_%n_%Y-%m-%d_%t.log");
    m_overwriteCheck->setChecked(false);
    m_startLoggingOnConnectCheck->setChecked(true);
    m_includeTerminalCodesCheck->setChecked(false);
    m_encodingCombo->setCurrentText("UTF-8");
    m_enableTimestampCheck->setChecked(true);
    m_timestampFormatEdit->setText("[%Y-%m-%d %h:%M:%s]");
    updateTimestampPreview();
}

void LogConfigDialog::onTimestampFormatChanged(const QString &/*format*/)
{
    updateTimestampPreview();
}

void LogConfigDialog::updateTimestampPreview()
{
    QString format = m_timestampFormatEdit->text();
    m_timestampPreviewLabel->setText(expandFormatPattern(format));
}

QString LogConfigDialog::expandFormatPattern(const QString &pattern) const
{
    QString result = pattern;
    QDateTime now = QDateTime::currentDateTime();

    // 替换时间相关变量（使用当前时间）
    result.replace("%Y", now.toString("yyyy"));
    result.replace("%m", now.toString("MM"));
    result.replace("%d", now.toString("dd"));
    result.replace("%h", now.toString("HH"));  // %h 表示小时
    result.replace("%H", now.toString("HH"));  // %H 也表示小时
    result.replace("%M", now.toString("mm"));
    result.replace("%s", now.toString("ss"));
    result.replace("%N", now.toString("zzz"));
    result.replace("%a", now.toString("ddd"));

    // %t 代表 时_分_秒
    QString timePart = QString("%1_%2_%3")
        .arg(now.toString("HH"))
        .arg(now.toString("mm"))
        .arg(now.toString("ss"));
    result.replace("%t", timePart);

    // 其他变量使用占位符
    result.replace("%c", tr("ssh"));
    result.replace("%n", tr("SessionName"));

    return result;
}

// Getter 方法
QString LogConfigDialog::logFileNamePattern() const
{
    return m_logFileNameEdit->text();
}

bool LogConfigDialog::overwriteExisting() const
{
    return m_overwriteCheck->isChecked();
}

bool LogConfigDialog::startLoggingOnConnect() const
{
    return m_startLoggingOnConnectCheck->isChecked();
}

bool LogConfigDialog::includeTerminalCodes() const
{
    return m_includeTerminalCodesCheck->isChecked();
}

QString LogConfigDialog::encoding() const
{
    return m_encodingCombo->currentData().toString();
}

bool LogConfigDialog::enableTimestamp() const
{
    return m_enableTimestampCheck->isChecked();
}

QString LogConfigDialog::timestampFormat() const
{
    return m_timestampFormatEdit->text();
}

// Setter 方法
void LogConfigDialog::setLogFileNamePattern(const QString &pattern)
{
    m_logFileNameEdit->setText(pattern);
}

void LogConfigDialog::setOverwriteExisting(bool overwrite)
{
    m_overwriteCheck->setChecked(overwrite);
}

void LogConfigDialog::setStartLoggingOnConnect(bool start)
{
    m_startLoggingOnConnectCheck->setChecked(start);
}

void LogConfigDialog::setIncludeTerminalCodes(bool include)
{
    m_includeTerminalCodesCheck->setChecked(include);
}

void LogConfigDialog::setEncoding(const QString &encoding)
{
    for (int i = 0; i < m_encodingCombo->count(); ++i) {
        if (m_encodingCombo->itemData(i).toString() == encoding) {
            m_encodingCombo->setCurrentIndex(i);
            break;
        }
    }
}

void LogConfigDialog::setEnableTimestamp(bool enabled)
{
    m_enableTimestampCheck->setChecked(enabled);
}

void LogConfigDialog::setTimestampFormat(const QString &format)
{
    m_timestampFormatEdit->setText(format);
}