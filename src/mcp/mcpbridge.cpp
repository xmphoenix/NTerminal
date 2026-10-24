#include "mcp/mcpbridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QThread>

#include <cstdio>

namespace {

constexpr int kConnectTimeoutMs = 5000;

void reportError(const QString &message)
{
    fprintf(stderr, "[NTerminal MCP] %s\n", qPrintable(message));
    fflush(stderr);
}

} // namespace

McpBridge::McpBridge(QObject *parent)
    : QObject(parent)
{
}

McpBridge::~McpBridge()
{
    if (m_reader) {
        m_reader->requestInterruption();
        if (!m_reader->wait(1000)) {
            // stdin 可能仍被客户端占用（例如 socket 先断开），退出时强制收尾
            m_reader->terminate();
            m_reader->wait(500);
        }
        delete m_reader;
        m_reader = nullptr;
    }
}

bool McpBridge::readEndpoint(QString *pipe, QString *error) const
{
    const QString path =
        QDir::homePath() + QStringLiteral("/.nterminal/mcp-endpoint.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral(
                     "NTerminal MCP service is not running. Start NTerminal and "
                     "enable \"MCP -> Enable MCP service\" first. Endpoint file: %1")
                     .arg(path);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("Invalid MCP endpoint file: %1").arg(path);
        return false;
    }

    *pipe = document.object().value(QStringLiteral("pipe")).toString();
    if (pipe->isEmpty()) {
        *error = QStringLiteral("MCP endpoint file has no pipe name: %1").arg(path);
        return false;
    }
    return true;
}

int McpBridge::run()
{
    QString pipe;
    QString error;
    if (!readEndpoint(&pipe, &error)) {
        reportError(error);
        return 1;
    }

    if (!m_stdout.open(stdout, QIODevice::WriteOnly)) {
        reportError(QStringLiteral("Cannot open stdout."));
        return 1;
    }

    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::readyRead, this, [this] {
        const QByteArray data = m_socket->readAll();
        if (!data.isEmpty()) {
            writeStdout(data);
        }
    });
    connect(m_socket, &QLocalSocket::disconnected, this, [this] {
        if (m_stdinClosed) {
            QCoreApplication::quit();
            return;
        }
        fail(QStringLiteral(
            "NTerminal MCP service disconnected. Is NTerminal still running?"));
    });
    connect(m_socket, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) {
                fail(QStringLiteral("MCP pipe error: %1").arg(m_socket->errorString()));
            });

    m_socket->connectToServer(pipe);
    if (!m_socket->waitForConnected(kConnectTimeoutMs)) {
        reportError(QStringLiteral("Cannot connect to NTerminal MCP service (%1): %2")
                        .arg(pipe, m_socket->errorString()));
        return 1;
    }

    // stdin 读取放在线程里，避免阻塞 Qt 事件循环；EOF 即客户端关闭，退出桥。
    m_reader = QThread::create([this] {
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly)) {
            QMetaObject::invokeMethod(
                this,
                [this] {
                    fail(QStringLiteral("Cannot open stdin."));
                },
                Qt::QueuedConnection);
            return;
        }

        while (!QThread::currentThread()->isInterruptionRequested()) {
            const QByteArray line = input.readLine();
            if (line.isEmpty()) {
                if (input.atEnd()) {
                    break;
                }
                continue;
            }
            QMetaObject::invokeMethod(
                this, [this, line] { forwardLine(line); }, Qt::QueuedConnection);
        }

        QMetaObject::invokeMethod(
            this,
            [this] {
                m_stdinClosed = true;
                m_socket->disconnectFromServer();
            },
            Qt::QueuedConnection);
        QMetaObject::invokeMethod(
            this, [] { QCoreApplication::quit(); }, Qt::QueuedConnection);
    });
    m_reader->start();

    return QCoreApplication::exec();
}

void McpBridge::forwardLine(const QByteArray &line)
{
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    QByteArray message = line;
    while (message.endsWith('\n') || message.endsWith('\r')) {
        message.chop(1);
    }
    if (message.isEmpty()) {
        return;
    }
    message.append('\n');
    m_socket->write(message);
    m_socket->flush();
}

void McpBridge::writeStdout(const QByteArray &data)
{
    if (m_stdout.write(data) != data.size() || !m_stdout.flush()) {
        fail(QStringLiteral("Cannot write to stdout."));
    }
}

void McpBridge::fail(const QString &message)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    reportError(message);
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
    QCoreApplication::exit(1);
}
