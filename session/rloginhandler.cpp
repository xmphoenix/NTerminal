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
    // Process rlogin protocol escape sequences
    // BSD rlogin protocol:
    // - 0xff 0xff 's' + window size data from server (skip entire packet)
    // - 0xff 0xff followed by other = escaped 0xff (output single 0xff)

    QByteArray processedData;
    int i = 0;

    while (i < m_pendingData.size()) {
        unsigned char currentByte = static_cast<unsigned char>(m_pendingData[i]);

        // Check for potential escape sequence start
        if (currentByte == 0xff) {
            // Need at least 2 more bytes to determine what this is
            if (i + 1 >= m_pendingData.size()) {
                // Incomplete sequence - keep it in buffer for next packet
                debugLog(QString("Incomplete escape at pos %1, keeping %2 bytes in buffer")
                    .arg(i).arg(m_pendingData.size() - i));
                // Keep the incomplete data from position i onwards
                m_pendingData = m_pendingData.mid(i);
                debugLog(QString("Pending buffer after: %1 bytes").arg(m_pendingData.size()));

                // Emit any processed data before returning
                if (!processedData.isEmpty()) {
                    debugLog(QString("Passing %1 bytes to terminal").arg(processedData.size()));
                    debugLog(QString("Terminal data hex: %1").arg(QString(processedData.toHex(' '))));
                    debugLog("Emitting dataReceived() signal (before incomplete escape return)...");
                    emit dataReceived(processedData);
                    debugLog("dataReceived() signal emitted");
                }
                return;
            }

            unsigned char nextByte = static_cast<unsigned char>(m_pendingData[i + 1]);

            if (nextByte == 0xff) {
                // Found ff ff sequence
                if (i + 2 >= m_pendingData.size()) {
                    // Incomplete ff ff sequence - keep it in buffer
                    debugLog(QString("Incomplete ff ff at pos %1, keeping %2 bytes in buffer")
                        .arg(i).arg(m_pendingData.size() - i));
                    m_pendingData = m_pendingData.mid(i);
                    debugLog(QString("Pending buffer after: %1 bytes").arg(m_pendingData.size()));

                    // Emit any processed data before returning
                    if (!processedData.isEmpty()) {
                        debugLog(QString("Passing %1 bytes to terminal").arg(processedData.size()));
                        debugLog(QString("Terminal data hex: %1").arg(QString(processedData.toHex(' '))));
                        emit dataReceived(processedData);
                    }
                    return;
                }

                unsigned char cmd = static_cast<unsigned char>(m_pendingData[i + 2]);

                if (cmd == 's' || cmd == 'S') {
                    // Window size echo from server
                    // Some rlogind implementations escape control characters:
                    // - Standard format: ff ff s + 8 bytes (binary) = 11 bytes total
                    // - Escaped format: ff ff s + escaped bytes (e.g., 0x00 becomes "^@" = 0x5e 0x40)
                    //   In escaped format, each null byte becomes 2 bytes, so 8 nulls become 16 bytes
                    //   Total: ff ff s + 16 bytes = 19 bytes (but may vary)
                    //
                    // Strategy: Skip until we find clear terminal data markers

                    int skipLen = 3;  // Start with ff ff s
                    bool foundEnd = false;

                    // Scan forward to find the end of window size data
                    for (int k = i + 3; k < m_pendingData.size() && k < i + 25; k++) {
                        unsigned char c = static_cast<unsigned char>(m_pendingData[k]);

                        // Check for invalid window size data characters
                        // Window size data should be: binary data (0x00-0x1F except ESC) or escaped ^@ (0x5e 0x40)
                        // If we see BEL (0x07) or other control characters that shouldn't be in window size data,
                        // this is not a valid window size echo
                        if (k < i + 11) {  // Within the expected window size data range
                            if (c == 0x07) {  // BEL character
                                // This is not valid window size data - only skip ff ff s
                                skipLen = 3;
                                foundEnd = true;
                                debugLog(QString("Found BEL at pos %1, invalid window size data, only skipping ff ff s").arg(k));
                                break;
                            }
                        }

                        // Check for terminal control sequence start
                        if (c == 0x1B) {
                            // ESC character - likely start of terminal data
                            skipLen = k - i;
                            foundEnd = true;
                            debugLog(QString("Found ESC at pos %1, ending window size skip").arg(k));
                            break;
                        }

                        // Check for carriage return or newline
                        if (c == 0x0D || c == 0x0A) {
                            skipLen = k - i;
                            foundEnd = true;
                            debugLog(QString("Found CR/LF at pos %1, ending window size skip").arg(k));
                            break;
                        }

                        // Check for printable ASCII that's not part of escape sequences
                        // Window size data is typically: binary nulls, or escaped as ^@ (0x5e 0x40)
                        // If we see regular letters (not ^ or @), it's probably terminal data
                        if (k >= i + 11) {  // After minimum window size length
                            if (c >= 'A' && c <= 'Z' && c != 'W') {  // Capital letters (except W for Welcome)
                                skipLen = k - i;
                                foundEnd = true;
                                debugLog(QString("Found printable char '%1' at pos %2, ending window size skip").arg(QChar(c)).arg(k));
                                break;
                            }
                            if (c >= 'a' && c <= 'z' && c != 'w' && c != 's') {  // Lowercase letters
                                skipLen = k - i;
                                foundEnd = true;
                                debugLog(QString("Found printable char '%1' at pos %2, ending window size skip").arg(QChar(c)).arg(k));
                                break;
                            }
                        }
                    }

                    // If we didn't find a clear end marker
                    if (!foundEnd) {
                        // If we're at the end of the buffer and haven't seen at least 11 bytes,
                        // this is likely an incomplete window size echo - keep it in buffer
                        if (i + 11 > m_pendingData.size()) {
                            debugLog(QString("Incomplete window size at pos %1, need at least 11 bytes, have %2, keeping in buffer")
                                .arg(i).arg(m_pendingData.size() - i));
                            m_pendingData = m_pendingData.mid(i);
                            debugLog(QString("Pending buffer after: %1 bytes").arg(m_pendingData.size()));

                            // Emit any processed data before returning
                            if (!processedData.isEmpty()) {
                                debugLog(QString("Passing %1 bytes to terminal").arg(processedData.size()));
                                debugLog(QString("Terminal data hex: %1").arg(QString(processedData.toHex(' '))));
                                emit dataReceived(processedData);
                            }
                            return;
                        }

                        // We have at least 11 bytes but no clear end marker
                        // This might be escaped format - skip everything that looks like window size data
                        skipLen = qMin(20, m_pendingData.size() - i);
                        debugLog(QString("No clear end marker found, skipping %1 bytes (max scan)").arg(skipLen));
                    }

                    debugLog(QString("Window size echo at pos %1, skipping %2 bytes")
                        .arg(i).arg(skipLen));

                    // Log the window size data for debugging
                    QByteArray wsData = m_pendingData.mid(i, skipLen);
                    debugLog(QString("Window size data hex: %1").arg(QString(wsData.toHex(' '))));

                    i += skipLen;
                    continue;
                } else {
                    // ff ff followed by non-'s' means escaped 0xff
                    // Output single 0xff and skip the second ff
                    debugLog(QString("Escaped ff at pos %1").arg(i));
                    processedData.append('\xff');
                    i += 2;
                    continue;
                }
            }
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
    QString asciiUser;
    for (const QChar &ch : localUser) {
        if (ch.unicode() < 128) {
            asciiUser += ch;
        } else {
            asciiUser += '_';
        }
    }
    localUser = asciiUser.isEmpty() ? "guest" : asciiUser;

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
    // Format: 0xFF 0xFF 's' rows(2) cols(2) xpixel(2) ypixel(2) = 11 bytes

    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QByteArray windowSize;
    windowSize.append('\xFF');
    windowSize.append('\xFF');
    windowSize.append('s');
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
