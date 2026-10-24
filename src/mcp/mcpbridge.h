#ifndef MCPBRIDGE_H
#define MCPBRIDGE_H

#include <QByteArray>
#include <QFile>
#include <QObject>

class QLocalSocket;
class QThread;

// ============================================================================
// MCP stdio 桥接器（NTerminal.exe --mcp-stdio）
//
// 面向只支持 stdio 的 MCP 客户端（Claude Desktop / Claude Code）：
//   stdin/stdout <--换行分隔 JSON-RPC--> 本桥 <--本地命名管道--> GUI 内的 McpServer
//
// 桥本身不解析 MCP 消息，只做字节转发；MCP 工具与权限逻辑都在 GUI 进程内，
// 因此可以附加到正在运行的 NTerminal，而不是再启动一个实例。
// ============================================================================

class McpBridge : public QObject
{
    Q_OBJECT

public:
    explicit McpBridge(QObject *parent = nullptr);
    ~McpBridge() override;

    // 连接正在运行的 GUI 并进入事件循环；返回进程退出码
    int run();

private:
    bool readEndpoint(QString *pipe, QString *error) const;
    void writeStdout(const QByteArray &data);
    void forwardLine(const QByteArray &line);
    void fail(const QString &message);

    QLocalSocket *m_socket = nullptr;
    QThread *m_reader = nullptr;
    QFile m_stdout;
    bool m_finished = false;
    bool m_stdinClosed = false;
};

#endif // MCPBRIDGE_H
