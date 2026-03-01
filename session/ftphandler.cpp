#include "ftphandler.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QFile>
#include <QElapsedTimer>

FtpHandler::FtpHandler(const Common::ConnectionSettings &settings, QObject *parent)
    : ConnectionHandler(parent)
    , m_socket(new QTcpSocket(this))
    , m_dataSocket(nullptr)
    , m_settings(settings)
    , m_authenticated(false)
    , m_authState(AuthNone)
    , m_localDir(QDir::homePath())
    , m_passiveMode(true)
    , m_localFile(nullptr)
    , m_isDownloading(false)
    , m_isListing(false)
    , m_transferBytes(0)
    , m_totalBytes(0)
    , m_transferType(Binary)
    , m_isMultiTransfer(false)
    , m_promptMode(true)
    , m_verboseMode(true)
    , m_hashMode(false)
    , m_hashBlockSize(1024)
{
    connect(m_socket, &QTcpSocket::connected, this, &FtpHandler::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &FtpHandler::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &FtpHandler::onReadyRead);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &FtpHandler::onError);
}

FtpHandler::~FtpHandler()
{
    close();
}

bool FtpHandler::open()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        return true;
    }

    qDebug() << "[FtpHandler] Connecting to" << m_settings.host << ":" << m_settings.port;
    m_socket->connectToHost(m_settings.host, m_settings.port);
    return true;
}

void FtpHandler::close()
{
    if (m_localFile) {
        m_localFile->close();
        delete m_localFile;
        m_localFile = nullptr;
    }

    if (m_dataSocket) {
        m_dataSocket->disconnectFromHost();
        delete m_dataSocket;
        m_dataSocket = nullptr;
    }

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        sendCommand("QUIT");
        m_socket->disconnectFromHost();
    }
}

bool FtpHandler::isOpen() const
{
    return m_socket->state() != QAbstractSocket::UnconnectedState;
}

bool FtpHandler::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState && m_authenticated;
}

void FtpHandler::sendData(const QByteArray &data)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    if (!m_authenticated) {
        return;
    }

    QString input = QString::fromUtf8(data);

    for (const QChar &ch : input) {
        if (ch == '\r' || ch == '\n') {
            QString command = m_commandBuffer.trimmed();
            m_commandBuffer.clear();
            emit dataReceived("\r\n");

            if (command.isEmpty()) {
                emit dataReceived("ftp> ");
                continue;
            }

            if (handleLocalCommand(command)) {
                emit dataReceived("ftp> ");
                continue;
            }

            // Parse and process FTP command
            processFtpCommand(command);
        } else if (ch == '\b' || ch == '\x7f') {
            if (!m_commandBuffer.isEmpty()) {
                m_commandBuffer.chop(1);
                emit dataReceived("\b \b");
            }
        } else if (ch.isPrint() || ch == ' ') {
            m_commandBuffer.append(ch);
            emit dataReceived(QString(ch).toUtf8());
        }
    }
}

QString FtpHandler::connectionInfo() const
{
    return QString("FTP - %1:%2").arg(m_settings.host).arg(m_settings.port);
}

void FtpHandler::setTerminalSize(int columns, int rows)
{
    Q_UNUSED(columns);
    Q_UNUSED(rows);
}

void FtpHandler::processFtpCommand(const QString &command)
{
    QString cmd = command.toLower().section(' ', 0, 0);
    QString args = command.section(' ', 1).trimmed();

    // Commands that need data connection
    if (cmd == "ls" || cmd == "dir") {
        m_isListing = true;
        m_isDownloading = false;
        m_pendingCommand = args.isEmpty() ? "LIST" : QString("LIST %1").arg(args);
        sendCommand("PASV");
    } else if (cmd == "nls" || cmd == "nlist") {
        m_isListing = true;
        m_isDownloading = false;
        m_pendingCommand = args.isEmpty() ? "NLST" : QString("NLST %1").arg(args);
        sendCommand("PASV");
    } else if (cmd == "get") {
        handleGetCommand(args);
    } else if (cmd == "mget") {
        handleMgetCommand(args);
    } else if (cmd == "put") {
        handlePutCommand(args);
    } else if (cmd == "mput") {
        handleMputCommand(args);
    } else if (cmd == "append") {
        handleAppendCommand(args);
    }
    // Simple FTP commands (no data connection needed)
    else if (cmd == "cd") {
        sendCommand(QString("CWD %1").arg(args));
    } else if (cmd == "cdup") {
        sendCommand("CDUP");
    } else if (cmd == "pwd") {
        sendCommand("PWD");
    } else if (cmd == "mkdir") {
        sendCommand(QString("MKD %1").arg(args));
    } else if (cmd == "rmdir") {
        sendCommand(QString("RMD %1").arg(args));
    } else if (cmd == "delete" || cmd == "rm") {
        sendCommand(QString("DELE %1").arg(args));
    } else if (cmd == "rename") {
        handleRenameCommand(args);
    } else if (cmd == "ascii") {
        m_transferType = ASCII;
        sendCommand("TYPE A");
    } else if (cmd == "binary" || cmd == "bin" || cmd == "image") {
        m_transferType = Binary;
        sendCommand("TYPE I");
    } else if (cmd == "type") {
        emit dataReceived(QString("Using %1 mode to transfer files.\r\n")
            .arg(m_transferType == ASCII ? "ascii" : "binary").toUtf8());
        emit dataReceived("ftp> ");
    } else if (cmd == "size") {
        sendCommand(QString("SIZE %1").arg(args));
    } else if (cmd == "modtime") {
        sendCommand(QString("MDTM %1").arg(args));
    } else if (cmd == "chmod") {
        handleChmodCommand(args);
    } else if (cmd == "system" || cmd == "sys") {
        sendCommand("SYST");
    } else if (cmd == "stat" || cmd == "status") {
        sendCommand("STAT");
    } else if (cmd == "help" || cmd == "?") {
        sendCommand("HELP");
    } else if (cmd == "user") {
        handleUserCommand(args);
    } else if (cmd == "quote" || cmd == "literal") {
        sendCommand(args);
    } else if (cmd == "quit" || cmd == "exit" || cmd == "bye") {
        emit dataReceived("221 Goodbye.\r\n");
        emit dataReceived("Connection closed.\r\n");
        // Close connection - this will trigger disconnected signal
        m_socket->disconnectFromHost();
        return;  // Don't show prompt after quit
    } else if (cmd == "noop") {
        sendCommand("NOOP");
    } else if (cmd == "features" || cmd == "feat") {
        sendCommand("FEAT");
    } else if (cmd == "pwd" || cmd == "xpwd") {
        sendCommand("PWD");
    } else if (cmd == "struct") {
        sendCommand(QString("STRU %1").arg(args.toUpper()));
    } else if (cmd == "mode") {
        sendCommand(QString("MODE %1").arg(args.toUpper()));
    } else {
        // Unknown command, try sending as-is
        emit dataReceived(QString("Unknown command: %1\r\n").arg(cmd).toUtf8());
        emit dataReceived("ftp> ");
    }
}

void FtpHandler::handleGetCommand(const QString &args)
{
    if (args.isEmpty()) {
        emit dataReceived("Usage: get remote-file [local-file]\r\n");
        emit dataReceived("ftp> ");
        return;
    }

    QStringList argList = args.split(' ', Qt::SkipEmptyParts);
    QString remoteFile = argList[0];
    QString localFile = argList.size() > 1 ? argList[1] : QFileInfo(remoteFile).fileName();
    
    m_localFilePath = QDir(m_localDir).filePath(localFile);
    m_pendingCommand = QString("RETR %1").arg(remoteFile);
    m_isDownloading = true;
    m_isListing = false;
    m_transferBytes = 0;
    m_totalBytes = 0;
    m_isMultiTransfer = false;
    
    sendCommand("PASV");
}

void FtpHandler::handleMgetCommand(const QString &args)
{
    if (args.isEmpty()) {
        emit dataReceived("Usage: mget remote-files ...\r\n");
        emit dataReceived("ftp> ");
        return;
    }

    m_pendingFiles = args.split(' ', Qt::SkipEmptyParts);
    m_isMultiTransfer = true;
    m_isDownloading = true;
    
    processNextPendingFile();
}

void FtpHandler::handlePutCommand(const QString &args)
{
    if (args.isEmpty()) {
        emit dataReceived("Usage: put local-file [remote-file]\r\n");
        emit dataReceived("ftp> ");
        return;
    }

    QStringList argList = args.split(' ', Qt::SkipEmptyParts);
    QString localFile = argList[0];
    QString remoteFile = argList.size() > 1 ? argList[1] : QFileInfo(localFile).fileName();
    
    m_localFilePath = QDir(m_localDir).filePath(localFile);
    
    if (!QFile::exists(m_localFilePath)) {
        emit dataReceived(QString("Local file not found: %1\r\n").arg(m_localFilePath).toUtf8());
        emit dataReceived("ftp> ");
        return;
    }
    
    m_pendingCommand = QString("STOR %1").arg(remoteFile);
    m_isDownloading = false;
    m_isListing = false;
    m_transferBytes = 0;
    m_totalBytes = 0;
    m_isMultiTransfer = false;
    
    sendCommand("PASV");
}

void FtpHandler::handleMputCommand(const QString &args)
{
    if (args.isEmpty()) {
        emit dataReceived("Usage: mput local-files ...\r\n");
        emit dataReceived("ftp> ");
        return;
    }

    // Expand local file patterns
    m_pendingFiles.clear();
    for (const QString &pattern : args.split(' ', Qt::SkipEmptyParts)) {
        QStringList files = expandLocalFiles(pattern);
        m_pendingFiles.append(files);
    }
    
    if (m_pendingFiles.isEmpty()) {
        emit dataReceived("No matching files found.\r\n");
        emit dataReceived("ftp> ");
        return;
    }
    
    m_isMultiTransfer = true;
    m_isDownloading = false;
    
    processNextPendingFile();
}

void FtpHandler::handleAppendCommand(const QString &args)
{
    if (args.isEmpty()) {
        emit dataReceived("Usage: append local-file [remote-file]\r\n");
        emit dataReceived("ftp> ");
        return;
    }

    QStringList argList = args.split(' ', Qt::SkipEmptyParts);
    QString localFile = argList[0];
    QString remoteFile = argList.size() > 1 ? argList[1] : QFileInfo(localFile).fileName();
    
    m_localFilePath = QDir(m_localDir).filePath(localFile);
    
    if (!QFile::exists(m_localFilePath)) {
        emit dataReceived(QString("Local file not found: %1\r\n").arg(m_localFilePath).toUtf8());
        emit dataReceived("ftp> ");
        return;
    }
    
    m_pendingCommand = QString("APPE %1").arg(remoteFile);
    m_isDownloading = false;
    m_isListing = false;
    m_transferBytes = 0;
    
    sendCommand("PASV");
}

void FtpHandler::handleRenameCommand(const QString &args)
{
    QStringList argList = args.split(' ', Qt::SkipEmptyParts);
    if (argList.size() < 2) {
        emit dataReceived("Usage: rename from-name to-name\r\n");
        emit dataReceived("ftp> ");
        return;
    }
    
    m_renameFrom = argList[0];
    sendCommand(QString("RNFR %1").arg(m_renameFrom));
}

void FtpHandler::handleChmodCommand(const QString &args)
{
    QStringList argList = args.split(' ', Qt::SkipEmptyParts);
    if (argList.size() < 2) {
        emit dataReceived("Usage: chmod mode file\r\n");
        emit dataReceived("ftp> ");
        return;
    }
    
    sendCommand(QString("SITE CHMOD %1 %2").arg(argList[0], argList[1]));
}

void FtpHandler::handleUserCommand(const QString &args)
{
    if (args.isEmpty()) {
        emit dataReceived("Usage: user username [password]\r\n");
        emit dataReceived("ftp> ");
        return;
    }
    
    QStringList argList = args.split(' ', Qt::SkipEmptyParts);
    m_settings.username = argList[0];
    m_settings.password = argList.size() > 1 ? argList[1] : "";
    
    m_authenticated = false;
    m_authState = AuthNone;
    sendCommand(QString("USER %1").arg(m_settings.username));
    m_authState = AuthSentUser;
}

void FtpHandler::processNextPendingFile()
{
    if (m_pendingFiles.isEmpty()) {
        m_isMultiTransfer = false;
        emit dataReceived("ftp> ");
        return;
    }
    
    QString file = m_pendingFiles.takeFirst();
    
    if (m_promptMode) {
        emit dataReceived(QString("m%s %s? ").arg(m_isDownloading ? "get" : "put", file).toUtf8());
        // For simplicity, auto-confirm in this implementation
        // In a real client, we'd wait for y/n response
    }
    
    if (m_isDownloading) {
        // mget: download file
        m_localFilePath = QDir(m_localDir).filePath(QFileInfo(file).fileName());
        m_pendingCommand = QString("RETR %1").arg(file);
        m_isDownloading = true;
        m_isListing = false;
        m_transferBytes = 0;
        sendCommand("PASV");
    } else {
        // mput: upload file
        m_localFilePath = QDir(m_localDir).filePath(file);
        if (!QFile::exists(m_localFilePath)) {
            emit dataReceived(QString("Local file not found: %1\r\n").arg(m_localFilePath).toUtf8());
            processNextPendingFile();
            return;
        }
        m_pendingCommand = QString("STOR %1").arg(file);
        m_isDownloading = false;
        m_isListing = false;
        m_transferBytes = 0;
        sendCommand("PASV");
    }
}

QStringList FtpHandler::expandLocalFiles(const QString &pattern)
{
    QStringList result;
    
    QDir dir(m_localDir);
    QString fileName = pattern;
    
    // Check if pattern contains directory path
    if (pattern.contains('/') || pattern.contains('\\')) {
        QFileInfo fi(pattern);
        dir.setPath(fi.path());
        fileName = fi.fileName();
    }
    
    // Simple wildcard expansion
    QStringList filters;
    filters << fileName;
    
    QStringList entries = dir.entryList(filters, QDir::Files);
    for (const QString &entry : entries) {
        result << entry;
    }
    
    if (result.isEmpty() && QFile::exists(QDir(m_localDir).filePath(pattern))) {
        result << pattern;
    }
    
    return result;
}

void FtpHandler::onConnected()
{
    emit connected();
}

void FtpHandler::onDisconnected()
{
    m_authenticated = false;
    m_authState = AuthNone;
    if (m_dataSocket) {
        m_dataSocket->deleteLater();
        m_dataSocket = nullptr;
    }
    if (m_localFile) {
        m_localFile->close();
        delete m_localFile;
        m_localFile = nullptr;
    }
    emit disconnected();
}

void FtpHandler::onReadyRead()
{
    QByteArray data = m_socket->readAll();
    m_receiveBuffer.append(QString::fromUtf8(data));
    emit dataReceived(data);

    while (m_receiveBuffer.contains("\r\n")) {
        int pos = m_receiveBuffer.indexOf("\r\n");
        QString line = m_receiveBuffer.left(pos);
        m_receiveBuffer = m_receiveBuffer.mid(pos + 2);
        processResponse(line);
    }
}

void FtpHandler::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    emit errorOccurred(m_socket->errorString());
}

void FtpHandler::onDataConnected()
{
    if (!m_isDownloading && !m_isListing && !m_localFilePath.isEmpty()) {
        m_localFile = new QFile(m_localFilePath, this);
        if (m_localFile->open(QIODevice::ReadOnly)) {
            m_transferBytes = 0;
            m_transferTimer.start();
            sendCommand(m_pendingCommand);
        } else {
            emit dataReceived(QString("Failed to open local file: %1\r\n").arg(m_localFilePath).toUtf8());
            emit dataReceived("ftp> ");
            m_localFile->deleteLater();
            m_localFile = nullptr;
        }
    } else {
        sendCommand(m_pendingCommand);
    }
}

void FtpHandler::onDataReadyRead()
{
    if (!m_dataSocket) return;

    QByteArray data = m_dataSocket->readAll();

    if (m_isDownloading && m_localFile) {
        m_localFile->write(data);
        m_transferBytes += data.size();
        if (m_hashMode) {
            for (int i = 0; i < data.size() / m_hashBlockSize; ++i) {
                emit dataReceived("#");
            }
        }
    } else if (m_isListing) {
        QString text = QString::fromUtf8(data);
        text.replace("\n", "\r\n");
        emit dataReceived(text.toUtf8());
    }
}

void FtpHandler::onDataDisconnected()
{
    if (m_localFile) {
        if (m_isDownloading) {
            m_localFile->close();
            qint64 elapsed = m_transferTimer.elapsed();
            QString speedStr = formatTransferSpeed(m_transferBytes, elapsed);
            QString sizeStr = formatSize(m_transferBytes);
            emit dataReceived(QString("\r\n%1: %2 received in %3 sec (%4)\r\n")
                .arg(QFileInfo(*m_localFile).fileName())
                .arg(sizeStr)
                .arg(elapsed / 1000.0, 0, 'f', 3)
                .arg(speedStr).toUtf8());
        }
        delete m_localFile;
        m_localFile = nullptr;
    }

    m_isListing = false;
    m_isDownloading = false;

    if (m_dataSocket) {
        m_dataSocket->deleteLater();
        m_dataSocket = nullptr;
    }
    
    if (m_isMultiTransfer) {
        processNextPendingFile();
    }
}

void FtpHandler::onBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);
    if (m_localFile && m_dataSocket && !m_isDownloading && !m_isListing) {
        QByteArray data = m_localFile->read(32768);
        if (!data.isEmpty()) {
            m_dataSocket->write(data);
            m_transferBytes += data.size();
            if (m_hashMode) {
                for (int i = 0; i < data.size() / m_hashBlockSize; ++i) {
                    emit dataReceived("#");
                }
            }
        } else {
            m_localFile->close();
            qint64 elapsed = m_transferTimer.elapsed();
            QString speedStr = formatTransferSpeed(m_transferBytes, elapsed);
            QString sizeStr = formatSize(m_transferBytes);
            emit dataReceived(QString("\r\n%1: %s sent in %3 sec (%4)\r\n")
                .arg(QFileInfo(*m_localFile).fileName())
                .arg(sizeStr)
                .arg(elapsed / 1000.0, 0, 'f', 3)
                .arg(speedStr).toUtf8());
            m_dataSocket->disconnectFromHost();
        }
    }
}

void FtpHandler::processResponse(const QString &response)
{
    qDebug() << "[FtpHandler] Response:" << response;

    // Authentication handling
    if (response.startsWith("220") && m_authState == AuthNone) {
        sendCommand(QString("USER %1").arg(m_settings.username));
        m_authState = AuthSentUser;
    } else if (response.startsWith("331") && m_authState == AuthSentUser) {
        sendCommand(QString("PASS %1").arg(m_settings.password));
        m_authState = AuthSentPass;
    } else if (response.startsWith("230") && !m_authenticated) {
        m_authenticated = true;
        m_authState = AuthDone;
        emit dataReceived("\r\nFTP session established. Type 'help' for available commands.\r\n");
        emit dataReceived("ftp> ");
    }
    // Password required for user command
    else if (response.startsWith("331") && m_authState == AuthDone) {
        // User sent new username via USER command, need password
        emit dataReceived("Password: ");
    }
    // Passive mode
    else if (response.startsWith("227")) {
        setupPassiveMode(response);
    }
    // Transfer starting
    else if (response.startsWith("125") || response.startsWith("150")) {
        if (m_isDownloading && !m_localFilePath.isEmpty() && !m_localFile) {
            m_localFile = new QFile(m_localFilePath, this);
            if (!m_localFile->open(QIODevice::WriteOnly)) {
                emit dataReceived(QString("Failed to create local file: %1\r\n").arg(m_localFilePath).toUtf8());
                emit dataReceived("ftp> ");
                delete m_localFile;
                m_localFile = nullptr;
                return;
            }
            m_transferBytes = 0;
            m_transferTimer.start();
        } else if (!m_isDownloading && !m_isListing && m_localFile) {
            m_transferTimer.start();
            QByteArray data = m_localFile->read(32768);
            if (!data.isEmpty()) {
                m_dataSocket->write(data);
                m_transferBytes += data.size();
            }
        }
    }
    // Transfer complete
    else if (response.startsWith("226")) {
        if (m_localFile && !m_isDownloading) {
            // Upload complete
            m_localFile->close();
            qint64 elapsed = m_transferTimer.elapsed();
            QString speedStr = formatTransferSpeed(m_transferBytes, elapsed);
            QString sizeStr = formatSize(m_transferBytes);
            emit dataReceived(QString("\r\n%1: %2 sent in %3 sec (%4)\r\n")
                .arg(QFileInfo(*m_localFile).fileName())
                .arg(sizeStr)
                .arg(elapsed / 1000.0, 0, 'f', 3)
                .arg(speedStr).toUtf8());
            delete m_localFile;
            m_localFile = nullptr;
        }
        
        m_isListing = false;
        m_isDownloading = false;
        
        if (!m_isMultiTransfer) {
            emit dataReceived("ftp> ");
        }
    }
    // Rename sequence
    else if (response.startsWith("350") && !m_renameFrom.isEmpty()) {
        QString toName = m_commandBuffer.section(' ', 2);
        sendCommand(QString("RNTO %1").arg(toName));
        m_renameFrom.clear();
    }
    // Error responses
    else if (response.startsWith("530")) {
        emit dataReceived("\r\nLogin failed. Please check your credentials.\r\n");
        m_authState = AuthNone;
        emit dataReceived("ftp> ");
    } else if (response.startsWith("550")) {
        emit dataReceived("ftp> ");
    } else if (response.startsWith("421")) {
        emit dataReceived("\r\nService not available.\r\n");
    }
    // Generic response with prompt
    else if (m_authenticated && response.length() >= 4) {
        QChar fourthChar = response[3];
        if (fourthChar == ' ' && !response.startsWith("125") && !response.startsWith("150")) {
            emit dataReceived("ftp> ");
        }
    }
}

void FtpHandler::sendCommand(const QString &command)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        qDebug() << "[FtpHandler] Sending:" << command;
        m_socket->write((command + "\r\n").toUtf8());
        m_socket->flush();
    }
}

QString FtpHandler::formatSize(qint64 bytes) const
{
    if (bytes < 1024) return QString("%1 bytes").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 2);
    if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString FtpHandler::formatTransferSpeed(qint64 bytes, qint64 ms) const
{
    if (ms <= 0) return "0 B/s";
    double seconds = ms / 1000.0;
    double bps = bytes / seconds;
    if (bps < 1024) return QString("%1 B/s").arg(bps, 0, 'f', 0);
    if (bps < 1024 * 1024) return QString("%1 KB/s").arg(bps / 1024, 0, 'f', 1);
    return QString("%1 MB/s").arg(bps / (1024 * 1024), 0, 'f', 1);
}

bool FtpHandler::handleLocalCommand(const QString &command)
{
    QString cmd = command.toLower().section(' ', 0, 0);
    QString args = command.section(' ', 1).trimmed();

    if (cmd == "help" || cmd == "?") {
        showLocalHelp();
        return true;
    }

    if (cmd == "lcd") {
        if (args.isEmpty()) {
            emit dataReceived(QString("Local directory: %1\r\n").arg(m_localDir).toUtf8());
        } else {
            QDir dir(args);
            if (dir.exists() || QDir().mkpath(args)) {
                m_localDir = QDir(args).absolutePath();
                emit dataReceived(QString("Local directory now: %1\r\n").arg(m_localDir).toUtf8());
            } else {
                emit dataReceived(QString("Directory not found: %1\r\n").arg(args).toUtf8());
            }
        }
        return true;
    }

    if (cmd == "lls" || cmd == "ldir") {
        QString path = args.isEmpty() ? m_localDir : args;
        QDir dir(path);
        if (dir.exists()) {
            QString result = QString("\r\nListing %1:\r\n").arg(path);
            for (const QFileInfo &fi : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name | QDir::DirsFirst)) {
                QString type = fi.isDir() ? "<DIR>" : formatSize(fi.size());
                result += QString("  %1  %2  %3\r\n")
                    .arg(fi.fileName(), -30)
                    .arg(type, -15)
                    .arg(fi.lastModified().toString("yyyy-MM-dd hh:mm"));
            }
            emit dataReceived(result.toUtf8());
        } else {
            emit dataReceived(QString("Directory not found: %1\r\n").arg(path).toUtf8());
        }
        return true;
    }

    if (cmd == "lpwd") {
        emit dataReceived(QString("Local directory: %1\r\n").arg(m_localDir).toUtf8());
        return true;
    }

    if (cmd == "passive") {
        m_passiveMode = !m_passiveMode;
        emit dataReceived(QString("Passive mode %1.\r\n").arg(m_passiveMode ? "on" : "off").toUtf8());
        return true;
    }

    if (cmd == "prompt") {
        m_promptMode = !m_promptMode;
        emit dataReceived(QString("Interactive mode %1.\r\n").arg(m_promptMode ? "on" : "off").toUtf8());
        return true;
    }

    if (cmd == "verbose") {
        m_verboseMode = !m_verboseMode;
        emit dataReceived(QString("Verbose mode %1.\r\n").arg(m_verboseMode ? "on" : "off").toUtf8());
        return true;
    }

    if (cmd == "hash") {
        m_hashMode = !m_hashMode;
        emit dataReceived(QString("Hash mark printing %1 (%2 bytes/hash mark).\r\n")
            .arg(m_hashMode ? "on" : "off").arg(m_hashBlockSize).toUtf8());
        return true;
    }

    if (cmd == "bell") {
        emit dataReceived("Bell mode not supported.\r\n");
        return true;
    }

    if (cmd == "glob") {
        emit dataReceived("Globbing always enabled.\r\n");
        return true;
    }

    if (cmd == "!") {
        emit dataReceived("Use lcd, lls, lpwd for local operations.\r\n");
        return true;
    }

    return false;
}

void FtpHandler::showLocalHelp()
{
    QString help = "\r\n";
    help += "Commands may be abbreviated.  Commands are:\r\n";
    help += "\r\n";
    help += "Local commands:\r\n";
    help += "  !           escape to the local shell\r\n";
    help += "  $           execute a macro\r\n";
    help += "  lcd <dir>   change local working directory\r\n";
    help += "  lls [dir]   list contents of local directory\r\n";
    help += "  lpwd        print local working directory\r\n";
    help += "  prompt      toggle interactive prompting\r\n";
    help += "  verbose     toggle verbose mode\r\n";
    help += "  hash        toggle hash sign (#) printing\r\n";
    help += "\r\n";
    help += "Remote commands:\r\n";
    help += "  ascii       set ascii transfer type\r\n";
    help += "  bell        beep when command completed\r\n";
    help += "  binary      set binary transfer type\r\n";
    help += "  bye         terminate ftp session and exit\r\n";
    help += "  cd <dir>    change remote working directory\r\n";
    help += "  cdup        change remote working directory to parent\r\n";
    help += "  chmod       change file permissions\r\n";
    help += "  close       terminate ftp session\r\n";
    help += "  delete      delete remote file\r\n";
    help += "  dir [dir]   list contents of remote directory\r\n";
    help += "  exit        terminate ftp session and exit\r\n";
    help += "  get         receive file\r\n";
    help += "  help        print local help information\r\n";
    help += "  ls [dir]    list contents of remote directory\r\n";
    help += "  mdelete     delete multiple files\r\n";
    help += "  mget        get multiple files\r\n";
    help += "  mkdir       make directory on the remote machine\r\n";
    help += "  mput        send multiple files\r\n";
    help += "  nlist       nlist contents of remote directory\r\n";
    help += "  open        connect to remote ftp\r\n";
    help += "  put         send one file\r\n";
    help += "  pwd         print working directory on remote machine\r\n";
    help += "  quit        terminate ftp session and exit\r\n";
    help += "  quote       send arbitrary ftp command\r\n";
    help += "  rename      rename file\r\n";
    help += "  rmdir       remove directory on the remote machine\r\n";
    help += "  size        show size of remote file\r\n";
    help += "  status      show current status\r\n";
    help += "  system      show remote system type\r\n";
    help += "  type        set file transfer type\r\n";
    help += "  user        send new user information\r\n";
    help += "  ?           print local help information\r\n";
    emit dataReceived(help.toUtf8());
}

void FtpHandler::setupPassiveMode(const QString &response)
{
    QRegularExpression re("\\((\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)\\)");
    QRegularExpressionMatch match = re.match(response);

    if (!match.hasMatch()) {
        emit dataReceived("\r\nFailed to parse passive mode response.\r\n");
        emit dataReceived("ftp> ");
        return;
    }

    QString host = QString("%1.%2.%3.%4")
        .arg(match.captured(1)).arg(match.captured(2))
        .arg(match.captured(3)).arg(match.captured(4));

    int port = match.captured(5).toInt() * 256 + match.captured(6).toInt();

    if (m_dataSocket) {
        m_dataSocket->disconnectFromHost();
        m_dataSocket->deleteLater();
    }

    m_dataSocket = new QTcpSocket(this);
    m_dataBuffer.clear();

    connect(m_dataSocket, &QTcpSocket::connected, this, &FtpHandler::onDataConnected);
    connect(m_dataSocket, &QTcpSocket::readyRead, this, &FtpHandler::onDataReadyRead);
    connect(m_dataSocket, &QTcpSocket::disconnected, this, &FtpHandler::onDataDisconnected);
    connect(m_dataSocket, &QTcpSocket::bytesWritten, this, &FtpHandler::onBytesWritten);

    m_dataSocket->connectToHost(host, port);
}