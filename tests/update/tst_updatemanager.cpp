// ============================================================================
// tst_update —— 自动更新模块测试
//
// 覆盖：
//   1. isNewerVersion 语义化版本比较（含 v 前缀、预发布、build metadata）
//   2. isSecureUrl 下载地址安全策略
//   3. parseManifest 清单解析：平台选择、相对 URL、SHA-256、错误路径、
//      error 指针为空时不得崩溃
//   4. currentVersion 回退到 AppInfo::version()
// ============================================================================
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include "update/updatemanager.h"
#include "common/appinfo.h"

namespace {

// 与 UpdateManager::platformKey() 相同的平台键算法，供测试构造清单使用
QString expectedPlatformKey()
{
#if defined(Q_OS_WIN)
    QString os = QStringLiteral("windows");
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    QString os = QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
    QString os = QStringLiteral("linux");
#else
    QString os = QSysInfo::kernelType().toLower();
#endif

    QString arch = QSysInfo::currentCpuArchitecture().toLower();
    if (arch == QLatin1String("amd64") || arch == QLatin1String("x86_64")) {
        arch = QStringLiteral("x86_64");
    } else if (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64")) {
        arch = QStringLiteral("arm64");
    } else if (arch == QLatin1String("i386") || arch == QLatin1String("i686")) {
        arch = QStringLiteral("x86");
    }
    return os + QLatin1Char('-') + arch;
}

const QString kSha =
    QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

QByteArray manifestJson(const QJsonObject &root)
{
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

class TestUpdateManager : public QObject
{
    Q_OBJECT

private slots:
    void versionComparison_data();
    void versionComparison();
    void versionComparisonInvalidInputs();
    void secureUrl_data();
    void secureUrl();
    void parseManifestCurrentPlatform();
    void parseManifestRelativeAndAbsoluteUrls();
    void parseManifestFlatLayout();
    void parseManifestMissingCurrentPlatform();
    void parseManifestValidationErrors_data();
    void parseManifestValidationErrors();
    void parseManifestAcceptsUppercaseSha();
    void parseManifestNullErrorPointer();
    void currentVersionFallsBackToAppInfo();
};

void TestUpdateManager::versionComparison_data()
{
    QTest::addColumn<QString>("remote");
    QTest::addColumn<QString>("local");
    QTest::addColumn<bool>("expected");

    QTest::newRow("equal") << "1.0.0" << "1.0.0" << false;
    QTest::newRow("patch newer") << "1.0.1" << "1.0.0" << true;
    QTest::newRow("patch older") << "1.0.0" << "1.0.1" << false;
    QTest::newRow("minor newer") << "1.1.0" << "1.0.9" << true;
    QTest::newRow("major newer") << "2.0.0" << "1.99.99" << true;
    QTest::newRow("v prefix remote") << "v1.2.3" << "1.2.2" << true;
    QTest::newRow("V prefix equal") << "V1.2.3" << "1.2.3" << false;
    QTest::newRow("whitespace trimmed") << "  1.2.3  " << "1.2.2" << true;
    QTest::newRow("remote metadata ignored") << "1.2.3+build.5" << "1.2.3" << false;
    QTest::newRow("local metadata ignored") << "1.2.3" << "1.2.3+build" << false;
    QTest::newRow("prerelease is older") << "1.2.3-beta.1" << "1.2.3" << false;
    QTest::newRow("release beats prerelease") << "1.2.3" << "1.2.3-beta.1" << true;
    QTest::newRow("prerelease number newer") << "1.2.3-beta.2" << "1.2.3-beta.1" << true;
    QTest::newRow("prerelease number older") << "1.2.3-beta.1" << "1.2.3-beta.2" << false;
    QTest::newRow("prerelease text ordering") << "1.2.3-beta.1" << "1.2.3-alpha.9" << true;
    QTest::newRow("fewer prerelease ids is lower") << "1.2.3-beta" << "1.2.3-beta.1" << false;
    QTest::newRow("more prerelease ids is higher") << "1.2.3-beta.1" << "1.2.3-beta" << true;
    QTest::newRow("numeric id below text id") << "1.2.3-1" << "1.2.3-alpha" << false;
    QTest::newRow("text id above numeric id") << "1.2.3-alpha" << "1.2.3-1" << true;
    QTest::newRow("numeric ids compare numerically lower")
        << "1.2.3-2" << "1.2.3-10" << false;
    QTest::newRow("numeric ids compare numerically higher")
        << "1.2.3-10" << "1.2.3-2" << true;
    QTest::newRow("four components") << "1.0.0.1" << "1.0.0" << true;
    QTest::newRow("trailing zero equal") << "1.0" << "1.0.0" << false;
    QTest::newRow("shorter local") << "1.0.1" << "1.0" << true;
    QTest::newRow("zero versions equal") << "0" << "0" << false;
}

void TestUpdateManager::versionComparison()
{
    QFETCH(QString, remote);
    QFETCH(QString, local);
    QFETCH(bool, expected);
    QCOMPARE(UpdateManager::isNewerVersion(remote, local), expected);
}

void TestUpdateManager::versionComparisonInvalidInputs()
{
    // 任一版本无法解析时一律返回 false（不提示更新，避免误升级）
    QVERIFY(!UpdateManager::isNewerVersion(QString(), "1.0.0"));
    QVERIFY(!UpdateManager::isNewerVersion("garbage", "1.0.0"));
    QVERIFY(!UpdateManager::isNewerVersion("1.0.0", QString()));
    QVERIFY(!UpdateManager::isNewerVersion("1.0.0", "garbage"));
    QVERIFY(!UpdateManager::isNewerVersion("v", "1.0.0"));

    // "garbage.1" 是合法的 SemVer 预发布标识，正式版 1.0.0 比它新
    QVERIFY(UpdateManager::isNewerVersion("1.0.0", "1.0.0-garbage.1"));
}

void TestUpdateManager::secureUrl_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("expected");

    QTest::newRow("https") << "https://example.com/update.json" << true;
    QTest::newRow("https uppercase scheme") << "HTTPS://example.com/x.zip" << true;
    QTest::newRow("file url") << "file:///tmp/NTerminal.zip" << true;
    QTest::newRow("file unc") << "file://server/share/NTerminal.zip" << true;
    QTest::newRow("http localhost") << "http://localhost:8080/update.json" << true;
    QTest::newRow("http loopback") << "http://127.0.0.1/update.json" << true;
    QTest::newRow("http ipv6 loopback") << "http://[::1]:8000/update.json" << true;
    QTest::newRow("http external rejected") << "http://example.com/update.json" << false;
    QTest::newRow("http lookalike host rejected")
        << "http://localhost.evil.example/update.json" << false;
    QTest::newRow("http lookalike ip rejected")
        << "http://127.0.0.1.evil.example/update.json" << false;
    QTest::newRow("ftp rejected") << "ftp://example.com/update.json" << false;
    QTest::newRow("relative rejected") << "update.json" << false;
    QTest::newRow("empty rejected") << "" << false;
}

void TestUpdateManager::secureUrl()
{
    QFETCH(QString, url);
    QFETCH(bool, expected);
    QCOMPARE(UpdateManager::isSecureUrl(QUrl(url)), expected);
}

void TestUpdateManager::parseManifestCurrentPlatform()
{
    QJsonObject platform;
    platform["url"] = "NTerminal-1.2.3.zip";
    platform["sha256"] = kSha;
    platform["size"] = 123456;

    QJsonObject platforms;
    platforms[expectedPlatformKey()] = platform;

    QJsonObject root;
    root["version"] = "1.2.3";
    root["notes"] = "Bug fixes";
    root["published"] = "2026-10-01T00:00:00Z";
    root["platforms"] = platforms;

    QString error = "sentinel";
    const UpdateInfo info = UpdateManager::parseManifest(
        manifestJson(root), QUrl("https://example.com/releases/update.json"), &error);

    QVERIFY(info.valid);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(info.version, QString("1.2.3"));
    QCOMPARE(info.notes, QString("Bug fixes"));
    QCOMPARE(info.published, QString("2026-10-01T00:00:00Z"));
    QCOMPARE(info.sha256, kSha);
    QCOMPARE(info.size, qint64(123456));
    QCOMPARE(info.url, QUrl("https://example.com/releases/NTerminal-1.2.3.zip"));
}

void TestUpdateManager::parseManifestRelativeAndAbsoluteUrls()
{
    QString error;
    UpdateInfo info = UpdateManager::parseManifest(
        manifestJson(QJsonObject{{"version", "1.0.0"}, {"url", "../downloads/pkg.zip"},
                                 {"sha256", kSha}}),
        QUrl("https://example.com/releases/v1/update.json"), &error);
    QVERIFY(info.valid);
    QCOMPARE(info.url, QUrl("https://example.com/releases/downloads/pkg.zip"));

    info = UpdateManager::parseManifest(
        manifestJson(QJsonObject{{"version", "1.0.0"},
                                 {"url", "https://cdn.example.org/pkg.zip"},
                                 {"sha256", kSha}}),
        QUrl("https://example.com/update.json"), &error);
    QVERIFY(info.valid);
    QCOMPARE(info.url, QUrl("https://cdn.example.org/pkg.zip"));
}

void TestUpdateManager::parseManifestFlatLayout()
{
    // 没有 platforms 字段时，下载信息直接位于根对象
    QJsonObject root;
    root["version"] = "2.0.0";
    root["url"] = "https://example.com/NTerminal-2.0.0.zip";
    root["sha256"] = kSha;
    QString error;
    UpdateInfo info = UpdateManager::parseManifest(
        manifestJson(root), QUrl("https://example.com/update.json"), &error);
    QVERIFY(info.valid);
    QVERIFY(error.isEmpty());
    QCOMPARE(info.size, qint64(-1));   // 未提供 size

    // platforms 为空对象同样回退到根对象
    root["platforms"] = QJsonObject();
    info = UpdateManager::parseManifest(
        manifestJson(root), QUrl("https://example.com/update.json"), &error);
    QVERIFY(info.valid);
}

void TestUpdateManager::parseManifestMissingCurrentPlatform()
{
    QJsonObject otherPlatform;
    otherPlatform["url"] = "https://example.com/pkg.zip";
    otherPlatform["sha256"] = kSha;

    QJsonObject root;
    root["version"] = "1.2.3";
    root["platforms"] = QJsonObject{{"definitely-not-this-platform", otherPlatform}};

    QString error;
    const UpdateInfo info = UpdateManager::parseManifest(
        manifestJson(root), QUrl("https://example.com/update.json"), &error);
    QVERIFY(!info.valid);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(expectedPlatformKey()));
}

void TestUpdateManager::parseManifestValidationErrors_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("url");

    QTest::newRow("invalid json") << QByteArray("{not json") << "https://e.test/u.json";
    QTest::newRow("json array") << QByteArray("[]") << "https://e.test/u.json";
    QTest::newRow("json string") << QByteArray("\"x\"") << "https://e.test/u.json";
    QTest::newRow("missing version")
        << manifestJson(QJsonObject{{"url", "https://e.test/p.zip"}, {"sha256", kSha}})
        << "https://e.test/u.json";
    QTest::newRow("empty version")
        << manifestJson(QJsonObject{{"version", "   "}, {"url", "https://e.test/p.zip"},
                                    {"sha256", kSha}})
        << "https://e.test/u.json";
    QTest::newRow("missing url")
        << manifestJson(QJsonObject{{"version", "1.0.0"}, {"sha256", kSha}})
        << "https://e.test/u.json";
    QTest::newRow("missing sha")
        << manifestJson(QJsonObject{{"version", "1.0.0"},
                                    {"url", "https://e.test/p.zip"}})
        << "https://e.test/u.json";
    QTest::newRow("short sha")
        << manifestJson(QJsonObject{{"version", "1.0.0"},
                                    {"url", "https://e.test/p.zip"}, {"sha256", "abc123"}})
        << "https://e.test/u.json";
    QTest::newRow("non hex sha")
        << manifestJson(QJsonObject{{"version", "1.0.0"},
                                    {"url", "https://e.test/p.zip"},
                                    {"sha256", QString(64, QChar('z'))}})
        << "https://e.test/u.json";
    QTest::newRow("insecure http url")
        << manifestJson(QJsonObject{{"version", "1.0.0"},
                                    {"url", "http://example.com/p.zip"},
                                    {"sha256", kSha}})
        << "https://e.test/u.json";
    QTest::newRow("unsupported scheme")
        << manifestJson(QJsonObject{{"version", "1.0.0"},
                                    {"url", "ftp://example.com/p.zip"},
                                    {"sha256", kSha}})
        << "https://e.test/u.json";
}

void TestUpdateManager::parseManifestValidationErrors()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, url);

    QString error;
    const UpdateInfo info = UpdateManager::parseManifest(data, QUrl(url), &error);
    QVERIFY2(!info.valid, qPrintable(url));
    QVERIFY2(!error.isEmpty(), qPrintable(url));
}

void TestUpdateManager::parseManifestAcceptsUppercaseSha()
{
    QJsonObject root;
    root["version"] = "1.0.0";
    root["url"] = "https://example.com/pkg.zip";
    root["sha256"] = kSha.toUpper();
    QString error;
    const UpdateInfo info = UpdateManager::parseManifest(
        manifestJson(root), QUrl("https://example.com/update.json"), &error);
    QVERIFY2(info.valid, qPrintable(error));
    QCOMPARE(info.sha256, kSha.toUpper());
}

void TestUpdateManager::parseManifestNullErrorPointer()
{
    // 公开 API 允许不关心错误详情；空指针不得导致崩溃
    const UpdateInfo bad = UpdateManager::parseManifest(
        QByteArray("{"), QUrl("https://example.com/update.json"), nullptr);
    QVERIFY(!bad.valid);

    QJsonObject root;
    root["version"] = "1.0.0";
    root["url"] = "https://example.com/pkg.zip";
    root["sha256"] = kSha;
    const UpdateInfo good = UpdateManager::parseManifest(
        manifestJson(root), QUrl("https://example.com/update.json"), nullptr);
    QVERIFY(good.valid);
}

void TestUpdateManager::currentVersionFallsBackToAppInfo()
{
    const QString saved = QCoreApplication::applicationVersion();
    QCoreApplication::setApplicationVersion(QString());
    QCOMPARE(UpdateManager::currentVersion(), AppInfo::version());

    QCoreApplication::setApplicationVersion(QStringLiteral("9.9.9"));
    QCOMPARE(UpdateManager::currentVersion(), QStringLiteral("9.9.9"));
    QCoreApplication::setApplicationVersion(saved);
}

QTEST_GUILESS_MAIN(TestUpdateManager)
#include "tst_updatemanager.moc"
