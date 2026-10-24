#ifndef MCPSETTINGSDIALOG_H
#define MCPSETTINGSDIALOG_H

#include <QDialog>

class McpServer;
class QCheckBox;
class QRadioButton;
class QLabel;

// ============================================================================
// MCP 设置：启用服务、AI 控制权限与客户端配置复制
// ============================================================================
class McpSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit McpSettingsDialog(McpServer *server, QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    void setupUi();
    void copyDesktopConfig();
    void copyClaudeCodeCommand();

    McpServer *m_server = nullptr;
    QCheckBox *m_enableCheck = nullptr;
    QRadioButton *m_readOnlyRadio = nullptr;
    QRadioButton *m_confirmRadio = nullptr;
    QRadioButton *m_allowRadio = nullptr;
    QLabel *m_endpointLabel = nullptr;
};

#endif // MCPSETTINGSDIALOG_H
