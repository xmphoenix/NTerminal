#ifndef MCPLOGVIEWER_H
#define MCPLOGVIEWER_H

#include <QDialog>

class McpServer;
class QPlainTextEdit;

// ============================================================================
// AI 操作审计日志查看器（JSON Lines）
// ============================================================================
class McpLogViewer : public QDialog
{
    Q_OBJECT

public:
    explicit McpLogViewer(McpServer *server, QWidget *parent = nullptr);

private:
    void refresh();
    void clearLog();

    McpServer *m_server = nullptr;
    QPlainTextEdit *m_log = nullptr;
};

#endif // MCPLOGVIEWER_H
