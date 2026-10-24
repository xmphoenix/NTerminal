#include "ui/mcpsettingsdialog.h"

#include "mcp/mcpserver.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

McpSettingsDialog::McpSettingsDialog(McpServer *server, QWidget *parent)
    : QDialog(parent)
    , m_server(server)
{
    setupUi();
    setWindowTitle(tr("MCP 设置"));
    setModal(true);
    resize(520, 460);

    if (m_server) {
        m_enableCheck->setChecked(m_server->isRunning());
        switch (m_server->policy()) {
        case McpServer::ControlPolicy::ReadOnly:
            m_readOnlyRadio->setChecked(true);
            break;
        case McpServer::ControlPolicy::Allow:
            m_allowRadio->setChecked(true);
            break;
        case McpServer::ControlPolicy::Confirm:
        default:
            m_confirmRadio->setChecked(true);
            break;
        }
        m_endpointLabel->setText(
            tr("端点文件：%1").arg(m_server->endpointPath()));
    }
}

void McpSettingsDialog::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(12);

    QLabel *title = new QLabel(tr("MCP / AI 控制"), this);
    title->setProperty("class", "sectionTitle");
    layout->addWidget(title);

    QLabel *hint = new QLabel(
        tr("AI 客户端（Claude Desktop、Claude Code 等）通过 MCP 协议读取终端内容、"
           "发送命令。所有调用都会写入审计日志。"),
        this);
    hint->setProperty("class", "hint");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    QGroupBox *serviceGroup = new QGroupBox(tr("服务"), this);
    QVBoxLayout *serviceLayout = new QVBoxLayout(serviceGroup);
    serviceLayout->setSpacing(8);

    m_enableCheck = new QCheckBox(tr("启用 MCP 服务"), serviceGroup);
    serviceLayout->addWidget(m_enableCheck);

    m_endpointLabel = new QLabel(serviceGroup);
    m_endpointLabel->setProperty("class", "hint");
    m_endpointLabel->setWordWrap(true);
    m_endpointLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    serviceLayout->addWidget(m_endpointLabel);

    QLabel *configHint = new QLabel(
        tr("在「MCP」菜单中可复制 Claude Desktop 配置或 Claude Code 命令；"
           "配置完成后由 AI 客户端启动 NTerminal.exe --mcp-stdio。"),
        serviceGroup);
    configHint->setProperty("class", "hint");
    configHint->setWordWrap(true);
    serviceLayout->addWidget(configHint);

    QHBoxLayout *copyLayout = new QHBoxLayout;
    QPushButton *desktopButton = new QPushButton(
        tr("复制 Claude Desktop 配置"), serviceGroup);
    desktopButton->setProperty("class", "tonal");
    QPushButton *codeButton = new QPushButton(
        tr("复制 Claude Code 命令"), serviceGroup);
    codeButton->setProperty("class", "tonal");
    copyLayout->addWidget(desktopButton);
    copyLayout->addWidget(codeButton);
    copyLayout->addStretch();
    serviceLayout->addLayout(copyLayout);
    layout->addWidget(serviceGroup);

    QGroupBox *policyGroup = new QGroupBox(tr("AI 权限"), this);
    QVBoxLayout *policyLayout = new QVBoxLayout(policyGroup);
    policyLayout->setSpacing(6);

    m_readOnlyRadio = new QRadioButton(
        tr("只读：AI 只能查看会话和终端内容"), policyGroup);
    m_confirmRadio = new QRadioButton(
        tr("每次确认：写入或关闭会话前弹窗询问"), policyGroup);
    m_allowRadio = new QRadioButton(
        tr("允许控制：写入不再询问（适合受信任的自动化）"), policyGroup);
    policyLayout->addWidget(m_readOnlyRadio);
    policyLayout->addWidget(m_confirmRadio);
    policyLayout->addWidget(m_allowRadio);
    layout->addWidget(policyGroup);

    QGroupBox *toolsGroup = new QGroupBox(tr("提供的工具"), this);
    QVBoxLayout *toolsLayout = new QVBoxLayout(toolsGroup);
    QLabel *tools = new QLabel(
        tr("会话管理、读屏/发送/按键/等待输出/执行命令、"
           "文件传输（ZMODEM/YMODEM/XMODEM/Kermit/ASCII）、"
           "内置网络服务（TFTP/FTP/HTTP/Telnet/NFS）、远端资源统计。"
           "完整参数由 MCP 客户端自动获取。"),
        toolsGroup);
    tools->setProperty("class", "hint");
    tools->setWordWrap(true);
    toolsLayout->addWidget(tools);
    layout->addWidget(toolsGroup);

    layout->addStretch();

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &McpSettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &McpSettingsDialog::reject);
    layout->addWidget(buttons);

    connect(desktopButton, &QPushButton::clicked,
            this, &McpSettingsDialog::copyDesktopConfig);
    connect(codeButton, &QPushButton::clicked,
            this, &McpSettingsDialog::copyClaudeCodeCommand);
}

void McpSettingsDialog::copyDesktopConfig()
{
    if (!m_server) {
        return;
    }
    QGuiApplication::clipboard()->setText(m_server->claudeDesktopConfig());
    QMessageBox::information(this, tr("MCP"),
                             tr("Claude Desktop 配置已复制到剪贴板。"));
}

void McpSettingsDialog::copyClaudeCodeCommand()
{
    if (!m_server) {
        return;
    }
    QGuiApplication::clipboard()->setText(m_server->claudeCodeCommand());
    QMessageBox::information(this, tr("MCP"),
                             tr("Claude Code 命令已复制到剪贴板。"));
}

void McpSettingsDialog::accept()
{
    if (!m_server) {
        QDialog::accept();
        return;
    }

    const bool enable = m_enableCheck->isChecked();
    if (enable && !m_server->isRunning()) {
        if (!m_server->start()) {
            QMessageBox::warning(
                this, tr("MCP"),
                tr("无法启动 MCP 服务：%1").arg(m_server->lastError()));
            return;
        }
    } else if (!enable && m_server->isRunning()) {
        m_server->stop();
    }

    if (m_allowRadio->isChecked()) {
        m_server->setPolicy(McpServer::ControlPolicy::Allow);
    } else if (m_readOnlyRadio->isChecked()) {
        m_server->setPolicy(McpServer::ControlPolicy::ReadOnly);
    } else {
        m_server->setPolicy(McpServer::ControlPolicy::Confirm);
    }
    m_server->setEnabled(enable);

    QDialog::accept();
}
