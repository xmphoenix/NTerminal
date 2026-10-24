#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

// ============================================================================
// 自动更新管理器
//
// 流程：
//   1. GET 远程 update.json（静态 HTTPS/本地测试地址）
//   2. 按平台取下载信息，比较版本号（语义化版本）
//   3. 下载 zip 到临时目录并校验 SHA-256
//   4. 生成一个独立的更新脚本，等待 NTerminal 退出后解压覆盖安装目录并重启
//
// 更新包不包含用户数据（配置在 ~/.nterminal），无需重新安装。
// Windows 使用 PowerShell 脚本，其他平台使用 sh + unzip。
// ============================================================================

struct UpdateInfo {
    QString version;
    QString notes;
    QString published;
    QUrl url;
    QString sha256;
    qint64 size = -1;
    bool valid = false;
    QString error;
};

class UpdateManager : public QObject
{
    Q_OBJECT

public:
    explicit UpdateManager(QObject *parent = nullptr);
    ~UpdateManager() override;

    // 配置（QSettings：Update/FeedUrl、Update/AutoCheck）
    static QString feedUrl();
    static void setFeedUrl(const QString &url);
    static bool autoCheckEnabled();
    static void setAutoCheckEnabled(bool enabled);
    static QString currentVersion();
    static QString pendingUpdatePath();
    static QString updaterLogPath();

    // 纯函数：语义化版本比较（支持 1.2.3-beta.1 与 build metadata）
    static bool isNewerVersion(const QString &remote, const QString &local);
    static bool isSecureUrl(const QUrl &url);
    static UpdateInfo parseManifest(const QByteArray &data,
                                    const QUrl &manifestUrl,
                                    QString *error);

    bool isBusy() const;
    void checkForUpdates();
    void downloadUpdate(const UpdateInfo &info);
    void cancel();

    // 下载完成后调用。restartNow=true 时调用方应立即退出；
    // false 时写入待安装文件，下次启动 NTerminal 时自动安装。
    bool applyUpdate(const QString &zipPath, const UpdateInfo &info,
                     bool restartNow, QString *error);

    // 启动时调用：存在待安装包时启动更新脚本并返回 true（调用方应立即退出）
    static bool applyPendingUpdate();

signals:
    void checking();
    void noUpdate(const QString &currentVersion);
    void updateAvailable(const UpdateInfo &info);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString &zipPath, const UpdateInfo &info);
    void cancelled();
    void error(const QString &message);

private:
    enum class State { Idle, Checking, Downloading };

    void setState(State state);
    void fail(const QString &message);
    void onManifestFinished(QNetworkReply *reply);
    void onDownloadFinished(QNetworkReply *reply);

    static QString platformKey();
    static QUrl resolveUrl(const QUrl &base, const QString &value);
    static bool verifyFileHash(const QString &path, const QString &expected,
                               QString *error);
    // 启动独立更新脚本；成功返回 true
    static bool launchUpdater(const QString &zipPath, const QString &version,
                              const QString &sha256, QString *error);
    static QByteArray updaterScript();

    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    State m_state = State::Idle;
    UpdateInfo m_downloadInfo;
    QString m_downloadPath;
    QFile *m_downloadFile = nullptr;
};

#endif // UPDATEMANAGER_H
