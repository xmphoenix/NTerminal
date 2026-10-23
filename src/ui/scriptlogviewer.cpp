#include "ui/scriptlogviewer.h"
#include "ui/iconprovider.h"
#include "theme/thememanager.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QDateTime>
#include <QMessageBox>
#include <QScrollBar>

ScriptLogViewer::ScriptLogViewer(QWidget *parent)
    : QDialog(parent)
    , m_logText(nullptr)
    , m_clearButton(nullptr)
    , m_exportButton(nullptr)
    , m_closeButton(nullptr)
{
    setWindowTitle(tr("脚本日志"));
    setMinimumWidth(600);
    setMinimumHeight(400);

    setupUI();
}

ScriptLogViewer::~ScriptLogViewer()
{
}

void ScriptLogViewer::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(14);

    // 头部：标题 + 说明
    QLabel *titleLabel = new QLabel(tr("脚本日志"), this);
    titleLabel->setProperty("class", "sectionTitle");
    QLabel *hintLabel = new QLabel(tr("运行输出按级别着色，可一键清空或导出为文本文件"), this);
    hintLabel->setProperty("class", "hint");
    hintLabel->setWordWrap(true);
    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(hintLabel);

    // 日志文本区域
    m_logText = new QTextEdit(this);
    m_logText->setReadOnly(true);
    QFont monoFont = m_logText->font();
    monoFont.setFamilies({"JetBrains Mono", "Consolas", "Cascadia Mono"});
    monoFont.setPixelSize(12);
    m_logText->setFont(monoFont);
    mainLayout->addWidget(m_logText, 1);

    // 按钮
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    m_clearButton = new QPushButton(tr("清空日志"), this);
    m_clearButton->setProperty("class", "ghost");
    connect(m_clearButton, &QPushButton::clicked,
            this, &ScriptLogViewer::clearLog);
    buttonLayout->addWidget(m_clearButton);

    m_exportButton = new QPushButton(tr("导出日志"), this);
    m_exportButton->setIcon(IconProvider::themedIcon("download"));
    connect(m_exportButton, &QPushButton::clicked,
            this, &ScriptLogViewer::exportLog);
    buttonLayout->addWidget(m_exportButton);

    buttonLayout->addStretch();

    m_closeButton = new QPushButton(tr("关闭"), this);
    m_closeButton->setProperty("class", "primary");
    m_closeButton->setDefault(true);
    connect(m_closeButton, &QPushButton::clicked,
            this, &QDialog::accept);
    buttonLayout->addWidget(m_closeButton);

    mainLayout->addLayout(buttonLayout);
}

void ScriptLogViewer::addLogMessage(const QString &level, const QString &message)
{
    m_logMessages.append(qMakePair(level, message));

    QString formattedMessage = formatLogMessage(level, message);
    m_logText->append(formattedMessage);

    // 自动滚动到底部
    QScrollBar *scrollBar = m_logText->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void ScriptLogViewer::clearLog()
{
    m_logMessages.clear();
    m_logText->clear();
}

void ScriptLogViewer::setScriptName(const QString &name)
{
    m_scriptName = name;
    setWindowTitle(tr("脚本日志 - %1").arg(name));
}

void ScriptLogViewer::exportLog()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("导出日志"),
        QString("script_log_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        tr("文本文件 (*.txt);;所有文件 (*)")
    );

    if (fileName.isEmpty()) {
        return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("导出失败"),
                             tr("无法写入文件: %1").arg(fileName));
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    // 写入头部信息
    out << tr("=== NTerminal 脚本日志 ===") << "\n";
    out << tr("脚本: ") << m_scriptName << "\n";
    out << tr("导出时间: ") << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
    out << "============================" << "\n\n";

    // 写入日志消息
    for (const auto &logEntry : m_logMessages) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        out << "[" << timestamp << "] ";
        out << "[" << logEntry.first.toUpper() << "] ";
        out << logEntry.second << "\n";
    }

    file.close();

    QMessageBox::information(this, tr("导出成功"),
                             tr("日志已导出到: %1").arg(fileName));
}

QString ScriptLogViewer::formatLogMessage(const QString &level, const QString &message) const
{
    const AppTheme theme = ThemeManager::instance()->currentAppTheme();
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString color = getLevelColor(level);

    QString html = QStringLiteral("<span style=\"color:%1;\">[%2]</span> "
                                  "<span style=\"color:%3; font-weight: bold;\">[%4]</span> "
                                  "<span>%5</span>")
                       .arg(theme.textFaint.name(),
                            timestamp,
                            color,
                            level.toUpper(),
                            message.toHtmlEscaped());

    return html;
}

QString ScriptLogViewer::getLevelColor(const QString &level) const
{
    const AppTheme theme = ThemeManager::instance()->currentAppTheme();
    const QString levelLower = level.toLower();

    if (levelLower == "error") {
        return theme.danger.name();
    } else if (levelLower == "warning") {
        return theme.warning.name();
    } else if (levelLower == "info") {
        return theme.accent.name();
    } else if (levelLower == "debug") {
        return theme.textDim.name();
    } else {
        return theme.windowText.name();
    }
}
