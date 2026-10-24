#include "files/sshfileclient.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <fcntl.h>

// 注意：O_CREAT/O_TRUNC 必须使用当前平台的 <fcntl.h> 定义。
// libssh 与应用程序在同一平台上编译，双方对 flags 的取值必须一致；
// 在 Windows/MinGW 下 O_CREAT 是 0x0100，而不是 Linux 的 0100。

// libssh 将 SCP API 标记为 deprecated，但当前版本仍是唯一可用的 SCP 实现；
// 这里只关闭该文件的弃用警告，SCP 功能本身保持可用。
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace {

#ifndef S_IRUSR
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001
#endif

constexpr int kBlockSize = 64 * 1024;

QString formatPermissions(uint32_t mode)
{
    QString permissions;
    permissions += (mode & S_IRUSR) ? QLatin1Char('r') : QLatin1Char('-');
    permissions += (mode & S_IWUSR) ? QLatin1Char('w') : QLatin1Char('-');
    permissions += (mode & S_IXUSR) ? QLatin1Char('x') : QLatin1Char('-');
    permissions += (mode & S_IRGRP) ? QLatin1Char('r') : QLatin1Char('-');
    permissions += (mode & S_IWGRP) ? QLatin1Char('w') : QLatin1Char('-');
    permissions += (mode & S_IXGRP) ? QLatin1Char('x') : QLatin1Char('-');
    permissions += (mode & S_IROTH) ? QLatin1Char('r') : QLatin1Char('-');
    permissions += (mode & S_IWOTH) ? QLatin1Char('w') : QLatin1Char('-');
    permissions += (mode & S_IXOTH) ? QLatin1Char('x') : QLatin1Char('-');
    return permissions;
}

} // namespace

SshFileClient::SshFileClient(const Common::ConnectionSettings &settings, bool useScp)
    : m_settings(settings)
    , m_useScp(useScp)
{
}

SshFileClient::~SshFileClient()
{
    disconnect();
}

QString SshFileClient::lastError() const
{
    return m_session ? QString::fromUtf8(ssh_get_error(m_session)) : QString();
}

bool SshFileClient::connect(QString *home, QString *error)
{
    m_session = ssh_new();
    if (!m_session) {
        *error = QStringLiteral("无法创建 SSH 会话");
        return false;
    }

    const QByteArray host = m_settings.host.toUtf8();
    const QByteArray user = m_settings.username.toUtf8();
    unsigned int port = m_settings.port;
    ssh_options_set(m_session, SSH_OPTIONS_HOST, host.constData());
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(m_session, SSH_OPTIONS_USER, user.constData());
    long timeout = 20;
    ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT, &timeout);
    long timeoutUsec = 0;
    ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT_USEC, &timeoutUsec);

    if (ssh_connect(m_session) != SSH_OK) {
        *error = QStringLiteral("连接失败：%1").arg(lastError());
        disconnect();
        return false;
    }

    if (!authenticate(error)) {
        disconnect();
        return false;
    }

    if (!setupSftp(error)) {
        disconnect();
        return false;
    }

    char *cwd = sftp_canonicalize_path(m_sftp, ".");
    if (cwd) {
        *home = QString::fromUtf8(cwd);
        ssh_string_free_char(cwd);
    } else {
        *home = QStringLiteral("/");
    }
    return true;
}

void SshFileClient::disconnect()
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
}

bool SshFileClient::authenticate(QString *error)
{
    if (m_settings.useSshKey && !m_settings.sshKeyPath.isEmpty()) {
        ssh_key key = nullptr;
        const QByteArray path = m_settings.sshKeyPath.toUtf8();
        if (ssh_pki_import_privkey_file(path.constData(), nullptr, nullptr, nullptr,
                                        &key) != SSH_OK) {
            *error = QStringLiteral("无法加载 SSH 私钥：%1").arg(m_settings.sshKeyPath);
            return false;
        }
        const int rc = ssh_userauth_publickey(m_session, nullptr, key);
        ssh_key_free(key);
        if (rc != SSH_AUTH_SUCCESS) {
            *error = QStringLiteral("SSH 密钥认证失败：%1").arg(lastError());
            return false;
        }
    } else {
        const QByteArray password = m_settings.password.toUtf8();
        const int rc = ssh_userauth_password(m_session, nullptr, password.constData());
        if (rc != SSH_AUTH_SUCCESS) {
            *error = QStringLiteral("SSH 密码认证失败：%1").arg(lastError());
            return false;
        }
    }
    return true;
}

bool SshFileClient::setupSftp(QString *error)
{
    m_sftp = sftp_new(m_session);
    if (!m_sftp) {
        *error = QStringLiteral("无法创建 SFTP 会话：%1").arg(lastError());
        return false;
    }
    if (sftp_init(m_sftp) != SSH_OK) {
        *error = QStringLiteral("SFTP 初始化失败（服务器可能未开启 SFTP 子系统）：%1")
                     .arg(lastError());
        return false;
    }
    return true;
}

QString SshFileClient::joinRemote(const QString &directory, const QString &name) const
{
    QString base = directory;
    if (base.isEmpty()) {
        base = QStringLiteral(".");
    }
    if (base == QLatin1String("/")) {
        return base + name;
    }
    if (base.endsWith(QLatin1Char('/'))) {
        return base + name;
    }
    return base + QLatin1Char('/') + name;
}

bool SshFileClient::listDirectory(const QString &path,
                                  QVector<RemoteFileEntry> *entries,
                                  QString *error)
{
    if (!m_sftp) {
        *error = QStringLiteral("SFTP 会话未建立");
        return false;
    }

    sftp_dir dir = sftp_opendir(m_sftp, path.toUtf8().constData());
    if (!dir) {
        *error = QStringLiteral("无法打开目录 %1：%2").arg(path, lastError());
        return false;
    }

    entries->clear();
    sftp_attributes attr = nullptr;
    while ((attr = sftp_readdir(m_sftp, dir)) != nullptr) {
        const QString name = QString::fromUtf8(attr->name);
        if (name == QLatin1String(".") || name == QLatin1String("..")) {
            sftp_attributes_free(attr);
            continue;
        }

        RemoteFileEntry entry;
        entry.name = name;
        entry.path = joinRemote(path, name);
        entry.isDir = attr->type == SSH_FILEXFER_TYPE_DIRECTORY;
        entry.isSymlink = attr->type == SSH_FILEXFER_TYPE_SYMLINK;
        entry.size = qint64(attr->size);
        entry.modified = QDateTime::fromSecsSinceEpoch(qint64(attr->mtime));
        entry.permissions = formatPermissions(attr->permissions);
        entry.owner = attr->owner ? QString::fromUtf8(attr->owner)
                                  : QString::number(attr->uid);
        entry.group = attr->group ? QString::fromUtf8(attr->group)
                                  : QString::number(attr->gid);
        entry.mode = int(attr->permissions & 0777);
        sftp_attributes_free(attr);
        attr = nullptr;

        if (entry.isSymlink) {
            sftp_attributes target =
                sftp_stat(m_sftp, entry.path.toUtf8().constData());
            if (target) {
                entry.isDir = target->type == SSH_FILEXFER_TYPE_DIRECTORY;
                entry.size = qint64(target->size);
                sftp_attributes_free(target);
            }
        }

        entries->append(entry);
    }
    sftp_closedir(dir);

    std::sort(entries->begin(), entries->end(),
              [](const RemoteFileEntry &a, const RemoteFileEntry &b) {
                  if (a.isDir != b.isDir) {
                      return a.isDir;
                  }
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
    return true;
}

bool SshFileClient::downloadFile(const QString &remotePath,
                                 const QString &localPath,
                                 const Progress &progress, QString *error)
{
    return m_useScp ? downloadScp(remotePath, localPath, progress, error)
                    : downloadSftp(remotePath, localPath, progress, error);
}

bool SshFileClient::uploadFile(const QString &localPath,
                               const QString &remotePath,
                               const Progress &progress, QString *error)
{
    return m_useScp ? uploadScp(localPath, remotePath, progress, error)
                    : uploadSftp(localPath, remotePath, progress, error);
}

bool SshFileClient::downloadSftp(const QString &remotePath,
                                 const QString &localPath,
                                 const Progress &progress, QString *error)
{
    sftp_attributes attr =
        sftp_stat(m_sftp, remotePath.toUtf8().constData());
    qint64 total = attr ? qint64(attr->size) : -1;
    if (attr) {
        sftp_attributes_free(attr);
    }

    sftp_file remote =
        sftp_open(m_sftp, remotePath.toUtf8().constData(), O_RDONLY, 0);
    if (!remote) {
        *error = QStringLiteral("无法打开远端文件 %1：%2").arg(remotePath, lastError());
        return false;
    }

    QFile local(localPath);
    if (!local.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sftp_close(remote);
        *error = QStringLiteral("无法创建本地文件 %1：%2").arg(localPath, local.errorString());
        return false;
    }

    if (!progress(0, total)) {
        sftp_close(remote);
        local.close();
        local.remove();
        *error = QStringLiteral("已取消");
        return false;
    }

    QByteArray buffer(kBlockSize, 0);
    qint64 done = 0;
    bool ok = true;
    while (true) {
        const int read = sftp_read(remote, buffer.data(), buffer.size());
        if (read < 0) {
            *error = QStringLiteral("读取远端文件失败：%1").arg(lastError());
            ok = false;
            break;
        }
        if (read == 0) {
            break;
        }
        if (local.write(buffer.constData(), read) != read) {
            *error = QStringLiteral("写入本地文件失败：%1").arg(local.errorString());
            ok = false;
            break;
        }
        done += read;
        if (!progress(done, total)) {
            *error = QStringLiteral("已取消");
            ok = false;
            break;
        }
    }

    local.close();
    sftp_close(remote);
    if (!ok) {
        local.remove();
    }
    return ok;
}

bool SshFileClient::uploadSftp(const QString &localPath,
                               const QString &remotePath,
                               const Progress &progress, QString *error)
{
    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法打开本地文件 %1：%2").arg(localPath, local.errorString());
        return false;
    }
    const qint64 total = local.size();

    sftp_file remote = sftp_open(m_sftp, remotePath.toUtf8().constData(),
                                 O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!remote) {
        local.close();
        *error = QStringLiteral("无法创建远端文件 %1：%2").arg(remotePath, lastError());
        return false;
    }

    if (!progress(0, total)) {
        sftp_close(remote);
        local.close();
        *error = QStringLiteral("已取消");
        return false;
    }

    qint64 done = 0;
    bool ok = true;
    while (!local.atEnd()) {
        const QByteArray chunk = local.read(kBlockSize);
        int offset = 0;
        while (offset < chunk.size()) {
            const int written = sftp_write(remote, chunk.constData() + offset,
                                           size_t(chunk.size() - offset));
            if (written <= 0) {
                *error = QStringLiteral("写入远端文件失败：%1").arg(lastError());
                ok = false;
                break;
            }
            offset += written;
            done += written;
        }
        if (!ok) {
            break;
        }
        if (!progress(done, total)) {
            *error = QStringLiteral("已取消");
            ok = false;
            break;
        }
    }

    local.close();
    sftp_close(remote);
    if (!ok) {
        sftp_unlink(m_sftp, remotePath.toUtf8().constData());
    }
    return ok;
}

bool SshFileClient::downloadScp(const QString &remotePath,
                                const QString &localPath,
                                const Progress &progress, QString *error)
{
    ssh_scp scp = ssh_scp_new(m_session, SSH_SCP_READ, remotePath.toUtf8().constData());
    if (!scp) {
        *error = QStringLiteral("无法创建 SCP 会话：%1").arg(lastError());
        return false;
    }
    if (ssh_scp_init(scp) != SSH_OK) {
        *error = QStringLiteral("SCP 初始化失败：%1").arg(lastError());
        ssh_scp_free(scp);
        return false;
    }

    const int request = ssh_scp_pull_request(scp);
    if (request != SSH_SCP_REQUEST_NEWFILE) {
        *error = QStringLiteral("SCP 请求失败：%1").arg(lastError());
        ssh_scp_close(scp);
        ssh_scp_free(scp);
        return false;
    }

    const qint64 total = qint64(ssh_scp_request_get_size64(scp));
    ssh_scp_accept_request(scp);

    QFile local(localPath);
    if (!local.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("无法创建本地文件 %1：%2").arg(localPath, local.errorString());
        ssh_scp_close(scp);
        ssh_scp_free(scp);
        return false;
    }

    if (!progress(0, total)) {
        ssh_scp_close(scp);
        ssh_scp_free(scp);
        local.close();
        local.remove();
        *error = QStringLiteral("已取消");
        return false;
    }

    QByteArray buffer(kBlockSize, 0);
    qint64 done = 0;
    bool ok = true;
    while (done < total) {
        const int want = int(qMin<qint64>(buffer.size(), total - done));
        const int read = ssh_scp_read(scp, buffer.data(), size_t(want));
        if (read < 0) {
            *error = QStringLiteral("SCP 读取失败：%1").arg(lastError());
            ok = false;
            break;
        }
        if (read == 0) {
            if (done < total) {
                *error = QStringLiteral("SCP 读取提前结束");
                ok = false;
            }
            break;
        }
        if (local.write(buffer.constData(), read) != read) {
            *error = QStringLiteral("写入本地文件失败：%1").arg(local.errorString());
            ok = false;
            break;
        }
        done += read;
        if (!progress(done, total)) {
            *error = QStringLiteral("已取消");
            ok = false;
            break;
        }
    }

    local.close();
    ssh_scp_close(scp);
    ssh_scp_free(scp);
    if (!ok) {
        local.remove();
    }
    return ok;
}

bool SshFileClient::uploadScp(const QString &localPath,
                              const QString &remotePath,
                              const Progress &progress, QString *error)
{
    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法打开本地文件 %1：%2").arg(localPath, local.errorString());
        return false;
    }
    const qint64 total = local.size();

    QString directory = QStringLiteral(".");
    QString name = remotePath;
    const int slash = remotePath.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) {
        directory = remotePath.left(slash);
        name = remotePath.mid(slash + 1);
        if (directory.isEmpty()) {
            directory = QStringLiteral("/");
        }
    }
    if (name.isEmpty()) {
        local.close();
        *error = QStringLiteral("远端目标文件名无效");
        return false;
    }

    ssh_scp scp = ssh_scp_new(m_session, SSH_SCP_WRITE, directory.toUtf8().constData());
    if (!scp) {
        local.close();
        *error = QStringLiteral("无法创建 SCP 会话：%1").arg(lastError());
        return false;
    }
    if (ssh_scp_init(scp) != SSH_OK) {
        local.close();
        *error = QStringLiteral("SCP 初始化失败：%1").arg(lastError());
        ssh_scp_free(scp);
        return false;
    }
    if (ssh_scp_push_file64(scp, name.toUtf8().constData(), size_t(total), 0644)
        != SSH_OK) {
        local.close();
        *error = QStringLiteral("SCP 上传请求失败：%1").arg(lastError());
        ssh_scp_close(scp);
        ssh_scp_free(scp);
        return false;
    }

    if (!progress(0, total)) {
        local.close();
        ssh_scp_close(scp);
        ssh_scp_free(scp);
        *error = QStringLiteral("已取消");
        return false;
    }

    qint64 done = 0;
    bool ok = true;
    while (!local.atEnd()) {
        const QByteArray chunk = local.read(kBlockSize);
        if (ssh_scp_write(scp, chunk.constData(), size_t(chunk.size())) != SSH_OK) {
            *error = QStringLiteral("SCP 写入失败：%1").arg(lastError());
            ok = false;
            break;
        }
        done += chunk.size();
        if (!progress(done, total)) {
            *error = QStringLiteral("已取消");
            ok = false;
            break;
        }
    }

    local.close();
    ssh_scp_close(scp);
    ssh_scp_free(scp);
    return ok;
}

bool SshFileClient::makeDirectory(const QString &path, QString *error)
{
    if (sftp_mkdir(m_sftp, path.toUtf8().constData(), 0755) == SSH_OK) {
        return true;
    }

    // 已存在时视为成功，便于递归上传
    sftp_attributes attr = sftp_stat(m_sftp, path.toUtf8().constData());
    if (attr) {
        const bool isDir = attr->type == SSH_FILEXFER_TYPE_DIRECTORY;
        sftp_attributes_free(attr);
        if (isDir) {
            return true;
        }
    }
    *error = QStringLiteral("创建远端目录失败 %1：%2").arg(path, lastError());
    return false;
}

bool SshFileClient::removeEntry(const RemoteTransferItem &item, QString *error)
{
    const int rc = item.isDir
                       ? sftp_rmdir(m_sftp, item.path.toUtf8().constData())
                       : sftp_unlink(m_sftp, item.path.toUtf8().constData());
    if (rc != SSH_OK) {
        *error = QStringLiteral("删除 %1 失败：%2").arg(item.path, lastError());
        return false;
    }
    return true;
}

bool SshFileClient::renameEntry(const QString &from, const QString &to,
                                QString *error)
{
    if (sftp_rename(m_sftp, from.toUtf8().constData(), to.toUtf8().constData())
        != SSH_OK) {
        *error = QStringLiteral("重命名失败：%1").arg(lastError());
        return false;
    }
    return true;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
