#ifndef MCPSERVER_H
#define MCPSERVER_H

#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPointer>
#include <QRegularExpression>
#include <QStringList>
#include <functional>

#include "terminal/terminalproxy.h"
#include "transfer/filetransfer.h"

class QLocalServer;
class QLocalSocket;
class QTimer;
class SessionManager;
class ServerService;

// ============================================================================
// MCP 服务（模型上下文协议）
//
// GUI 进程内的 MCP Server：通过每用户本地命名管道接受 MCP 客户端连接，
// 把 list_sessions / read_screen / send_text / run_command 等工具直接映射到
// TerminalProxy。NTerminal.exe --mcp-stdio 作为 stdio <-> 本地管道的桥接器，
// 供 Claude Desktop / Claude Code 等只支持 stdio 的客户端使用。
//
// 同时兼容两代 MCP：
//   - 2025-11-25 及更早：initialize 握手（Claude Desktop 当前使用）
//   - 2026-07-28：每个请求携带 _meta 的无状态协议（server/discover）
// ============================================================================

class McpServer : public QObject
{
    Q_OBJECT

public:
    enum class ControlPolicy {
        ReadOnly,   // AI 只能读取终端
        Confirm,    // 写入/关闭前每次询问
        Allow       // 完全允许 AI 控制
    };
    Q_ENUM(ControlPolicy)

    explicit McpServer(QObject *parent = nullptr);
    ~McpServer() override;

    bool start();
    void stop();
    bool isRunning() const { return m_running; }
    QString lastError() const { return m_lastError; }

    // 持久化配置（QSettings：Mcp/Enabled、Mcp/Policy）
    void loadSettings();
    void saveSettings();
    bool enabledInSettings() const { return m_enabled; }
    void setEnabled(bool enabled);
    ControlPolicy policy() const { return m_policy; }
    void setPolicy(ControlPolicy policy);

    // 终端注册表：MainWindow 在创建/关闭标签时调用
    void registerTerminal(const QString &id, const QString &name, TerminalProxy *terminal);
    void unregisterTerminal(const QString &id);

    // 打开/关闭会话由 UI 层实现（需要创建标签页）；错误信息写入 error
    void setOpenSessionHandler(std::function<QString(const QString &, QString *)> handler);
    void setCloseSessionHandler(std::function<bool(const QString &, QString *)> handler);
    void setSessionManager(SessionManager *manager) { m_sessionManager = manager; }

    // 客户端配置文本（MCP 菜单复制到剪贴板）
    QString claudeDesktopConfig() const;
    QString claudeCodeCommand() const;
    QString endpointPath() const;
    QString auditLogPath() const;
    QByteArray auditLog() const;
    void clearAuditLog();

signals:
    void runningChanged(bool running);
    void auditLogged(const QString &line);

private:
    struct Client {
        QLocalSocket *socket = nullptr;
        QByteArray buffer;
        bool legacyInitialized = false;
        QString name;
        QString version;
    };

    struct Session {
        QString id;
        QString name;
        QPointer<TerminalProxy> terminal;
        QByteArray raw;        // 最近的原始输出（用于 wait_for / run_command）
        qint64 totalBytes = 0; // 历史累计字节数（单调递增，用于定位起始偏移）
        QElapsedTimer lastData;
    };

    struct Waiter {
        QPointer<QLocalSocket> socket;
        QJsonValue requestId;
        bool modern = false;
        QString sessionId;
        QString tool;
        QString clientName;
        QString command;            // run_command 的原始命令（用于结果与审计）
        QRegularExpression regex;   // wait_for 的正则 / run_command 的标记
        QString marker;             // run_command 的完成标记
        qint64 startByte = 0;
        QElapsedTimer timer;
        int timeoutMs = 30000;
        QJsonValue progressToken;
        bool idleCompletion = false;
        qint64 lastProgressMs = 0;
    };

    struct TransferJob {
        QString id;
        QString sessionId;
        QPointer<TerminalProxy> terminal;
        QPointer<FileTransfer> transfer;
        int filterToken = 0;
        QString protocol;        // zmodem / ymodem / xmodem / kermit / ascii
        QString direction;       // send / receive
        QStringList paths;
        QStringList log;
        QString result;          // running / success / failed / cancelled
        QString message;
        QDateTime startedAt;
        QDateTime finishedAt;
    };

    // 连接处理
    void onNewConnection();
    void onReadyRead(QLocalSocket *socket);
    void onDisconnected(QLocalSocket *socket);
    void processLine(Client *client, const QByteArray &line);
    bool handleRequest(Client *client, const QJsonObject &request);

    // JSON-RPC / MCP
    void sendJson(QLocalSocket *socket, const QJsonObject &message);
    void sendResponse(Client *client, const QJsonValue &id, const QJsonObject &result,
                      bool modern);
    void sendError(QLocalSocket *socket, const QJsonValue &id, int code,
                   const QString &message);
    QJsonObject initializeResult(const QJsonObject &params);
    QJsonObject discoverResult() const;
    QJsonObject capabilities() const;
    QJsonArray toolDefinitions() const;
    QJsonObject callTool(Client *client, const QJsonValue &requestId,
                         const QJsonObject &meta, const QString &tool,
                         const QJsonObject &args, bool modern, bool *async);
    QJsonObject toolResult(const QString &text, const QJsonObject &structured,
                           bool isError, bool modern) const;

    // 资源
    QJsonArray resourceList() const;
    QJsonArray resourceTemplates() const;
    QJsonObject readResource(const QString &uri, bool *ok, QString *error) const;

    // 文件传输与网络服务工具
    QJsonObject startTransfer(Client *client, const QJsonObject &args,
                              bool modern);
    QJsonObject transferStatus(const QString &id, bool *ok, QString *error) const;
    QJsonObject transferList(bool modern) const;
    QJsonObject serverStatus(ServerService *service) const;
    QJsonObject publicServiceConfig(const QJsonObject &config) const;
    void cancelTransfersForSession(const QString &sessionId);
    void cleanupTransfers();

    // 终端访问
    Session *sessionFor(const QString &id);
    const Session *sessionFor(const QString &id) const;
    QString cleanedOutput(const Session &session, qint64 startByte) const;
    QJsonObject sessionMetadata(const Session &session) const;
    void checkWaiters(const QString &sessionId);
    void tickWaiters();
    void sendProgress(Waiter &waiter);

    // 权限与审计
    bool allowControl(Client *client, const QString &tool, const QString &sessionId,
                      const QString &preview, QString *error);
    void logAudit(const QString &clientName, const QString &tool,
                  const QString &sessionId, const QString &result,
                  const QString &detail, qint64 durationMs);
    static QString toolPreview(const QString &tool, const QJsonObject &args);

    QLocalServer *m_server = nullptr;
    QTimer *m_waiterTimer = nullptr;
    QHash<QLocalSocket *, Client *> m_clients;
    QHash<QString, Session> m_sessions;
    QList<Waiter> m_waiters;
    QList<TransferJob> m_transfers;
    int m_nextTransferId = 1;
    SessionManager *m_sessionManager = nullptr;
    std::function<QString(const QString &, QString *)> m_openSessionHandler;
    std::function<bool(const QString &, QString *)> m_closeSessionHandler;

    QString m_pipeName;
    QString m_lastError;
    bool m_running = false;
    bool m_enabled = true;
    bool m_confirming = false;
    ControlPolicy m_policy = ControlPolicy::Confirm;
};

#endif // MCPSERVER_H
