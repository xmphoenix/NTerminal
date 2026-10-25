// ============================================================================
// tst_integration —— 真实环境功能测试（SSH / SFTP / SCP / 串口）
//
// 与其余单元测试不同，本套用例会连接真实设备。为避免普通构建/CI 意外访问
// 外部资源，每个用例默认 QSKIP，只有设置对应环境变量后才执行：
//
//   NTERMINAL_TEST_SSH_HOST        SSH/SFTP/SCP 主机（如 192.0.2.10）
//   NTERMINAL_TEST_SSH_PORT        端口，默认 22
//   NTERMINAL_TEST_SSH_USER        用户名
//   NTERMINAL_TEST_SSH_PASSWORD    密码
//
//   NTERMINAL_TEST_SERIAL_PORT     串口名（如 COM3）
//   NTERMINAL_TEST_SERIAL_BAUD     波特率，默认 115200
//   NTERMINAL_TEST_SERIAL_USER     串口控制台用户名
//   NTERMINAL_TEST_SERIAL_PASSWORD 串口控制台密码
//
// 凭据只从环境变量读取，不写入测试代码或测试输出；失败信息中的终端
// 回显会先屏蔽密码。
// ============================================================================
#include <QtTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QScopedPointer>
#include <QSerialPortInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <functional>

#include "session/connectionhandler.h"
#include "session/connectionhandlerfactory.h"
#include "session/serialhandler.h"
#include "session/telnethandler.h"
#include "session/ssshandler.h"
#include "session/rloginhandler.h"
#include "session/sftphandler.h"
#include "session/ftphandler.h"
#include "session/tftphandler.h"
#include "files/sshfileclient.h"

namespace {

QString envValue(const char *name)
{
    return qEnvironmentVariable(name).trimmed();
}

int envInt(const char *name, int fallback)
{
    bool ok = false;
    const int value = envValue(name).toInt(&ok);
    return ok ? value : fallback;
}

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QTest::qWait(20);
    }
    return predicate();
}

bool containsText(const QByteArray &data, const char *needle)
{
    return QString::fromLatin1(data).contains(QLatin1String(needle),
                                              Qt::CaseInsensitive);
}

QByteArray transcriptTail(const QByteArray &transcript, const QString &password)
{
    QByteArray safe = transcript;
    if (!password.isEmpty()) {
        safe.replace(password.toUtf8(), "***");
    }
    return safe.right(3000);
}

Common::ConnectionSettings sshTestSettings()
{
    Common::ConnectionSettings settings;
    settings.type = Common::ConnectionType::SSH;
    settings.host = envValue("NTERMINAL_TEST_SSH_HOST");
    const int port = envInt("NTERMINAL_TEST_SSH_PORT", 22);
    settings.port = (port >= 1 && port <= 65535) ? quint16(port) : quint16(22);
    settings.username = envValue("NTERMINAL_TEST_SSH_USER");
    settings.password = envValue("NTERMINAL_TEST_SSH_PASSWORD");
    return settings;
}

QString sshSkipReason(const Common::ConnectionSettings &settings)
{
    if (settings.host.isEmpty()) {
        return QStringLiteral("未设置 NTERMINAL_TEST_SSH_HOST，跳过真实 SSH 测试");
    }
    if (settings.username.isEmpty() || settings.password.isEmpty()) {
        return QStringLiteral("未设置 NTERMINAL_TEST_SSH_USER/PASSWORD，跳过真实 SSH 测试");
    }
    return QString();
}

int indexOfEntry(const QVector<RemoteFileEntry> &entries, const QString &name)
{
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).name == name) {
            return i;
        }
    }
    return -1;
}

QByteArray makePayload(int size)
{
    QByteArray payload(size, '\0');
    for (int i = 0; i < payload.size(); ++i) {
        payload[i] = char((i * 37 + 11) & 0xFF);
    }
    return payload;
}

// 测试失败提前返回时也要删除远端临时目录，避免在真实主机留下垃圾。
struct RemoteDirectoryGuard {
    SshFileClient *client = nullptr;
    QString path;

    ~RemoteDirectoryGuard()
    {
        if (!client || path.isEmpty()) {
            return;
        }
        QString error;
        QVector<RemoteFileEntry> entries;
        if (client->listDirectory(path, &entries, &error)) {
            for (const RemoteFileEntry &entry : entries) {
                RemoteTransferItem item;
                item.name = entry.name;
                item.path = entry.path;
                item.isDir = entry.isDir;
                item.size = entry.size;
                item.mode = entry.mode;
                client->removeEntry(item, &error);
            }
        }
        RemoteTransferItem directory;
        directory.name = QFileInfo(path).fileName();
        directory.path = path;
        directory.isDir = true;
        client->removeEntry(directory, &error);
    }
};

} // namespace

class TestIntegration : public QObject
{
    Q_OBJECT

private slots:
    void factoryCreatesHandlers();
    void serialRejectsMissingPort();
    void serialConsoleLogin();
    void sshHandlerLoginAndStats();
    void sshRejectsWrongPassword();
    void sftpFileLifecycle();
    void scpFileTransfer();
};

void TestIntegration::factoryCreatesHandlers()
{
    struct HandlerCase {
        Common::ConnectionType type;
        const char *className;
        bool supported;
    };
    const HandlerCase cases[] = {
        { Common::ConnectionType::Serial, "SerialHandler", true },
        { Common::ConnectionType::Telnet, "TelnetHandler", true },
        { Common::ConnectionType::SSH, "SshHandler", true },
        { Common::ConnectionType::Rlogin, "RloginHandler", true },
        { Common::ConnectionType::SFTP, "SftpHandler", true },
        { Common::ConnectionType::FTP, "FtpHandler", true },
        { Common::ConnectionType::TFTP, "TftpHandler", true },
        { Common::ConnectionType::SCP, "", false },
        { Common::ConnectionType::RDP, "", false },
    };

    for (const HandlerCase &testCase : cases) {
        Common::ConnectionSettings settings;
        settings.type = testCase.type;
        settings.host = QStringLiteral("127.0.0.1");
        settings.portName = QStringLiteral("COM_TEST");
        QScopedPointer<ConnectionHandler> handler(
            ConnectionHandlerFactory::create(settings));
        const QString typeName =
            Common::ConnectionSettings::connectionTypeToString(testCase.type);

        if (!testCase.supported) {
            QVERIFY2(handler.isNull(),
                     qPrintable(QStringLiteral("%1 类型不应创建处理器").arg(typeName)));
            continue;
        }

        QVERIFY2(!handler.isNull(),
                 qPrintable(QStringLiteral("%1 类型未创建处理器").arg(typeName)));
        QCOMPARE(QString::fromLatin1(handler->metaObject()->className()),
                 QString::fromLatin1(testCase.className));
        QVERIFY(!handler->connectionInfo().isEmpty());
    }
}

void TestIntegration::serialRejectsMissingPort()
{
    Common::ConnectionSettings settings;
    settings.type = Common::ConnectionType::Serial;
    settings.portName = QStringLiteral("__NTERMINAL_MISSING_PORT__");
    settings.baudRate = 115200;

    SerialHandler handler(settings);
    QSignalSpy errors(&handler, &ConnectionHandler::errorOccurred);
    QSignalSpy connected(&handler, &ConnectionHandler::connected);

    QVERIFY(!handler.open());
    QVERIFY(!handler.isOpen());
    QVERIFY(!handler.isConnected());
    QCOMPARE(connected.count(), 0);
    QVERIFY(!errors.isEmpty());
}

void TestIntegration::serialConsoleLogin()
{
    const QString requestedPort = envValue("NTERMINAL_TEST_SERIAL_PORT");
    if (requestedPort.isEmpty()) {
        QSKIP("未设置 NTERMINAL_TEST_SERIAL_PORT，跳过真实串口测试");
    }

    QStringList availablePorts;
    QString portName = requestedPort;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        availablePorts.append(info.portName());
        if (info.portName().compare(requestedPort, Qt::CaseInsensitive) == 0) {
            portName = info.portName();
        }
    }
    QVERIFY2(availablePorts.contains(portName, Qt::CaseInsensitive),
             qPrintable(QStringLiteral("串口 %1 不存在；当前可用：%2")
                            .arg(requestedPort, availablePorts.join(QStringLiteral(", ")))));

    const QString user = envValue("NTERMINAL_TEST_SERIAL_USER");
    const QString password = envValue("NTERMINAL_TEST_SERIAL_PASSWORD");

    Common::ConnectionSettings settings;
    settings.type = Common::ConnectionType::Serial;
    settings.portName = portName;
    settings.baudRate = envInt("NTERMINAL_TEST_SERIAL_BAUD", 115200);
    settings.dataBits = QSerialPort::Data8;
    settings.parity = QSerialPort::NoParity;
    settings.stopBits = QSerialPort::OneStop;
    settings.flowControl = QSerialPort::NoFlowControl;

    SerialHandler handler(settings);
    QByteArray transcript;
    connect(&handler, &ConnectionHandler::dataReceived, this,
            [&transcript](const QByteArray &data) { transcript.append(data); });
    QSignalSpy errors(&handler, &ConnectionHandler::errorOccurred);
    QSignalSpy connected(&handler, &ConnectionHandler::connected);
    QSignalSpy disconnected(&handler, &ConnectionHandler::disconnected);

    QVERIFY2(handler.open(),
             qPrintable(QStringLiteral("打开串口 %1 失败：%2")
                            .arg(portName, errors.isEmpty()
                                     ? QStringLiteral("无错误详情")
                                     : errors.first().first().toString())));
    QCOMPARE(connected.count(), 1);
    QVERIFY(handler.isOpen());
    QVERIFY(handler.isConnected());
    QVERIFY(handler.connectionInfo().contains(portName));

    // 敲一个回车唤醒控制台；有的设备已经登录，有的还停在 login: 提示。
    handler.sendData("\r");
    const bool sawPrompt = waitUntil(
        [&transcript]() {
            return containsText(transcript, "login:")
                   || containsText(transcript, "password:")
                   || containsText(transcript, "#") || containsText(transcript, "$");
        },
        10000);
    QVERIFY2(sawPrompt,
             qPrintable(QString::fromLatin1(
                 transcriptTail(transcript, password))));

    QString promptText = QString::fromLatin1(transcript);
    const bool needsLogin = promptText.contains(QStringLiteral("login:"),
                                                Qt::CaseInsensitive);
    bool needsPassword = promptText.contains(QStringLiteral("password:"),
                                             Qt::CaseInsensitive);
    if (needsLogin) {
        QVERIFY2(!user.isEmpty() && !password.isEmpty(),
                 "串口要求登录，但未设置 NTERMINAL_TEST_SERIAL_USER/PASSWORD");
        handler.sendData(user.toUtf8() + "\r");
        const bool sawPassword = waitUntil(
            [&transcript]() { return containsText(transcript, "password:"); },
            10000);
        QVERIFY2(sawPassword,
                 qPrintable(QString::fromLatin1(
                     transcriptTail(transcript, password))));
        needsPassword = true;
    }

    if (needsPassword) {
        QVERIFY2(!password.isEmpty(),
                 "串口要求密码，但未设置 NTERMINAL_TEST_SERIAL_PASSWORD");
        handler.sendData(password.toUtf8() + "\r");
        // 等 shell 提示符；即便没有可识别的提示符，也继续发送命令，
        // 由命令回显做最终判定（部分嵌入式控制台提示符很特殊）。
        waitUntil(
            [&transcript]() {
                return containsText(transcript, "#") || containsText(transcript, "$");
            },
            15000);
    }

    // 用算术展开确认命令真的被执行，而不只是终端回显了输入。
    handler.sendData("echo NTERMINAL_SERIAL_$((6*7))\r");
    const bool commandRan = waitUntil(
        [&transcript]() { return transcript.contains("NTERMINAL_SERIAL_42"); },
        15000);
    QVERIFY2(commandRan,
             qPrintable(QString::fromLatin1(
                 transcriptTail(transcript, password))));

    handler.close();
    QVERIFY(!handler.isOpen());
    QVERIFY(!handler.isConnected());
    QVERIFY(disconnected.count() >= 1);
}

void TestIntegration::sshHandlerLoginAndStats()
{
    const Common::ConnectionSettings settings = sshTestSettings();
    const QString skipReason = sshSkipReason(settings);
    if (!skipReason.isEmpty()) {
        QSKIP(qPrintable(skipReason));
    }

    SshHandler handler(settings);
    QByteArray transcript;
    connect(&handler, &ConnectionHandler::dataReceived, this,
            [&transcript](const QByteArray &data) { transcript.append(data); });
    QSignalSpy errors(&handler, &ConnectionHandler::errorOccurred);
    QSignalSpy connected(&handler, &ConnectionHandler::connected);
    QSignalSpy disconnected(&handler, &ConnectionHandler::disconnected);
    QSignalSpy statsSpy(&handler, &ConnectionHandler::systemStatsUpdated);

    QVERIFY2(handler.open(),
             qPrintable(QStringLiteral("SSH 连接 %1:%2 失败：%3")
                            .arg(settings.host)
                            .arg(settings.port)
                            .arg(errors.isEmpty()
                                     ? QStringLiteral("无错误详情")
                                     : errors.first().first().toString())));
    QCOMPARE(connected.count(), 1);
    QVERIFY(handler.isOpen());
    QVERIFY(handler.isConnected());
    QVERIFY(handler.connectionInfo().contains(settings.host));
    QVERIFY(handler.connectionInfo().contains(settings.username));

    handler.setTerminalSize(120, 40);

    handler.sendData("echo NTERMINAL_SSH_$((40+2))\n");
    const bool commandRan = waitUntil(
        [&transcript]() { return transcript.contains("NTERMINAL_SSH_42"); },
        15000);
    QVERIFY2(commandRan,
             qPrintable(QString::fromLatin1(
                 transcriptTail(transcript, settings.password))));

    const bool gotStats = waitUntil([&statsSpy]() { return statsSpy.count() > 0; },
                                    15000);
    QVERIFY2(gotStats, "SSH 已登录，但 15 秒内未收到 systemStatsUpdated");
    const QJsonObject stats = statsSpy.first().first().toJsonObject();
    QVERIFY(stats.contains(QStringLiteral("host")));
    QVERIFY(!stats.value(QStringLiteral("host")).toString().isEmpty());
    QVERIFY(stats.value(QStringLiteral("memTotal")).toDouble() > 0);

    handler.close();
    QVERIFY(!handler.isOpen());
    QVERIFY(!handler.isConnected());
    QVERIFY(disconnected.count() >= 1);
}

void TestIntegration::sshRejectsWrongPassword()
{
    Common::ConnectionSettings settings = sshTestSettings();
    const QString skipReason = sshSkipReason(settings);
    if (!skipReason.isEmpty()) {
        QSKIP(qPrintable(skipReason));
    }
    settings.password += QStringLiteral("-nterminal-wrong-password");

    SshFileClient client(settings, false);
    QString home;
    QString error;
    QElapsedTimer timer;
    timer.start();
    const bool connected = client.connect(&home, &error);

    QVERIFY2(!connected, "错误密码不应通过 SSH 认证");
    QVERIFY(!error.isEmpty());
    QVERIFY2(timer.elapsed() < 30000,
             qPrintable(QStringLiteral("错误密码认证耗时异常：%1 ms")
                            .arg(timer.elapsed())));
}

void TestIntegration::sftpFileLifecycle()
{
    const Common::ConnectionSettings settings = sshTestSettings();
    const QString skipReason = sshSkipReason(settings);
    if (!skipReason.isEmpty()) {
        QSKIP(qPrintable(skipReason));
    }

    SshFileClient client(settings, false);
    QString home;
    QString error;
    QVERIFY2(client.connect(&home, &error),
             qPrintable(QStringLiteral("SFTP 连接失败：%1").arg(error)));
    QVERIFY(!home.isEmpty());
    QVERIFY(client.supportsDirectoryTransfer());

    QString remoteDir = home;
    if (!remoteDir.endsWith(QLatin1Char('/'))) {
        remoteDir += QLatin1Char('/');
    }
    remoteDir += QStringLiteral("nterminal_it_%1_%2")
                     .arg(QCoreApplication::applicationPid())
                     .arg(QDateTime::currentMSecsSinceEpoch());

    QVERIFY2(client.makeDirectory(remoteDir, &error),
             qPrintable(QStringLiteral("创建远端目录失败：%1").arg(error)));
    RemoteDirectoryGuard guard{ &client, remoteDir };

    // 已存在的目录应视为成功（递归上传依赖这一行为）。
    QVERIFY2(client.makeDirectory(remoteDir, &error),
             qPrintable(QStringLiteral("重复创建目录失败：%1").arg(error)));

    QVector<RemoteFileEntry> entries;
    QVERIFY2(client.listDirectory(home, &entries, &error),
             qPrintable(QStringLiteral("列出远端家目录失败：%1").arg(error)));
    const int dirIndex = indexOfEntry(entries, QFileInfo(remoteDir).fileName());
    QVERIFY2(dirIndex >= 0, "远端家目录中未找到新建的测试目录");
    QVERIFY(entries.at(dirIndex).isDir);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QByteArray payload = makePayload(150 * 1024 + 37);
    const QString localSource = tempDir.filePath(QStringLiteral("upload.bin"));
    {
        QFile file(localSource);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(payload), qint64(payload.size()));
    }

    const QString remotePath = remoteDir + QStringLiteral("/payload.bin");
    QVector<QPair<qint64, qint64>> uploadProgress;
    const bool uploaded = client.uploadFile(
        localSource, remotePath,
        [&uploadProgress](qint64 done, qint64 total) {
            uploadProgress.append(qMakePair(done, total));
            return true;
        },
        &error);
    QVERIFY2(uploaded,
             qPrintable(QStringLiteral("SFTP 上传失败：%1").arg(error)));
    QVERIFY(!uploadProgress.isEmpty());
    QCOMPARE(uploadProgress.first().first, qint64(0));
    QCOMPARE(uploadProgress.first().second, qint64(payload.size()));
    QCOMPARE(uploadProgress.last().first, qint64(payload.size()));

    QVERIFY2(client.listDirectory(remoteDir, &entries, &error),
             qPrintable(QStringLiteral("列出上传目录失败：%1").arg(error)));
    int fileIndex = indexOfEntry(entries, QStringLiteral("payload.bin"));
    QVERIFY2(fileIndex >= 0, "上传后目录中未找到文件");
    QVERIFY(!entries.at(fileIndex).isDir);
    QCOMPARE(entries.at(fileIndex).size, qint64(payload.size()));

    const QString localDownload = tempDir.filePath(QStringLiteral("download.bin"));
    QVector<QPair<qint64, qint64>> downloadProgress;
    const bool downloaded = client.downloadFile(
        remotePath, localDownload,
        [&downloadProgress](qint64 done, qint64 total) {
            downloadProgress.append(qMakePair(done, total));
            return true;
        },
        &error);
    QVERIFY2(downloaded,
             qPrintable(QStringLiteral("SFTP 下载失败：%1").arg(error)));
    QVERIFY(!downloadProgress.isEmpty());
    QCOMPARE(downloadProgress.first().first, qint64(0));
    QCOMPARE(downloadProgress.first().second, qint64(payload.size()));
    QCOMPARE(downloadProgress.last().first, qint64(payload.size()));
    {
        QFile file(localDownload);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray roundTrip = file.readAll();
        QVERIFY2(roundTrip == payload, "SFTP 下载内容与上传内容不一致");
    }

    // 下载刚开始就取消：本地半成品必须清理掉。
    const QString canceledDownload = tempDir.filePath(QStringLiteral("canceled.bin"));
    QString cancelError;
    const bool downloadCanceled = client.downloadFile(
        remotePath, canceledDownload,
        [](qint64, qint64) { return false; }, &cancelError);
    QVERIFY(!downloadCanceled);
    QCOMPARE(cancelError, QStringLiteral("已取消"));
    QVERIFY(!QFileInfo::exists(canceledDownload));

    const QString renamedPath = remoteDir + QStringLiteral("/renamed.bin");
    QVERIFY2(client.renameEntry(remotePath, renamedPath, &error),
             qPrintable(QStringLiteral("重命名失败：%1").arg(error)));
    QVERIFY2(client.listDirectory(remoteDir, &entries, &error),
             qPrintable(QStringLiteral("重命名后列出目录失败：%1").arg(error)));
    QCOMPARE(indexOfEntry(entries, QStringLiteral("payload.bin")), -1);
    fileIndex = indexOfEntry(entries, QStringLiteral("renamed.bin"));
    QVERIFY(fileIndex >= 0);
    QCOMPARE(entries.at(fileIndex).size, qint64(payload.size()));

    RemoteTransferItem fileItem;
    fileItem.name = QStringLiteral("renamed.bin");
    fileItem.path = renamedPath;
    fileItem.isDir = false;
    fileItem.size = payload.size();
    fileItem.mode = entries.at(fileIndex).mode;
    QVERIFY2(client.removeEntry(fileItem, &error),
             qPrintable(QStringLiteral("删除远端文件失败：%1").arg(error)));
    QVERIFY2(client.listDirectory(remoteDir, &entries, &error),
             qPrintable(QStringLiteral("删除后列出目录失败：%1").arg(error)));
    QCOMPARE(indexOfEntry(entries, QStringLiteral("renamed.bin")), -1);

    const QString missingLocal = tempDir.filePath(QStringLiteral("missing.bin"));
    const bool missingDownloaded = client.downloadFile(
        remoteDir + QStringLiteral("/not-exists-nterminal.bin"), missingLocal,
        [](qint64, qint64) { return true; }, &error);
    QVERIFY(!missingDownloaded);
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(missingLocal));

    // 覆盖已有文件时，如果用户在传输开始前就取消，远端原文件必须保留，
    // 不能被 O_TRUNC 截成空文件。
    const QByteArray original = QByteArray("ORIGINAL-CONTENT-1234567890");
    const QString keepSource = tempDir.filePath(QStringLiteral("keep-source.bin"));
    const QString keepRemote = remoteDir + QStringLiteral("/keep.bin");
    {
        QFile file(keepSource);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), qint64(original.size()));
    }
    QVERIFY2(client.uploadFile(keepSource, keepRemote,
                               [](qint64, qint64) { return true; }, &error),
             qPrintable(QStringLiteral("准备覆盖文件失败：%1").arg(error)));
    const bool uploadCanceled = client.uploadFile(
        localSource, keepRemote, [](qint64, qint64) { return false; }, &cancelError);
    QVERIFY(!uploadCanceled);
    QCOMPARE(cancelError, QStringLiteral("已取消"));
    const QString keepDownload = tempDir.filePath(QStringLiteral("keep-download.bin"));
    QVERIFY2(client.downloadFile(keepRemote, keepDownload,
                                 [](qint64, qint64) { return true; }, &error),
             qPrintable(QStringLiteral("取消后回读原文件失败：%1").arg(error)));
    {
        QFile file(keepDownload);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY2(file.readAll() == original,
                 "传输开始前取消上传破坏了远端已有文件");
    }
}

void TestIntegration::scpFileTransfer()
{
    const Common::ConnectionSettings settings = sshTestSettings();
    const QString skipReason = sshSkipReason(settings);
    if (!skipReason.isEmpty()) {
        QSKIP(qPrintable(skipReason));
    }

    SshFileClient client(settings, true);
    QString home;
    QString error;
    QVERIFY2(client.connect(&home, &error),
             qPrintable(QStringLiteral("SCP 连接失败：%1").arg(error)));
    QVERIFY(!home.isEmpty());
    QVERIFY(!client.supportsDirectoryTransfer());

    QString remoteDir = home;
    if (!remoteDir.endsWith(QLatin1Char('/'))) {
        remoteDir += QLatin1Char('/');
    }
    remoteDir += QStringLiteral("nterminal_scp_%1_%2")
                     .arg(QCoreApplication::applicationPid())
                     .arg(QDateTime::currentMSecsSinceEpoch());
    QVERIFY2(client.makeDirectory(remoteDir, &error),
             qPrintable(QStringLiteral("创建 SCP 测试目录失败：%1").arg(error)));
    RemoteDirectoryGuard guard{ &client, remoteDir };

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QByteArray payload = makePayload(70 * 1024 + 5);
    const QString localSource = tempDir.filePath(QStringLiteral("scp-upload.bin"));
    {
        QFile file(localSource);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(payload), qint64(payload.size()));
    }

    const QString remotePath = remoteDir + QStringLiteral("/scp.bin");
    QVector<QPair<qint64, qint64>> uploadProgress;
    const bool uploaded = client.uploadFile(
        localSource, remotePath,
        [&uploadProgress](qint64 done, qint64 total) {
            uploadProgress.append(qMakePair(done, total));
            return true;
        },
        &error);
    QVERIFY2(uploaded,
             qPrintable(QStringLiteral("SCP 上传失败：%1").arg(error)));
    QVERIFY(!uploadProgress.isEmpty());
    QCOMPARE(uploadProgress.first().first, qint64(0));
    QCOMPARE(uploadProgress.first().second, qint64(payload.size()));
    QCOMPARE(uploadProgress.last().first, qint64(payload.size()));

    // 浏览仍走 SFTP，可顺便验证 SCP 写入的文件大小。
    QVector<RemoteFileEntry> entries;
    QVERIFY2(client.listDirectory(remoteDir, &entries, &error),
             qPrintable(QStringLiteral("列出 SCP 测试目录失败：%1").arg(error)));
    const int fileIndex = indexOfEntry(entries, QStringLiteral("scp.bin"));
    QVERIFY2(fileIndex >= 0, "SCP 上传后目录中未找到文件");
    QCOMPARE(entries.at(fileIndex).size, qint64(payload.size()));

    const QString localDownload = tempDir.filePath(QStringLiteral("scp-download.bin"));
    QVector<QPair<qint64, qint64>> downloadProgress;
    const bool downloaded = client.downloadFile(
        remotePath, localDownload,
        [&downloadProgress](qint64 done, qint64 total) {
            downloadProgress.append(qMakePair(done, total));
            return true;
        },
        &error);
    QVERIFY2(downloaded,
             qPrintable(QStringLiteral("SCP 下载失败：%1").arg(error)));
    QVERIFY(!downloadProgress.isEmpty());
    QCOMPARE(downloadProgress.last().first, qint64(payload.size()));
    {
        QFile file(localDownload);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY2(file.readAll() == payload, "SCP 下载内容与上传内容不一致");
    }

    RemoteTransferItem item;
    item.name = QStringLiteral("scp.bin");
    item.path = remotePath;
    item.isDir = false;
    item.size = payload.size();
    item.mode = entries.at(fileIndex).mode;
    QVERIFY2(client.removeEntry(item, &error),
             qPrintable(QStringLiteral("删除 SCP 测试文件失败：%1").arg(error)));
}

QTEST_GUILESS_MAIN(TestIntegration)
#include "tst_integration.moc"
