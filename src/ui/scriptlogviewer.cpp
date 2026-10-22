#include "ui/scriptlogviewer.h"
#include <QVBoxLayout>
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

    // 日志文本区域
    m_logText = new QTextEdit(this);
    m_logText->setReadOnly(true);
    m_logText->setFont(QFont("Consolas", 9));
    mainLayout->addWidget(m_logText);

    // 按钮
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    m_clearButton = new QPushButton(tr("清空日志"), this);
    connect(m_clearButton, &QPushButton::clicked,
            this, &ScriptLogViewer::clearLog);
    buttonLayout->addWidget(m_clearButton);

    m_exportButton = new QPushButton(tr("导出日志"), this);
    connect(m_exportButton, &QPushButton::clicked,
            this, &ScriptLogViewer::exportLog);
    buttonLayout->addWidget(m_exportButton);

    buttonLayout->addStretch();

    m_closeButton = new QPushButton(tr("关闭"), this);
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
    out << "=== NTerminal Script Log ===" << "\n";
    out << "Script: " << m_scriptName << "\n";
    out << "Export Time: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
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
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString color = getLevelColor(level);

    QString html = QString("<span style='color: gray;'>[%1]</span> "
                           "<span style='color: %2; font-weight: bold;'>[%3]</span> "
                           "<span>%4</span>")
                       .arg(timestamp)
                       .arg(color)
                       .arg(level.toUpper())
                       .arg(message.toHtmlEscaped());

    return html;
}

QString ScriptLogViewer::getLevelColor(const QString &level) const
{
    QString levelLower = level.toLower();

    if (levelLower == "error") {
        return "red";
    } else if (levelLower == "warning") {
        return "orange";
    } else if (levelLower == "info") {
        return "blue";
    } else if (levelLower == "debug") {
        return "gray";
    } else {
        return "black";
    }
}
