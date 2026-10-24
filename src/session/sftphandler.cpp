#include "sftphandler.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <fcntl.h>

// Windows compatibility - define POSIX permission and file flags if not present
#ifndef S_IRUSR
#define S_IRUSR  00400
#define S_IWUSR  00200
#define S_IXUSR  00100
#define S_IRGRP  00040
#define S_IWGRP  00020
#define S_IXGRP  00010
#define S_IROTH  00004
#define S_IWOTH  00002
#define S_IXOTH  00001
#endif

SftpHandler::SftpHandler(const Common::ConnectionSettings &settings, QObject *parent)
    : ConnectionHandler(parent)
    , m_session(nullptr)
    , m_sftp(nullptr)
    , m_settings(settings)
    , m_notifier(nullptr)
    , m_connected(false)
    , m_localDir(QDir::homePath())
    , m_transferBlockSize(32768)
{
}

SftpHandler::~SftpHandler()
{
    close();
}

bool SftpHandler::open()
{
    if (m_connected) {
        return true;
    }

    m_session = ssh_new();
    if (!m_session) {
        emit errorOccurred("Failed to create SSH session");
        return false;
    }

    ssh_options_set(m_session, SSH_OPTIONS_HOST, m_settings.host.toUtf8().constData());
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &m_settings.port);
    ssh_options_set(m_session, SSH_OPTIONS_USER, m_settings.username.toUtf8().constData());

    int rc = ssh_connect(m_session);
    if (rc != SSH_OK) {
        QString error = QString("Connection failed: %1").arg(ssh_get_error(m_session));
        emit errorOccurred(error);
        cleanup();
        return false;
    }

    if (!authenticate()) {
        cleanup();
        return false;
    }

    if (!setupSftp()) {
        cleanup();
        return false;
    }

    // Get initial remote directory
    char *cwd = sftp_canonicalize_path(m_sftp, ".");
    if (cwd) {
        m_remoteDir = QString::fromUtf8(cwd);
        ssh_string_free_char(cwd);
    }

    m_connected = true;
    emit connected();
    emit dataReceived(QString("Connected to %1.\r\n").arg(m_settings.host).toUtf8());
    emit dataReceived("sftp> ");
    return true;
}

void SftpHandler::close()
{
    cleanup();
}

bool SftpHandler::isOpen() const
{
    return m_session != nullptr;
}

bool SftpHandler::isConnected() const
{
    return m_connected;
}

void SftpHandler::sendData(const QByteArray &data)
{
    if (!m_connected || !m_sftp) {
        return;
    }

    QString input = QString::fromUtf8(data);

    for (const QChar &ch : input) {
        if (ch == '\r' || ch == '\n') {
            QString command = m_commandBuffer.trimmed();
            m_commandBuffer.clear();
            emit dataReceived("\r\n");

            if (command.isEmpty()) {
                emit dataReceived("sftp> ");
                continue;
            }

            processCommand(command);
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

QString SftpHandler::connectionInfo() const
{
    return QString("SFTP - %1:%2").arg(m_settings.host).arg(m_settings.port);
}

void SftpHandler::setTerminalSize(int columns, int rows)
{
    Q_UNUSED(columns);
    Q_UNUSED(rows);
}

void SftpHandler::onSocketActivated(int socket)
{
    Q_UNUSED(socket);
}

void SftpHandler::processCommand(const QString &command)
{
    QStringList parts = command.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        emit dataReceived("sftp> ");
        return;
    }

    QString cmd = parts[0].toLower();
    QStringList args = parts.mid(1);

    // Check if it's a local command first
    if (handleLocalCommand(cmd, args)) {
        emit dataReceived("sftp> ");
        return;
    }

    // SFTP remote commands
    if (cmd == "ls" || cmd == "dir") {
        QString path = args.isEmpty() ? "." : args[0];
        doListDirectory(path, true);
    } else if (cmd == "lls" || cmd == "ldir") {
        QString path = args.isEmpty() ? m_localDir : args[0];
        QDir dir(path);
        if (dir.exists()) {
            QString result = QString("Listing %1:\r\n").arg(path);
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
    } else if (cmd == "cd") {
        QString path = args.isEmpty() ? QDir::homePath() : args[0];
        doChangeDirectory(path);
    } else if (cmd == "pwd") {
        doPrintWorkingDirectory();
    } else if (cmd == "lcd") {
        QString path = args.isEmpty() ? QDir::homePath() : args[0];
        QDir dir(path);
        if (dir.exists()) {
            m_localDir = dir.absolutePath();
            emit dataReceived(QString("Local directory now: %1\r\n").arg(m_localDir).toUtf8());
        } else {
            emit dataReceived(QString("Directory not found: %1\r\n").arg(path).toUtf8());
        }
    } else if (cmd == "lpwd") {
        emit dataReceived(QString("Local directory: %1\r\n").arg(m_localDir).toUtf8());
    } else if (cmd == "mkdir") {
        if (args.isEmpty()) {
            emit dataReceived("Usage: mkdir <path>\r\n");
        } else {
            doMakeDirectory(args[0]);
        }
    } else if (cmd == "rmdir") {
        if (args.isEmpty()) {
            emit dataReceived("Usage: rmdir <path>\r\n");
        } else {
            doRemoveDirectory(args[0]);
        }
    } else if (cmd == "rm" || cmd == "delete") {
        if (args.isEmpty()) {
            emit dataReceived("Usage: rm <file>\r\n");
        } else {
            doRemoveFile(args[0]);
        }
    } else if (cmd == "mv" || cmd == "rename") {
        if (args.size() < 2) {
            emit dataReceived("Usage: mv <old> <new>\r\n");
        } else {
            doRename(args[0], args[1]);
        }
    } else if (cmd == "get") {
        if (args.isEmpty()) {
            emit dataReceived("Usage: get <remote> [local]\r\n");
        } else {
            QString remotePath = args[0];
            QString localPath = args.size() > 1 ? args[1] : QFileInfo(remotePath).fileName();
            localPath = QDir(m_localDir).filePath(localPath);
            doGetFile(remotePath, localPath);
        }
    } else if (cmd == "mget") {
        if (args.isEmpty()) {
            emit dataReceived("Usage: mget <files...>\r\n");
        } else {
            doGetMultipleFiles(args);
        }
    } else if (cmd == "put") {
        if (args.isEmpty()) {
            emit dataReceived("Usage: put <local> [remote]\r\n");
        } else {
            QString localPath = args[0];
            localPath = QDir(m_localDir).filePath(localPath);
            QString remotePath = args.size() > 1 ? args[1] : QFileInfo(localPath).fileName();
            doPutFile(localPath, remotePath);
        }
    } else if (cmd == "mput") {
        if (args.isEmpty()) {
            emit dataReceived("Usage: mput <files...>\r\n");
        } else {
            QStringList files;
            for (const QString &pattern : args) {
                files.append(expandLocalFiles(pattern));
            }
            doPutMultipleFiles(files);
        }
    } else if (cmd == "stat") {
        if (args.isEmpty()) {
            emit dataReceived("Usage: stat <path>\r\n");
        } else {
            doStat(args[0]);
        }
    } else if (cmd == "chmod") {
        if (args.size() < 2) {
            emit dataReceived("Usage: chmod <mode> <path>\r\n");
        } else {
            int mode = args[0].toInt(nullptr, 8);
            int rc = sftp_chmod(m_sftp, args[1].toUtf8().constData(), mode);
            if (rc != SSH_OK) {
                emit dataReceived(QString("chmod failed: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
            } else {
                emit dataReceived(QString("Mode changed to %1 for %2\r\n").arg(args[0], args[1]).toUtf8());
            }
        }
    } else if (cmd == "chown") {
        if (args.size() < 2) {
            emit dataReceived("Usage: chown <owner> <path>\r\n");
        } else {
            int owner = args[0].toInt();
            int rc = sftp_chown(m_sftp, args[1].toUtf8().constData(), owner, -1);
            if (rc != SSH_OK) {
                emit dataReceived(QString("chown failed: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
            } else {
                emit dataReceived(QString("Owner changed to %1 for %2\r\n").arg(args[0], args[1]).toUtf8());
            }
        }
    } else if (cmd == "ln" || cmd == "symlink") {
        if (args.size() < 2) {
            emit dataReceived("Usage: ln <target> <link>\r\n");
        } else {
            int rc = sftp_symlink(m_sftp, args[0].toUtf8().constData(), args[1].toUtf8().constData());
            if (rc != SSH_OK) {
                emit dataReceived(QString("symlink failed: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
            } else {
                emit dataReceived(QString("Symlink created: %1 -> %2\r\n").arg(args[1], args[0]).toUtf8());
            }
        }
    } else if (cmd == "readlink") {
        if (args.isEmpty()) {
            emit dataReceived("Usage: readlink <path>\r\n");
        } else {
            char *target = sftp_readlink(m_sftp, args[0].toUtf8().constData());
            if (target) {
                emit dataReceived(QString("%1 -> %2\r\n").arg(args[0], QString::fromUtf8(target)).toUtf8());
                ssh_string_free_char(target);
            } else {
                emit dataReceived(QString("readlink failed: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
            }
        }
    } else if (cmd == "exit" || cmd == "quit" || cmd == "bye") {
        emit dataReceived("Closing SFTP session...\r\n");
        emit dataReceived("Connection closed.\r\n");
        // Close connection - this will trigger disconnected signal
        cleanup();
        return;  // Don't show prompt after quit
    } else if (cmd == "clear") {
        emit dataReceived("\x1b[2J\x1b[H");
    } else if (cmd == "help" || cmd == "?") {
        showHelp();
    } else {
        emit dataReceived(QString("Unknown command: %1. Type 'help' for available commands.\r\n").arg(cmd).toUtf8());
    }

    emit dataReceived("sftp> ");
}

bool SftpHandler::handleLocalCommand(const QString &cmd, const QStringList &args)
{
    Q_UNUSED(cmd);
    Q_UNUSED(args);
    
    // These are handled in processCommand with remote commands
    // This function is for purely local commands that don't need the sftp> prompt
    
    return false;
}

void SftpHandler::showHelp()
{
    QString help = "\r\n";
    help += "Available commands:\r\n";
    help += "\r\n";
    help += "Remote directory operations:\r\n";
    help += "  cd <path>        change remote working directory\r\n";
    help += "  pwd              print remote working directory\r\n";
    help += "  ls [path]        list contents of remote directory\r\n";
    help += "  mkdir <path>     create directory on remote server\r\n";
    help += "  rmdir <path>     remove directory on remote server\r\n";
    help += "\r\n";
    help += "Local directory operations:\r\n";
    help += "  lcd <path>       change local working directory\r\n";
    help += "  lpwd             print local working directory\r\n";
    help += "  lls [path]       list contents of local directory\r\n";
    help += "\r\n";
    help += "File transfer:\r\n";
    help += "  get <remote> [local]   download file\r\n";
    help += "  put <local> [remote]   upload file\r\n";
    help += "  mget <files...>        download multiple files\r\n";
    help += "  mput <files...>        upload multiple files\r\n";
    help += "\r\n";
    help += "File management:\r\n";
    help += "  rm <file>        delete remote file\r\n";
    help += "  mv <old> <new>   move/rename remote file\r\n";
    help += "  chmod <mode> <file>  change file permissions\r\n";
    help += "  chown <owner> <file> change file owner\r\n";
    help += "  stat <path>      show file information\r\n";
    help += "\r\n";
    help += "Symlinks:\r\n";
    help += "  ln <target> <link>   create symbolic link\r\n";
    help += "  readlink <path>      read symbolic link target\r\n";
    help += "\r\n";
    help += "Other:\r\n";
    help += "  help / ?         show this help\r\n";
    help += "  clear            clear screen\r\n";
    help += "  exit / quit      close SFTP session\r\n";
    emit dataReceived(help.toUtf8());
}

QString SftpHandler::formatSize(qint64 bytes) const
{
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

QString SftpHandler::formatTransferSpeed(qint64 bytes, qint64 ms) const
{
    if (ms <= 0) return "0 B/s";
    double seconds = ms / 1000.0;
    double bps = bytes / seconds;
    if (bps < 1024) return QString("%1 B/s").arg(bps, 0, 'f', 0);
    if (bps < 1024 * 1024) return QString("%1 KB/s").arg(bps / 1024, 0, 'f', 1);
    return QString("%1 MB/s").arg(bps / (1024 * 1024), 0, 'f', 1);
}

QString SftpHandler::formatPermissions(uint32_t mode) const
{
    QString perms;
    perms += (mode & S_IRUSR) ? 'r' : '-';
    perms += (mode & S_IWUSR) ? 'w' : '-';
    perms += (mode & S_IXUSR) ? 'x' : '-';
    perms += (mode & S_IRGRP) ? 'r' : '-';
    perms += (mode & S_IWGRP) ? 'w' : '-';
    perms += (mode & S_IXGRP) ? 'x' : '-';
    perms += (mode & S_IROTH) ? 'r' : '-';
    perms += (mode & S_IWOTH) ? 'w' : '-';
    perms += (mode & S_IXOTH) ? 'x' : '-';
    return perms;
}

QString SftpHandler::resolveRemotePath(const QString &path)
{
    if (path.startsWith('/')) {
        return path;
    }
    return m_remoteDir + '/' + path;
}

void SftpHandler::doListDirectory(const QString &path, bool longFormat)
{
    QString resolvedPath = resolveRemotePath(path);
    sftp_dir dir = sftp_opendir(m_sftp, resolvedPath.toUtf8().constData());
    if (!dir) {
        emit dataReceived(QString("Cannot open directory: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
        return;
    }

    QString result;
    sftp_attributes attr;
    while ((attr = sftp_readdir(m_sftp, dir)) != nullptr) {
        if (longFormat) {
            QString type = (attr->type == SSH_FILEXFER_TYPE_DIRECTORY) ? "d" : "-";
            QString perms = formatPermissions(attr->permissions);
            QString size = (attr->type == SSH_FILEXFER_TYPE_DIRECTORY) ? "" : formatSize(attr->size);
            QString name = QString::fromUtf8(attr->name);
            
            // Add trailing slash for directories
            if (attr->type == SSH_FILEXFER_TYPE_DIRECTORY) {
                name += '/';
            }
            
            result += QString("%1%2 %3 %4 %5\r\n")
                .arg(type + perms, -12)
                .arg(attr->uid, -6)
                .arg(attr->gid, -6)
                .arg(size, -12)
                .arg(name);
        } else {
            QString name = QString::fromUtf8(attr->name);
            if (attr->type == SSH_FILEXFER_TYPE_DIRECTORY) {
                name += '/';
            }
            result += name + "\r\n";
        }
        sftp_attributes_free(attr);
    }
    sftp_closedir(dir);

    if (result.isEmpty()) {
        result = "Empty directory\r\n";
    }
    emit dataReceived(result.toUtf8());
}

void SftpHandler::doChangeDirectory(const QString &path)
{
    QString resolvedPath = resolveRemotePath(path);
    
    // Verify directory exists
    sftp_dir dir = sftp_opendir(m_sftp, resolvedPath.toUtf8().constData());
    if (!dir) {
        emit dataReceived(QString("Cannot change directory: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
        return;
    }
    sftp_closedir(dir);
    
    // Update remote directory
    char *cwd = sftp_canonicalize_path(m_sftp, resolvedPath.toUtf8().constData());
    if (cwd) {
        m_remoteDir = QString::fromUtf8(cwd);
        ssh_string_free_char(cwd);
        emit dataReceived(QString("Remote directory changed to: %1\r\n").arg(m_remoteDir).toUtf8());
    }
}

void SftpHandler::doPrintWorkingDirectory()
{
    char *cwd = sftp_canonicalize_path(m_sftp, ".");
    if (cwd) {
        emit dataReceived(QString("Remote working directory: %1\r\n").arg(QString::fromUtf8(cwd)).toUtf8());
        m_remoteDir = QString::fromUtf8(cwd);
        ssh_string_free_char(cwd);
    } else {
        emit dataReceived(QString("Remote working directory: %1\r\n").arg(m_remoteDir).toUtf8());
    }
}

void SftpHandler::doMakeDirectory(const QString &path)
{
    QString resolvedPath = resolveRemotePath(path);
    int rc = sftp_mkdir(m_sftp, resolvedPath.toUtf8().constData(), 0755);
    if (rc != SSH_OK) {
        emit dataReceived(QString("mkdir failed: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
    } else {
        emit dataReceived(QString("Directory created: %1\r\n").arg(resolvedPath).toUtf8());
    }
}

void SftpHandler::doRemoveDirectory(const QString &path)
{
    QString resolvedPath = resolveRemotePath(path);
    int rc = sftp_rmdir(m_sftp, resolvedPath.toUtf8().constData());
    if (rc != SSH_OK) {
        emit dataReceived(QString("rmdir failed: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
    } else {
        emit dataReceived(QString("Directory removed: %1\r\n").arg(resolvedPath).toUtf8());
    }
}

void SftpHandler::doRemoveFile(const QString &path)
{
    QString resolvedPath = resolveRemotePath(path);
    int rc = sftp_unlink(m_sftp, resolvedPath.toUtf8().constData());
    if (rc != SSH_OK) {
        emit dataReceived(QString("rm failed: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
    } else {
        emit dataReceived(QString("File removed: %1\r\n").arg(resolvedPath).toUtf8());
    }
}

void SftpHandler::doRename(const QString &oldPath, const QString &newPath)
{
    QString resolvedOld = resolveRemotePath(oldPath);
    QString resolvedNew = resolveRemotePath(newPath);
    
    int rc = sftp_rename(m_sftp, resolvedOld.toUtf8().constData(), resolvedNew.toUtf8().constData());
    if (rc != SSH_OK) {
        emit dataReceived(QString("rename failed: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
    } else {
        emit dataReceived(QString("Renamed %1 to %2\r\n").arg(resolvedOld, resolvedNew).toUtf8());
    }
}

void SftpHandler::doGetFile(const QString &remotePath, const QString &localPath)
{
    QString resolvedRemote = resolveRemotePath(remotePath);

    // Open remote file
    sftp_file remoteFile = sftp_open(m_sftp, resolvedRemote.toUtf8().constData(), O_RDONLY, 0);
    if (!remoteFile) {
        emit dataReceived(QString("Cannot open remote file: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
        return;
    }

    // Open local file
    QFile localFile(localPath);
    if (!localFile.open(QIODevice::WriteOnly)) {
        emit dataReceived(QString("Cannot create local file: %1\r\n").arg(localPath).toUtf8());
        sftp_close(remoteFile);
        return;
    }

    // Transfer file
    QElapsedTimer timer;
    timer.start();
    qint64 totalBytes = 0;
    QByteArray buffer(m_transferBlockSize, 0);

    emit dataReceived(QString("Downloading %1 to %2\r\n").arg(resolvedRemote, localPath).toUtf8());

    while (true) {
        int bytesRead = sftp_read(remoteFile, buffer.data(), buffer.size());
        if (bytesRead < 0) {
            emit dataReceived(QString("\r\nRead error: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
            break;
        }
        if (bytesRead == 0) break;

        localFile.write(buffer.constData(), bytesRead);
        totalBytes += bytesRead;
    }

    localFile.close();
    sftp_close(remoteFile);

    qint64 elapsed = timer.elapsed();
    QString speed = formatTransferSpeed(totalBytes, elapsed);
    emit dataReceived(QString("%1: %2 downloaded in %3s (%4)\r\n")
        .arg(QFileInfo(localPath).fileName())
        .arg(formatSize(totalBytes))
        .arg(elapsed / 1000.0, 0, 'f', 2)
        .arg(speed).toUtf8());
}

void SftpHandler::doPutFile(const QString &localPath, const QString &remotePath)
{
    // Check local file exists
    if (!QFile::exists(localPath)) {
        emit dataReceived(QString("Local file not found: %1\r\n").arg(localPath).toUtf8());
        return;
    }

    QString resolvedRemote = resolveRemotePath(remotePath);
    
    // Open local file
    QFile localFile(localPath);
    if (!localFile.open(QIODevice::ReadOnly)) {
        emit dataReceived(QString("Cannot open local file: %1\r\n").arg(localPath).toUtf8());
        return;
    }

    // Open remote file
    sftp_file remoteFile = sftp_open(m_sftp, resolvedRemote.toUtf8().constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!remoteFile) {
        emit dataReceived(QString("Cannot create remote file: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
        localFile.close();
        return;
    }

    // Transfer file
    QElapsedTimer timer;
    timer.start();
    qint64 totalBytes = 0;

    emit dataReceived(QString("Uploading %1 to %2\r\n").arg(localPath, resolvedRemote).toUtf8());

    while (!localFile.atEnd()) {
        QByteArray buffer = localFile.read(m_transferBlockSize);
        int bytesWritten = sftp_write(remoteFile, buffer.constData(), buffer.size());
        if (bytesWritten < 0) {
            emit dataReceived(QString("\r\nWrite error: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
            break;
        }
        totalBytes += bytesWritten;
    }

    localFile.close();
    sftp_close(remoteFile);

    qint64 elapsed = timer.elapsed();
    QString speed = formatTransferSpeed(totalBytes, elapsed);
    emit dataReceived(QString("%1: %2 uploaded in %3s (%4)\r\n")
        .arg(QFileInfo(localPath).fileName())
        .arg(formatSize(totalBytes))
        .arg(elapsed / 1000.0, 0, 'f', 2)
        .arg(speed).toUtf8());
}

void SftpHandler::doGetMultipleFiles(const QStringList &files)
{
    for (const QString &file : files) {
        QString localName = QFileInfo(file).fileName();
        QString localPath = QDir(m_localDir).filePath(localName);
        doGetFile(file, localPath);
    }
}

void SftpHandler::doPutMultipleFiles(const QStringList &files)
{
    for (const QString &file : files) {
        QString localPath = QDir(m_localDir).filePath(file);
        doPutFile(localPath, file);
    }
}

void SftpHandler::doStat(const QString &path)
{
    QString resolvedPath = resolveRemotePath(path);
    
    sftp_attributes attr = sftp_stat(m_sftp, resolvedPath.toUtf8().constData());
    if (!attr) {
        emit dataReceived(QString("stat failed: %1\r\n").arg(ssh_get_error(m_session)).toUtf8());
        return;
    }

    QString result;
    result += QString("File: %1\r\n").arg(resolvedPath);
    result += QString("Size: %1\r\n").arg(formatSize(attr->size));
    result += QString("Type: %1\r\n").arg(
        attr->type == SSH_FILEXFER_TYPE_DIRECTORY ? "Directory" :
        attr->type == SSH_FILEXFER_TYPE_SYMLINK ? "Symlink" :
        attr->type == SSH_FILEXFER_TYPE_REGULAR ? "Regular file" : "Unknown");
    result += QString("Permissions: %1 (%2)\r\n").arg(formatPermissions(attr->permissions)).arg(attr->permissions & 0777, 3, 8, QChar('0'));
    result += QString("UID: %1\r\n").arg(attr->uid);
    result += QString("GID: %1\r\n").arg(attr->gid);
    result += QString("Modified: %1\r\n").arg(QDateTime::fromSecsSinceEpoch(attr->mtime).toString("yyyy-MM-dd hh:mm:ss"));
    result += QString("Accessed: %1\r\n").arg(QDateTime::fromSecsSinceEpoch(attr->atime).toString("yyyy-MM-dd hh:mm:ss"));

    sftp_attributes_free(attr);
    emit dataReceived(result.toUtf8());
}

QStringList SftpHandler::expandLocalFiles(const QString &pattern)
{
    QStringList result;
    QDir dir(m_localDir);
    
    QStringList filters;
    filters << pattern;
    
    QStringList entries = dir.entryList(filters, QDir::Files);
    for (const QString &entry : entries) {
        result << entry;
    }
    
    if (result.isEmpty() && QFile::exists(QDir(m_localDir).filePath(pattern))) {
        result << pattern;
    }
    
    return result;
}

bool SftpHandler::authenticate()
{
    if (m_settings.useSshKey && !m_settings.sshKeyPath.isEmpty()) {
        ssh_key privkey;
        int rc = ssh_pki_import_privkey_file(m_settings.sshKeyPath.toUtf8().constData(),
                                             nullptr, nullptr, nullptr, &privkey);
        if (rc != SSH_OK) {
            emit errorOccurred("Failed to load private key");
            return false;
        }

        rc = ssh_userauth_publickey(m_session, nullptr, privkey);
        ssh_key_free(privkey);

        if (rc != SSH_AUTH_SUCCESS) {
            emit errorOccurred("Public key authentication failed");
            return false;
        }
    } else {
        int rc = ssh_userauth_password(m_session, nullptr, m_settings.password.toUtf8().constData());
        if (rc != SSH_AUTH_SUCCESS) {
            emit errorOccurred("Password authentication failed");
            return false;
        }
    }

    return true;
}

bool SftpHandler::setupSftp()
{
    m_sftp = sftp_new(m_session);
    if (!m_sftp) {
        emit errorOccurred("Failed to create SFTP session");
        return false;
    }

    int rc = sftp_init(m_sftp);
    if (rc != SSH_OK) {
        QString error = QString("SFTP initialization failed: %1").arg(ssh_get_error(m_session));
        emit errorOccurred(error);
        return false;
    }

    return true;
}

void SftpHandler::cleanup()
{
    if (m_sftp) {
        sftp_free(m_sftp);
        m_sftp = nullptr;
    }

    if (m_session) {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
    }

    if (m_notifier) {
        delete m_notifier;
        m_notifier = nullptr;
    }

    m_connected = false;
    emit disconnected();
}