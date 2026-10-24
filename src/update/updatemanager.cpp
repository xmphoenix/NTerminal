#include "update/updatemanager.h"

#include "common/appinfo.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>
#include <QVersionNumber>

namespace {

constexpr qint64 kMaxManifestBytes = 2 * 1024 * 1024;

QString normalizedArchitecture()
{
    QString arch = QSysInfo::currentCpuArchitecture().toLower();
    if (arch == QLatin1String("amd64") || arch == QLatin1String("x86_64")) {
        return QStringLiteral("x86_64");
    }
    if (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64")) {
        return QStringLiteral("arm64");
    }
    if (arch == QLatin1String("i386") || arch == QLatin1String("i686")) {
        return QStringLiteral("x86");
    }
    return arch;
}

bool canWriteInstallDirectory(QString *error)
{
    const QString target = QCoreApplication::applicationDirPath();
    QFile probe(QDir(target).filePath(QStringLiteral(".nt-update-write-test")));
    if (!probe.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QCoreApplication::translate(
            "UpdateManager",
            "安装目录不可写：%1。请把 NTerminal 放到用户可写目录，或以管理员权限运行。")
                     .arg(target);
        return false;
    }
    probe.close();
    QFile::remove(probe.fileName());
    return true;
}

} // namespace

UpdateManager::UpdateManager(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

UpdateManager::~UpdateManager()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
}

// ---------------------------------------------------------------------------
// 配置
// ---------------------------------------------------------------------------

QString UpdateManager::feedUrl()
{
    QSettings settings(QStringLiteral("NTerminal"), QStringLiteral("NTerminal"));
    return settings.value(QStringLiteral("Update/FeedUrl")).toString().trimmed();
}

void UpdateManager::setFeedUrl(const QString &url)
{
    QSettings settings(QStringLiteral("NTerminal"), QStringLiteral("NTerminal"));
    settings.setValue(QStringLiteral("Update/FeedUrl"), url.trimmed());
}

bool UpdateManager::autoCheckEnabled()
{
    QSettings settings(QStringLiteral("NTerminal"), QStringLiteral("NTerminal"));
    return settings.value(QStringLiteral("Update/AutoCheck"), true).toBool();
}

void UpdateManager::setAutoCheckEnabled(bool enabled)
{
    QSettings settings(QStringLiteral("NTerminal"), QStringLiteral("NTerminal"));
    settings.setValue(QStringLiteral("Update/AutoCheck"), enabled);
}

QString UpdateManager::currentVersion()
{
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? AppInfo::version() : version;
}

QString UpdateManager::pendingUpdatePath()
{
    return QDir::homePath() + QStringLiteral("/.nterminal/pending-update.json");
}

QString UpdateManager::updaterLogPath()
{
    return QDir::homePath() + QStringLiteral("/.nterminal/updater.log");
}

bool UpdateManager::isSecureUrl(const QUrl &url)
{
    if (!url.isValid() || url.isRelative()) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https") || scheme == QLatin1String("file")) {
        return true;
    }
    if (scheme == QLatin1String("http")) {
        const QString host = url.host().toLower();
        return host == QLatin1String("localhost")
               || host == QLatin1String("127.0.0.1")
               || host == QLatin1String("::1");
    }
    return false;
}

QString UpdateManager::platformKey()
{
#if defined(Q_OS_WIN)
    const QString os = QStringLiteral("windows");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    const QString os = QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
    const QString os = QStringLiteral("linux");
#else
    const QString os = QSysInfo::kernelType().toLower();
#endif
    return os + QLatin1Char('-') + normalizedArchitecture();
}

bool UpdateManager::isBusy() const
{
    return m_state != State::Idle;
}

// ---------------------------------------------------------------------------
// 版本比较（SemVer 子集）
// ---------------------------------------------------------------------------

bool UpdateManager::isNewerVersion(const QString &remote, const QString &local)
{
    auto parse = [](QString value, QVersionNumber *number, QStringList *prerelease) {
        value = value.trimmed();
        if (value.startsWith(QLatin1Char('v')) || value.startsWith(QLatin1Char('V'))) {
            value.remove(0, 1);
        }
        const int plus = value.indexOf(QLatin1Char('+'));
        if (plus >= 0) {
            value = value.left(plus);
        }
        const int dash = value.indexOf(QLatin1Char('-'));
        if (dash >= 0) {
            *prerelease = value.mid(dash + 1).split(QLatin1Char('.'),
                                                     Qt::SkipEmptyParts);
            value = value.left(dash);
        } else {
            prerelease->clear();
        }
        *number = QVersionNumber::fromString(value);
        return !number->isNull() || value == QLatin1String("0");
    };

    QVersionNumber remoteNumber;
    QVersionNumber localNumber;
    QStringList remotePre;
    QStringList localPre;
    if (!parse(remote, &remoteNumber, &remotePre)
        || !parse(local, &localNumber, &localPre)) {
        return false;
    }

    const int comparison = QVersionNumber::compare(remoteNumber, localNumber);
    if (comparison != 0) {
        return comparison > 0;
    }

    if (remotePre.isEmpty() && localPre.isEmpty()) {
        return false;
    }
    if (remotePre.isEmpty()) {
        return true;   // 正式版 > 预发布版
    }
    if (localPre.isEmpty()) {
        return false;
    }

    const int count = qMax(remotePre.size(), localPre.size());
    for (int i = 0; i < count; ++i) {
        if (i >= localPre.size()) {
            return true;    // 前缀相同，标识更多者更高
        }
        if (i >= remotePre.size()) {
            return false;
        }

        bool remoteNumeric = false;
        bool localNumeric = false;
        const qlonglong remoteValue = remotePre.at(i).toLongLong(&remoteNumeric);
        const qlonglong localValue = localPre.at(i).toLongLong(&localNumeric);

        if (remoteNumeric && localNumeric) {
            if (remoteValue != localValue) {
                return remoteValue > localValue;
            }
        } else if (remoteNumeric != localNumeric) {
            // SemVer：数字标识符优先级低于字母数字标识符
            return !remoteNumeric;
        } else {
            const int textCompare =
                QString::compare(remotePre.at(i), localPre.at(i));
            if (textCompare != 0) {
                return textCompare > 0;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 清单解析
// ---------------------------------------------------------------------------

QUrl UpdateManager::resolveUrl(const QUrl &base, const QString &value)
{
    const QUrl direct(value);
    if (direct.isValid() && !direct.isRelative()) {
        return direct;
    }
    return base.resolved(direct);
}

UpdateInfo UpdateManager::parseManifest(const QByteArray &data,
                                        const QUrl &manifestUrl,
                                        QString *error)
{
    UpdateInfo info;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QCoreApplication::translate(
            "UpdateManager", "更新清单不是有效的 JSON：%1")
                     .arg(parseError.errorString());
        return info;
    }

    const QJsonObject root = document.object();
    info.version = root.value(QStringLiteral("version")).toString().trimmed();
    info.notes = root.value(QStringLiteral("notes")).toString();
    info.published = root.value(QStringLiteral("published")).toString();

    if (info.version.isEmpty()) {
        *error = QCoreApplication::translate(
            "UpdateManager", "更新清单缺少 version 字段。");
        return info;
    }

    QJsonObject platform = root;
    const QJsonObject platforms = root.value(QStringLiteral("platforms")).toObject();
    if (!platforms.isEmpty()) {
        const QString key = platformKey();
        platform = platforms.value(key).toObject();
        if (platform.isEmpty()) {
            *error = QCoreApplication::translate(
                "UpdateManager",
                "更新清单中没有当前平台 %1 的下载信息。")
                         .arg(key);
            return info;
        }
    }

    const QString urlText = platform.value(QStringLiteral("url")).toString().trimmed();
    info.sha256 = platform.value(QStringLiteral("sha256")).toString().trimmed();
    const QJsonValue sizeValue = platform.value(QStringLiteral("size"));
    info.size = sizeValue.isDouble() ? qint64(sizeValue.toDouble()) : -1;

    if (urlText.isEmpty()) {
        *error = QCoreApplication::translate(
            "UpdateManager", "更新清单缺少下载地址 url。");
        return info;
    }

    static const QRegularExpression shaPattern(
        QStringLiteral("^[0-9a-fA-F]{64}$"));
    if (!shaPattern.match(info.sha256).hasMatch()) {
        *error = QCoreApplication::translate(
            "UpdateManager", "更新清单的 sha256 必须是 64 位十六进制字符串。");
        return info;
    }

    info.url = resolveUrl(manifestUrl, urlText);
    if (!info.url.isValid() || !isSecureUrl(info.url)) {
        *error = QCoreApplication::translate(
            "UpdateManager",
            "下载地址必须是 HTTPS（本地测试可用 http://127.0.0.1 或 file://）：%1")
                     .arg(info.url.toString());
        return info;
    }

    info.valid = true;
    return info;
}

// ---------------------------------------------------------------------------
// 检查更新
// ---------------------------------------------------------------------------

void UpdateManager::setState(State state)
{
    m_state = state;
}

void UpdateManager::fail(const QString &message)
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
    if (!m_downloadPath.isEmpty()) {
        QFile::remove(m_downloadPath);
        m_downloadPath.clear();
    }
    m_downloadInfo = UpdateInfo();
    setState(State::Idle);
    emit error(message);
}

void UpdateManager::checkForUpdates()
{
    if (isBusy()) {
        return;
    }

    const QString feed = feedUrl();
    if (feed.isEmpty()) {
        fail(tr("未配置更新源。请在“设置 → 更新设置”中填写 update.json 地址。"));
        return;
    }

    const QUrl manifestUrl(feed);
    if (!isSecureUrl(manifestUrl)) {
        fail(tr("更新源必须是 HTTPS 地址（本地测试可用 http://127.0.0.1 或 file://）。"));
        return;
    }

    setState(State::Checking);
    emit checking();

    QNetworkRequest request(manifestUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("NTerminal-Updater/%1").arg(currentVersion()));
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onManifestFinished(reply); });
}

void UpdateManager::onManifestFinished(QNetworkReply *reply)
{
    const QByteArray data = reply->readAll();
    const QUrl finalUrl = reply->url();
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();

    m_reply = nullptr;
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError) {
        fail(tr("检查更新失败：%1").arg(networkErrorText));
        return;
    }
    if (finalUrl.scheme() != QLatin1String("file") && status >= 400) {
        fail(tr("检查更新失败：服务器返回 HTTP %1。").arg(status));
        return;
    }
    if (data.size() > kMaxManifestBytes) {
        fail(tr("更新清单过大，已拒绝。"));
        return;
    }

    QString error;
    const UpdateInfo info = parseManifest(data, finalUrl, &error);
    if (!info.valid) {
        fail(error);
        return;
    }

    setState(State::Idle);
    if (isNewerVersion(info.version, currentVersion())) {
        emit updateAvailable(info);
    } else {
        emit noUpdate(currentVersion());
    }
}

// ---------------------------------------------------------------------------
// 下载
// ---------------------------------------------------------------------------

void UpdateManager::downloadUpdate(const UpdateInfo &info)
{
    if (isBusy()) {
        return;
    }
    if (!info.valid) {
        fail(tr("更新信息无效。"));
        return;
    }
    if (!isSecureUrl(info.url)) {
        fail(tr("下载地址必须是 HTTPS。"));
        return;
    }

    QString safeVersion = info.version;
    safeVersion.replace(QRegularExpression(QStringLiteral("[^0-9A-Za-z._-]")),
                        QStringLiteral("_"));

    const QString tempDir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(tempDir);
    m_downloadPath = QDir(tempDir).filePath(
        QStringLiteral("NTerminal-update-%1.zip").arg(safeVersion));
    QFile::remove(m_downloadPath);

    m_downloadInfo = info;
    m_downloadFile = new QFile(m_downloadPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("无法创建临时下载文件：%1").arg(m_downloadFile->errorString()));
        return;
    }

    setState(State::Downloading);
    emit downloadProgress(0, info.size);

    QNetworkRequest request(info.url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("NTerminal-Updater/%1").arg(currentVersion()));
    request.setTransferTimeout(600000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (!m_downloadFile) {
            return;
        }
        const QByteArray chunk = reply->readAll();
        if (m_downloadFile->write(chunk) != chunk.size()) {
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                emit downloadProgress(received, total);
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onDownloadFinished(reply); });
}

void UpdateManager::onDownloadFinished(QNetworkReply *reply)
{
    if (m_downloadFile) {
        // readyRead 通常已取走全部数据；finished 前再取一次，避免尾部丢失
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty()) {
            m_downloadFile->write(tail);
        }
        m_downloadFile->flush();
        m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }

    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    m_reply = nullptr;
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError) {
        fail(tr("下载更新失败：%1").arg(networkErrorText));
        return;
    }

    const QFileInfo fileInfo(m_downloadPath);
    if (!fileInfo.exists() || fileInfo.size() <= 0) {
        fail(tr("下载的更新包为空。"));
        return;
    }
    if (m_downloadInfo.size > 0 && fileInfo.size() != m_downloadInfo.size) {
        fail(tr("更新包大小不一致：期望 %1 字节，实际 %2 字节。")
                 .arg(m_downloadInfo.size)
                 .arg(fileInfo.size()));
        return;
    }

    QString error;
    if (!verifyFileHash(m_downloadPath, m_downloadInfo.sha256, &error)) {
        fail(error);
        return;
    }

    const QString path = m_downloadPath;
    const UpdateInfo info = m_downloadInfo;
    m_downloadPath.clear();
    m_downloadInfo = UpdateInfo();
    setState(State::Idle);
    emit downloadFinished(path, info);
}

void UpdateManager::cancel()
{
    if (!isBusy()) {
        return;
    }
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
    if (!m_downloadPath.isEmpty()) {
        QFile::remove(m_downloadPath);
        m_downloadPath.clear();
    }
    m_downloadInfo = UpdateInfo();
    setState(State::Idle);
    emit cancelled();
}

// ---------------------------------------------------------------------------
// 安装与更新脚本
// ---------------------------------------------------------------------------

bool UpdateManager::verifyFileHash(const QString &path, const QString &expected,
                                   QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QCoreApplication::translate(
            "UpdateManager", "无法读取更新包：%1").arg(file.errorString());
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        *error = QCoreApplication::translate(
            "UpdateManager", "读取更新包失败：%1").arg(file.errorString());
        return false;
    }

    const QString actual = QString::fromLatin1(hash.result().toHex());
    if (actual.compare(expected, Qt::CaseInsensitive) != 0) {
        *error = QCoreApplication::translate(
            "UpdateManager",
            "更新包校验失败（SHA-256 不匹配），已拒绝安装。")
                     + QStringLiteral("\n")
                     + QCoreApplication::translate("UpdateManager", "期望：%1")
                           .arg(expected)
                     + QStringLiteral("\n")
                     + QCoreApplication::translate("UpdateManager", "实际：%1")
                           .arg(actual);
        return false;
    }
    return true;
}

QByteArray UpdateManager::updaterScript()
{
#if defined(Q_OS_WIN)
    return QStringLiteral(R"PS1(
param(
    [int]$WaitPid,
    [string]$Zip,
    [string]$Target,
    [string]$Exe,
    [string]$Pending,
    [string]$Sha256,
    [string]$LogPath
)
$ErrorActionPreference = "Stop"

function Write-Log([string]$Message) {
    try {
        "$(Get-Date -Format o) $Message" | Out-File -LiteralPath $LogPath -Append -Encoding utf8
    } catch {
    }
}

try {
    Write-Log "Waiting for process $WaitPid to exit"
    for ($i = 0; $i -lt 900; $i++) {
        if (-not (Get-Process -Id $WaitPid -ErrorAction SilentlyContinue)) {
            break
        }
        Start-Sleep -Milliseconds 200
    }
    Start-Sleep -Milliseconds 800

    Write-Log "Verifying package SHA-256"
    $actualHash = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLower()
    if ($actualHash -ne $Sha256.ToLower()) {
        throw "SHA-256 mismatch: expected $Sha256, got $actualHash"
    }

    $stage = Join-Path ([IO.Path]::GetTempPath()) ("NTerminal-stage-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    Write-Log "Expanding $Zip"
    Expand-Archive -LiteralPath $Zip -DestinationPath $stage -Force

    $exeName = Split-Path -Leaf $Exe
    $newExe = Get-ChildItem -Path $stage -Recurse -Filter $exeName -File | Select-Object -First 1
    if (-not $newExe) {
        throw "Package does not contain $exeName"
    }
    $root = $newExe.Directory.FullName
    Write-Log "Copying $root to $Target"

    for ($attempt = 1; $attempt -le 20; $attempt++) {
        try {
            Copy-Item -Path (Join-Path $root "*") -Destination $Target -Recurse -Force
            break
        } catch {
            if ($attempt -eq 20) {
                throw
            }
            Write-Log "Copy attempt $attempt failed: $($_.Exception.Message)"
            Start-Sleep -Milliseconds 500
        }
    }

    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $Zip -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $Pending -Force -ErrorAction SilentlyContinue

    Write-Log "Starting $Exe"
    Start-Process -FilePath $Exe -WorkingDirectory $Target
    Write-Log "Update completed"
} catch {
    Write-Log "ERROR: $($_.Exception.Message)"
    Remove-Item -LiteralPath $Pending -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $Exe) {
        Write-Log "Restarting existing installation"
        Start-Process -FilePath $Exe -WorkingDirectory $Target
    }
}
)PS1").toUtf8();
#else
    return QStringLiteral(R"SH(
#!/bin/sh
pid="$1"
zip="$2"
target="$3"
exe="$4"
pending="$5"
sha="$6"
log="$7"

exec >>"$log" 2>&1
echo "$(date) waiting for $pid"
i=0
while kill -0 "$pid" 2>/dev/null && [ "$i" -lt 900 ]; do
    i=$((i + 1))
    sleep 0.2
done
sleep 1

echo "verifying package sha256"
if command -v sha256sum >/dev/null 2>&1; then
    actual_hash="$(sha256sum "$zip" | awk '{print $1}')"
else
    actual_hash="$(shasum -a 256 "$zip" | awk '{print $1}')"
fi
if [ "$actual_hash" != "$sha" ]; then
    echo "SHA-256 mismatch"
    rm -f "$pending"
    (cd "$target" && "$exe" >/dev/null 2>&1 &)
    exit 1
fi

stage="$(mktemp -d)"
if ! unzip -q "$zip" -d "$stage"; then
    echo "unzip failed"
    rm -rf "$stage"
    rm -f "$pending"
    (cd "$target" && "$exe" >/dev/null 2>&1 &)
    exit 1
fi

new_exe="$(find "$stage" -name "$(basename "$exe")" -type f | head -n 1)"
if [ -z "$new_exe" ]; then
    echo "new executable not found"
    rm -rf "$stage"
    rm -f "$pending"
    (cd "$target" && "$exe" >/dev/null 2>&1 &)
    exit 1
fi

root="$(dirname "$new_exe")"
if ! cp -R "$root"/. "$target"/; then
    echo "copy failed"
    rm -rf "$stage"
    rm -f "$pending"
    (cd "$target" && "$exe" >/dev/null 2>&1 &)
    exit 1
fi

rm -rf "$stage" "$zip" "$pending"
(cd "$target" && nohup "$exe" >/dev/null 2>&1 &)
)SH").toUtf8();
#endif
}

bool UpdateManager::launchUpdater(const QString &zipPath, const QString &version,
                                  const QString &sha256, QString *error)
{
    Q_UNUSED(version)

    if (!canWriteInstallDirectory(error)) {
        return false;
    }

    const QString normalizedSha256 = sha256.toLower();
    const QString target = QCoreApplication::applicationDirPath();
    const QString exe = QCoreApplication::applicationFilePath();
    const QString pending = pendingUpdatePath();
    const QString logPath = updaterLogPath();
    QDir().mkpath(QFileInfo(logPath).absolutePath());

#if defined(Q_OS_WIN)
    const QString powershell =
        QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (powershell.isEmpty()) {
        *error = tr("找不到 powershell.exe，无法启动更新程序。");
        return false;
    }

    const QString scriptPath =
        QDir::tempPath()
        + QStringLiteral("/NTerminal-update-%1.ps1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = tr("无法创建更新脚本：%1").arg(script.errorString());
        return false;
    }
    script.write(updaterScript());
    script.close();

    const QStringList arguments{
        QStringLiteral("-NoProfile"),
        QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), scriptPath,
        QStringLiteral("-WaitPid"), QString::number(QCoreApplication::applicationPid()),
        QStringLiteral("-Zip"), zipPath,
        QStringLiteral("-Target"), target,
        QStringLiteral("-Exe"), exe,
        QStringLiteral("-Pending"), pending,
        QStringLiteral("-Sha256"), normalizedSha256,
        QStringLiteral("-LogPath"), logPath,
    };
    if (!QProcess::startDetached(powershell, arguments)) {
        *error = tr("无法启动更新程序。");
        return false;
    }
    return true;
#else
    if (QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty()) {
        *error = tr("找不到 unzip，无法解压更新包。请先安装 unzip。");
        return false;
    }

    const QString scriptPath =
        QDir::tempPath()
        + QStringLiteral("/NTerminal-update-%1.sh")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = tr("无法创建更新脚本：%1").arg(script.errorString());
        return false;
    }
    script.write(updaterScript());
    script.close();
    QFile::setPermissions(scriptPath,
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                              | QFile::ReadGroup | QFile::ExeGroup
                              | QFile::ReadOther | QFile::ExeOther);

    const QStringList arguments{
        scriptPath,
        QString::number(QCoreApplication::applicationPid()),
        zipPath,
        target,
        exe,
        pending,
        normalizedSha256,
        logPath,
    };
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), arguments)) {
        *error = tr("无法启动更新程序。");
        return false;
    }
    return true;
#endif
}

bool UpdateManager::applyUpdate(const QString &zipPath, const UpdateInfo &info,
                                bool restartNow, QString *error)
{
    if (!QFileInfo::exists(zipPath)) {
        *error = tr("更新包不存在：%1").arg(zipPath);
        return false;
    }
    if (!verifyFileHash(zipPath, info.sha256, error)) {
        return false;
    }
    if (!canWriteInstallDirectory(error)) {
        return false;
    }

    QDir().mkpath(QFileInfo(pendingUpdatePath()).absolutePath());
    QJsonObject pending{
        {QStringLiteral("zip"), zipPath},
        {QStringLiteral("version"), info.version},
        {QStringLiteral("sha256"), info.sha256},
        {QStringLiteral("created_at"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
    };
    QSaveFile file(pendingUpdatePath());
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(pending).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        *error = tr("无法写入待安装更新信息：%1").arg(pendingUpdatePath());
        return false;
    }

    if (!restartNow) {
        return true;
    }

    if (!launchUpdater(zipPath, info.version, info.sha256, error)) {
        QFile::remove(pendingUpdatePath());
        return false;
    }
    return true;
}

bool UpdateManager::applyPendingUpdate()
{
    QFile file(pendingUpdatePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QFile::remove(pendingUpdatePath());
        return false;
    }

    const QJsonObject object = document.object();
    const QString zipPath = object.value(QStringLiteral("zip")).toString();
    const QString version = object.value(QStringLiteral("version")).toString();
    const QString sha256 = object.value(QStringLiteral("sha256")).toString();

    QString error;
    if (zipPath.isEmpty() || !QFileInfo::exists(zipPath)
        || !verifyFileHash(zipPath, sha256, &error)
        || !launchUpdater(zipPath, version, sha256, &error)) {
        qWarning() << "[Update] Pending update could not be applied:" << error;
        QFile::remove(pendingUpdatePath());
        return false;
    }
    return true;
}
