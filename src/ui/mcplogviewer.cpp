#include "ui/mcplogviewer.h"

#include "mcp/mcpserver.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

McpLogViewer::McpLogViewer(McpServer *server, QWidget *parent)
    : QDialog(parent)
    , m_server(server)
{
    setWindowTitle(tr("AI 操作日志"));
    setModal(true);
    resize(760, 520);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    QLabel *title = new QLabel(tr("AI 操作日志"), this);
    title->setProperty("class", "sectionTitle");
    layout->addWidget(title);

    QLabel *hint = new QLabel(
        m_server ? tr("记录每次 MCP 工具调用（JSON Lines）：%1")
                       .arg(m_server->auditLogPath())
                 : QString(),
        this);
    hint->setProperty("class", "hint");
    hint->setWordWrap(true);
    hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(hint);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_log->setPlaceholderText(tr("暂无 AI 操作记录"));
    m_log->setObjectName(QStringLiteral("mcpAuditLog"));
    layout->addWidget(m_log, 1);

    QHBoxLayout *buttons = new QHBoxLayout;
    QPushButton *refreshButton = new QPushButton(tr("刷新"), this);
    refreshButton->setProperty("class", "tonal");
    QPushButton *clearButton = new QPushButton(tr("清空日志"), this);
    clearButton->setProperty("class", "danger");
    QPushButton *closeButton = new QPushButton(tr("关闭"), this);
    buttons->addWidget(refreshButton);
    buttons->addWidget(clearButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(refreshButton, &QPushButton::clicked, this, &McpLogViewer::refresh);
    connect(clearButton, &QPushButton::clicked, this, &McpLogViewer::clearLog);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    if (m_server) {
        connect(m_server, &McpServer::auditLogged, this,
                [this](const QString &line) {
                    m_log->appendPlainText(line);
                });
    }

    refresh();
}

void McpLogViewer::refresh()
{
    if (!m_server) {
        return;
    }
    m_log->setPlainText(QString::fromUtf8(m_server->auditLog()));
}

void McpLogViewer::clearLog()
{
    if (!m_server) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("AI 操作日志"), tr("确定清空全部 AI 操作日志吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_server->clearAuditLog();
    m_log->clear();
}
