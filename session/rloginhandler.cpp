#include "rloginhandler.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QHostInfo>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#pragma comment(lib, "ws2_32.lib")
#endif

// Log to file for debugging
static QFile* debugLogFile = nullptr;
static QTextStream* debugLogStream = nullptr;

static void initDebugLog() {
    if (!debugLogFile) {
        debugLogFile = new QFile("rlogin_debug.log");
        if (debugLogFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            debugLogStream = new QTextStream(debugLogFile);
        }
    }
}

static void debugLog(const QString &msg) {
    initDebugLog();
    if (debugLogStream) {
        *debugLogStream << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
                        << " [RloginHandler] " << msg << "\n";
        debugLogStream->flush();
    }
}

RloginHandler::RloginHandler(const Common::ConnectionSettings &settings, QObject *parent)
    : ConnectionHandler(parent)
    , m_socket(nullptr)
    , m_settings(settings)
    , m_handshakeSent(false)
    , m_handshakeComplete(false)
    , m_columns(80)
    , m_rows(24)
{
    debugLog(QString("Constructor - Host: %1 Port: %2 Username: %3")
        .arg(settings.host).arg(settings.port).arg(settings.username));
}

RloginHandler::~RloginHandler()
{
    close();
}

bool RloginHandler::open()
{
    debugLog("========== open() START ==========");
    debugLog(QString("Host: %1").arg(m_settings.host));
    debugLog(QString("Port: %1").arg(m_settings.port > 0 ? m_settings.port : 513));
    debugLog(QString("Socket state before: %1").arg(m_socket ? m_socket->state() : -1));

    m_handshakeSent = false;
    m_handshakeComplete = false;
    m_pendingData.clear();

    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        debugLog("Already connected, returning true");
        return true;
    }

    quint16 port = m_settings.port > 0 ? m_settings.port : 513;

#ifdef Q_OS_WIN
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        debugLog("WSAStartup failed");
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        debugLog(QString("socket() failed: %1").arg(WSAGetLastError()));
        WSACleanup();
        return false;
    }

    // Bind to privileged port (512-1023, skip 513)
    bool boundToPrivileged = false;
    for (quint16 bindPort = 512; bindPort < 1024; bindPort++) {
        if (bindPort == 513) continue;

        sockaddr_in bindAddr;
        memset(&bindAddr, 0, sizeof(bindAddr));
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        bindAddr.sin_port = htons(bindPort);

        if (bind(sock, (sockaddr*)&bindAddr, sizeof(bindAddr)) == 0) {
            debugLog(QString("SUCCESS: Bound to privileged port %1").arg(bindPort));
            boundToPrivileged = true;
            break;
        }
    }

    if (!boundToPrivileged) {
        debugLog("WARNING: Could not bind to privileged port (requires Administrator)");
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    QString hostStr = m_settings.host;
    QHostAddress ipAddress(hostStr);

    if (ipAddress.isNull()) {
        debugLog(QString("Resolving hostname: %1").arg(hostStr));
        QHostInfo hostInfo = QHostInfo::fromName(hostStr);
        if (hostInfo.error() != QHostInfo::NoError) {
            debugLog(QString("DNS resolution failed: %1").arg(hostInfo.errorString()));
            emit errorOccurred(QString("DNS resolution failed: %1").arg(hostInfo.errorString()));
            closesocket(sock);
            WSACleanup();
            return false;
        }
        QList<QHostAddress> addresses = hostInfo.addresses();
        for (const QHostAddress &addr : addresses) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                ipAddress = addr;
                break;
            }
        }
        if (ipAddress.isNull()) {
            debugLog("No IPv4 address found");
            emit errorOccurred("No IPv4 address found for hostname");
            closesocket(sock);
            WSACleanup();
            return false;
        }
        debugLog(QString("Resolved %1 to %2").arg(hostStr).arg(ipAddress.toString()));
    }

    serverAddr.sin_addr.s_addr = inet_addr(ipAddress.toString().toUtf8().constData());

    debugLog(QString("Connecting to %1 (%2):%3").arg(hostStr).arg(ipAddress.toString()).arg(port));
    int connectResult = ::connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr));

    if (connectResult == SOCKET_ERROR) {
        debugLog(QString("connect() failed: %1").arg(WSAGetLastError()));
        closesocket(sock);
        WSACleanup();
        return false;
    }

    debugLog("Socket connected successfully");

    sockaddr_in localAddr;
    int localAddrLen = sizeof(localAddr);
    if (getsockname(sock, (sockaddr*)&localAddr, &localAddrLen) == 0) {
        quint16 localPort = ntohs(localAddr.sin_port);
        debugLog(QString("Local port: %1 (%2)").arg(localPort).arg(localPort < 1024 ? "PRIVILEGED" : "UNPRIVILEGED"));
    }

    m_socket = new QTcpSocket(this);
    if (!m_socket->setSocketDescriptor((qintptr)sock, QAbstractSocket::ConnectedState)) {
        debugLog("setSocketDescriptor failed");
        delete m_socket;
        m_socket = nullptr;
        closesocket(sock);
        WSACleanup();
        return false;
    }

    connect(m_socket, &QTcpSocket::readyRead, this, &RloginHandler::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &RloginHandler::onDisconnected);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &RloginHandler::onError);

    debugLog("Connection established, sending handshake...");
    sendRloginHandshake();

#else
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, [this]() {
        debugLog("Qt connected() signal received, sending handshake");
        sendRloginHandshake();
        emit connected();
    });
    connect(m_socket, &QTcpSocket::readyRead, this, &RloginHandler::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &RloginHandler::onDisconnected);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &RloginHandler::onError);

    m_socket->connectToHost(m_settings.host, port);
#endif

    debugLog("===================================");
    return true;
}

void RloginHandler::close()
{
    debugLog("close() called");

    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

#ifdef Q_OS_WIN
    WSACleanup();
#endif

    m_handshakeSent = false;
    m_handshakeComplete = false;
    m_pendingData.clear();
    emit disconnected();
}

bool RloginHandler::isOpen() const
{
    return m_socket && m_socket->state() != QAbstractSocket::UnconnectedState;
}

bool RloginHandler::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void RloginHandler::sendData(const QByteArray &data)
{
    debugLog(QString("sendData: %1 bytes").arg(data.size()));

    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        qint64 written = m_socket->write(data);
        m_socket->flush();
        debugLog(QString("sendData: Written %1 bytes").arg(written));
    } else {
        debugLog("sendData: Socket not connected");
    }
}

QString RloginHandler::connectionInfo() const
{
    return QString("Rlogin - %1:%2").arg(m_settings.host).arg(m_settings.port);
}

void RloginHandler::setTerminalSize(int columns, int rows)
{
    bool changed = (m_columns != columns || m_rows != rows);
    m_columns = columns;
    m_rows = rows;

    if (changed && m_handshakeComplete && m_socket &&
        m_socket->state() == QAbstractSocket::ConnectedState) {
        sendWindowSize();
    }
}

void RloginHandler::onDisconnected()
{
    debugLog("onDisconnected() called");
    m_handshakeSent = false;
    m_handshakeComplete = false;
    emit disconnected();
}

void RloginHandler::onReadyRead()
{
    if (!m_socket) return;

    QByteArray data = m_socket->readAll();

    debugLog("========== onReadyRead ==========");
    debugLog(QString("Received: %1 bytes").arg(data.size()));
    debugLog(QString("Data hex: %1").arg(QString(data.toHex(' '))));

    QString readableText;
    for (int i = 0; i < data.size(); i++) {
        unsigned char c = (unsigned char)data[i];
        if (c >= 32 && c < 127) {
            readableText += QChar(c);
        } else if (c == 0x0d) {
            readableText += "\\r";
        } else if (c == 0x0a) {
            readableText += "\\n";
        } else {
            readableText += QString("<%1>").arg(c, 2, 16, QChar('0'));
        }
    }
    debugLog(QString("Data readable: %1").arg(readableText));
    debugLog(QString("Handshake complete: %1").arg(m_handshakeComplete));
    debugLog(QString("Pending buffer before: %1 bytes").arg(m_pendingData.size()));
    debugLog("=================================");

    if (!m_handshakeComplete && !data.isEmpty()) {
        unsigned char firstByte = static_cast<unsigned char>(data[0]);

        if (firstByte == 0x01) {
            QString errorMsg = QString::fromUtf8(data.mid(1));
            debugLog(QString("Handshake FAILED - server returned error: %1").arg(errorMsg));
            emit dataReceived(QString("\r\nRlogin error: %1\r\n").arg(errorMsg).toUtf8());
            return;
        }

        if (firstByte == 0x00) {
            m_handshakeComplete = true;
            debugLog("Handshake complete - server accepted connection");
            debugLog("Emitting connected() signal...");
            emit connected();  // Emit connected signal after successful handshake
            debugLog("connected() signal emitted");
            sendWindowSize();

            if (data.size() > 1) {
                QByteArray remainingData = data.mid(1);
                m_pendingData.append(remainingData);
                processDataWithEscapeSequences();
            }
            return;
        }

        debugLog(QString("Unexpected data before handshake complete, first byte: 0x%1")
            .arg(firstByte, 2, 16, QChar('0')));
        m_handshakeComplete = true;
    }

    if (!data.isEmpty()) {
        m_pendingData.append(data);
        processDataWithEscapeSequences();
    }
}

void RloginHandler::processDataWithEscapeSequences()
{
    // Process rlogin protocol escape sequences per RFC 1282
    // Window size change: FF FF 73 73 + 8 bytes (rows, cols, xpixels, ypixels)
    // Total: 12 bytes
    //
    // Note: Server should NOT echo window size commands back to client.
    // Server uses TCP urgent data for control messages (not FF FF sequences).

    QByteArray processedData;
    int i = 0;

    while (i < m_pendingData.size()) {
        unsigned char currentByte = static_cast<unsigned char>(m_pendingData[i]);

        // Check for potential escape sequence start (0xFF)
        if (currentByte == 0xff) {
            // Need at least 4 bytes for window size: FF FF 73 73
            if (i + 3 >= m_pendingData.size()) {
                // Incomplete sequence - keep it in buffer for next packet
                debugLog(QString("Incomplete escape at pos %1, keeping %2 bytes in buffer")
                    .arg(i).arg(m_pendingData.size() - i));
                m_pendingData = m_pendingData.mid(i);
                debugLog(QString("Pending buffer after: %1 bytes").arg(m_pendingData.size()));

                if (!processedData.isEmpty()) {
                    debugLog(QString("Passing %1 bytes to terminal").arg(processedData.size()));
                    debugLog(QString("Terminal data hex: %1").arg(QString(processedData.toHex(' '))));
                    emit dataReceived(processedData);
                }
                return;
            }

            unsigned char byte1 = static_cast<unsigned char>(m_pendingData[i + 1]);
            unsigned char byte2 = static_cast<unsigned char>(m_pendingData[i + 2]);
            unsigned char byte3 = static_cast<unsigned char>(m_pendingData[i + 3]);

            // Check for window size sequence: FF FF 73 73 (or FF FF 's' 's')
            if (byte1 == 0xff && (byte2 == 0x73 || byte2 == 'S') && (byte3 == 0x73 || byte3 == 'S')) {
                // This is a window size sequence
                // Need 12 bytes total: FF FF 73 73 + 8 bytes data
                if (i + 12 > m_pendingData.size()) {
                    // Incomplete window size - keep in buffer
                    debugLog(QString("Incomplete window size at pos %1, keeping in buffer").arg(i));
                    m_pendingData = m_pendingData.mid(i);
                    debugLog(QString("Pending buffer after: %1 bytes").arg(m_pendingData.size()));

                    if (!processedData.isEmpty()) {
                        debugLog(QString("Passing %1 bytes to terminal").arg(processedData.size()));
                        emit dataReceived(processedData);
                    }
                    return;
                }

                // Extract window size data (8 bytes after FF FF 73 73)
                int dataStart = i + 4;
                int rows = (static_cast<unsigned char>(m_pendingData[dataStart]) << 8) |
                          static_cast<unsigned char>(m_pendingData[dataStart + 1]);
                int cols = (static_cast<unsigned char>(m_pendingData[dataStart + 2]) << 8) |
                          static_cast<unsigned char>(m_pendingData[dataStart + 3]);

                debugLog(QString("Server window size echo: %1 cols x %2 rows").arg(cols).arg(rows));
                debugLog(QString("Skipping 12 bytes at pos %1").arg(i));

                // Skip the entire 12-byte window size sequence
                i += 12;
                continue;
            }

            // Not a window size sequence - treat as regular data
            processedData.append(m_pendingData[i]);
            i++;
            continue;
        }

        // Regular byte - just append it
        processedData.append(m_pendingData[i]);
        i++;
    }

    // Clear the pending buffer if we processed everything
    if (i >= m_pendingData.size()) {
        m_pendingData.clear();
        debugLog("Pending buffer cleared (all data processed)");
    }

    if (!processedData.isEmpty()) {
        debugLog(QString("Passing %1 bytes to terminal").arg(processedData.size()));
        debugLog(QString("Terminal data hex: %1").arg(QString(processedData.toHex(' '))));
        debugLog("Emitting dataReceived() signal...");
        emit dataReceived(processedData);
        debugLog("dataReceived() signal emitted");
    } else {
        debugLog("No data to pass to terminal");
    }
}

void RloginHandler::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QString errorMsg = m_socket ? m_socket->errorString() : "Unknown error";
    qWarning() << "[RloginHandler] Error:" << errorMsg;
    debugLog(QString("onError: %1").arg(errorMsg));
    emit errorOccurred(errorMsg);
}

void RloginHandler::sendRloginHandshake()
{
    debugLog("========== sendRloginHandshake ==========");

    if (!m_socket) {
        debugLog("ERROR: No socket available");
        return;
    }

    if (m_handshakeSent) {
        debugLog("Handshake already sent, returning");
        return;
    }

    quint16 localPort = 0;
#ifdef Q_OS_WIN
    SOCKET s = (SOCKET)m_socket->socketDescriptor();
    sockaddr_in addr;
    int addrLen = sizeof(addr);
    if (getsockname(s, (sockaddr*)&addr, &addrLen) == 0) {
        localPort = ntohs(addr.sin_port);
    }
#endif

    QByteArray handshake;
    handshake.append('\0');  // No stderr channel

    QString localUser = QString::fromLocal8Bit(qgetenv("USERNAME"));
    if (localUser.isEmpty()) {
        localUser = QString::fromLocal8Bit(qgetenv("USER"));
    }
    
    // Convert to ASCII, replacing non-ASCII characters with their closest ASCII equivalent
    // or removing them. rlogin protocol requires ASCII username.
    QString asciiUser;
    for (const QChar &ch : localUser) {
        uint unicode = ch.unicode();
        if (unicode < 128) {
            // ASCII character - keep as is
            asciiUser += ch;
        } else if (unicode >= 0x4E00 && unicode <= 0x9FFF) {
            // CJK character - skip (no good ASCII mapping)
            continue;
        } else {
            // Other non-ASCII - skip
            continue;
        }
    }
    
    // If username is empty after filtering, use a reasonable default
    localUser = asciiUser.isEmpty() ? "user" : asciiUser;
    
    // Ensure username only contains valid characters (alphanumeric, _, -)
    QString sanitizedUser;
    for (const QChar &ch : localUser) {
        if (ch.isLetterOrNumber() || ch == '_' || ch == '-') {
            sanitizedUser += ch;
        }
    }
    localUser = sanitizedUser.isEmpty() ? "user" : sanitizedUser;

    QString remoteUser = m_settings.username;
    if (remoteUser.isEmpty()) {
        remoteUser = localUser;
    }

    // Terminal type/speed format: "terminaltype/speed"
    // Speed is baud rate (9600), NOT window size!
    QString terminalInfo = "xterm/9600";

    handshake.append(localUser.toUtf8());
    handshake.append('\0');
    handshake.append(remoteUser.toUtf8());
    handshake.append('\0');
    handshake.append(terminalInfo.toUtf8());
    handshake.append('\0');

    debugLog(QString("Local port: %1 (%2)").arg(localPort).arg(localPort > 0 && localPort < 1024 ? "PRIVILEGED" : "UNPRIVILEGED"));
    debugLog(QString("Handshake packet hex: %1").arg(QString(handshake.toHex(' '))));
    debugLog(QString("Packet size: %1 bytes").arg(handshake.size()));
    debugLog(QString("Local user: '%1', Remote user: '%2', Terminal: '%3'")
        .arg(localUser).arg(remoteUser).arg(terminalInfo));
    debugLog("==============================================");

    qint64 written = m_socket->write(handshake);
    m_socket->flush();

    debugLog(QString("Bytes written: %1").arg(written));
    m_handshakeSent = true;
    debugLog("Handshake sent, waiting for server response...");
}

void RloginHandler::sendWindowSize()
{
    // Send window size using rlogin escape sequence
    // RFC 1282: Format is 12 bytes: 0xFF 0xFF 's' 's' rows(2) cols(2) xpixel(2) ypixel(2)

    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QByteArray windowSize;
    windowSize.append('\xFF');
    windowSize.append('\xFF');
    windowSize.append('s');
    windowSize.append('s');  // RFC 1282 requires two 's' characters
    windowSize.append(static_cast<char>((m_rows >> 8) & 0xFF));
    windowSize.append(static_cast<char>(m_rows & 0xFF));
    windowSize.append(static_cast<char>((m_columns >> 8) & 0xFF));
    windowSize.append(static_cast<char>(m_columns & 0xFF));
    windowSize.append('\0');
    windowSize.append('\0');
    windowSize.append('\0');
    windowSize.append('\0');

    debugLog(QString("Sending window size: %1 cols x %2 rows, hex: %3")
        .arg(m_columns).arg(m_rows).arg(QString(windowSize.toHex(' '))));

    m_socket->write(windowSize);
    m_socket->flush();
}
