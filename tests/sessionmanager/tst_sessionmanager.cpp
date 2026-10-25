// ============================================================================
// tst_sessionmanager —— 会话持久化测试
//
// 覆盖：
//   1. 增删改查与信号
//   2. save/load JSON 往返（含全部 ConnectionSettings 字段）
//   3. 历史格式兼容（缺 sessionName / 缺 port / 非对象项 / 缺 settings）
//   4. 损坏配置的错误上报，且不破坏内存中的既有列表
//   5. 析构自动保存、按 lastUsed 排序
//
// 所有用例使用临时配置文件，绝不触碰 ~/.nterminal/sessions.json。
// ============================================================================
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QSignalSpy>
#include "services/sessionmanager.h"

class TestSessionManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void loadMissingFileIsEmpty();
    void addUpdateRemoveSignals();
    void saveLoadRoundTrip();
    void destructorPersistsSessions();
    void sortsByLastUsedDescending();
    void legacySessionsGetDefaults();
    void invalidRootReportsErrorAndKeepsList();
    void invalidJsonReportsError();
    void skipsMalformedEntries();
    void saveFailureReportsError();

private:
    QString configPath(const QString &name) const;
    void writeRaw(const QString &path, const QByteArray &data) const;

    QTemporaryDir m_tempDir;
};

void TestSessionManager::initTestCase()
{
    QVERIFY2(m_tempDir.isValid(), qPrintable(m_tempDir.errorString()));
}

QString TestSessionManager::configPath(const QString &name) const
{
    return m_tempDir.filePath(name + QStringLiteral("/sessions.json"));
}

void TestSessionManager::writeRaw(const QString &path, const QByteArray &data) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(data);
    file.close();
}

void TestSessionManager::loadMissingFileIsEmpty()
{
    const QString path = configPath(QStringLiteral("missing"));
    SessionManager manager(path);
    QCOMPARE(manager.getConfigFilePath(), path);
    QVERIFY(manager.loadSessions());
    QVERIFY(manager.getSessions().isEmpty());
    QCOMPARE(manager.getSession(QStringLiteral("none")).sessionName,
             QString("none"));
}

void TestSessionManager::addUpdateRemoveSignals()
{
    const QString path = configPath(QStringLiteral("crud"));
    SessionManager manager(path);
    QSignalSpy added(&manager, &SessionManager::sessionAdded);
    QSignalSpy updated(&manager, &SessionManager::sessionUpdated);
    QSignalSpy removed(&manager, &SessionManager::sessionRemoved);

    Common::ConnectionSettings settings;
    settings.sessionName = QStringLiteral("S1");
    settings.type = Common::ConnectionType::Telnet;
    settings.host = QStringLiteral("10.0.0.1");

    manager.addSession(QStringLiteral("S1"), settings);
    QCOMPARE(added.count(), 1);
    QCOMPARE(manager.getSessions().size(), 1);
    QCOMPARE(manager.getSession(QStringLiteral("S1")).host, QString("10.0.0.1"));

    // 同名添加视为更新，不产生重复项
    settings.host = QStringLiteral("10.0.0.2");
    manager.addSession(QStringLiteral("S1"), settings);
    QCOMPARE(added.count(), 1);
    QCOMPARE(updated.count(), 1);
    QCOMPARE(manager.getSessions().size(), 1);
    QCOMPARE(manager.getSession(QStringLiteral("S1")).host, QString("10.0.0.2"));

    // update 不存在的会话会转为新增
    manager.updateSession(QStringLiteral("S2"), settings);
    QCOMPARE(added.count(), 2);
    QCOMPARE(manager.getSessions().size(), 2);

    manager.removeSession(QStringLiteral("S1"));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(manager.getSessions().size(), 1);

    // 删除不存在的会话不产生信号
    manager.removeSession(QStringLiteral("nope"));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(manager.getSessions().size(), 1);
}

void TestSessionManager::saveLoadRoundTrip()
{
    const QString path = configPath(QStringLiteral("roundtrip"));
    {
        SessionManager writer(path);

        Common::ConnectionSettings ssh;
        ssh.sessionName = QStringLiteral("prod-ssh");
        ssh.type = Common::ConnectionType::SSH;
        ssh.host = QStringLiteral("198.51.100.20");
        ssh.port = 2222;
        ssh.username = QStringLiteral("testuser");
        ssh.password = QStringLiteral("pw");
        ssh.useSshKey = true;
        ssh.sshKeyPath = QStringLiteral("C:/keys/id_ed25519");

        Common::ConnectionSettings serial;
        serial.sessionName = QStringLiteral("console");
        serial.type = Common::ConnectionType::Serial;
        serial.portName = QStringLiteral("COM3");
        serial.baudRate = 115200;
        serial.dataBits = QSerialPort::Data7;
        serial.parity = QSerialPort::EvenParity;
        serial.stopBits = QSerialPort::OneAndHalfStop;
        serial.flowControl = QSerialPort::HardwareControl;

        Common::ConnectionSettings rdp;
        rdp.sessionName = QStringLiteral("winbox");
        rdp.type = Common::ConnectionType::RDP;
        rdp.host = QStringLiteral("10.1.1.5");
        rdp.port = 3389;
        rdp.rdpDomain = QStringLiteral("LAB");
        rdp.rdpUseNla = true;

        writer.addSession(ssh.sessionName, ssh);
        writer.addSession(serial.sessionName, serial);
        writer.addSession(rdp.sessionName, rdp);
        QVERIFY(writer.saveSessions());
    }

    // 文件必须是 JSON 数组
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(doc.isArray());
    QCOMPARE(doc.array().size(), 3);

    SessionManager reader(path);
    QVERIFY(reader.loadSessions());
    const QList<SessionManager::Session> sessions = reader.getSessions();
    QCOMPARE(sessions.size(), 3);

    const Common::ConnectionSettings ssh = reader.getSession(QStringLiteral("prod-ssh"));
    QCOMPARE(ssh.type, Common::ConnectionType::SSH);
    QCOMPARE(ssh.host, QString("198.51.100.20"));
    QCOMPARE(ssh.port, quint16(2222));
    QCOMPARE(ssh.username, QString("testuser"));
    QCOMPARE(ssh.password, QString("pw"));
    QCOMPARE(ssh.useSshKey, true);
    QCOMPARE(ssh.sshKeyPath, QString("C:/keys/id_ed25519"));

    const Common::ConnectionSettings serial = reader.getSession(QStringLiteral("console"));
    QCOMPARE(serial.portName, QString("COM3"));
    QCOMPARE(serial.baudRate, 115200);
    QCOMPARE(serial.dataBits, QSerialPort::Data7);
    QCOMPARE(serial.parity, QSerialPort::EvenParity);
    QCOMPARE(serial.stopBits, QSerialPort::OneAndHalfStop);
    QCOMPARE(serial.flowControl, QSerialPort::HardwareControl);

    const Common::ConnectionSettings rdp = reader.getSession(QStringLiteral("winbox"));
    QCOMPARE(rdp.rdpDomain, QString("LAB"));
    QCOMPARE(rdp.rdpUseNla, true);
}

void TestSessionManager::destructorPersistsSessions()
{
    const QString path = configPath(QStringLiteral("destructor"));
    {
        SessionManager manager(path);
        Common::ConnectionSettings settings;
        settings.type = Common::ConnectionType::SSH;
        settings.host = QStringLiteral("example.test");
        manager.addSession(QStringLiteral("auto-save"), settings);
        // 不显式调用 saveSessions()
    }
    QVERIFY(QFile::exists(path));

    SessionManager reloaded(path);
    QVERIFY(reloaded.loadSessions());
    QCOMPARE(reloaded.getSessions().size(), 1);
    QCOMPARE(reloaded.getSession(QStringLiteral("auto-save")).host,
             QString("example.test"));
}

void TestSessionManager::sortsByLastUsedDescending()
{
    const QString path = configPath(QStringLiteral("sorting"));
    const QByteArray json = R"JSON([
        {"name":"old","lastUsed":"2020-01-01T00:00:00","settings":{"type":"SSH"}},
        {"name":"new","lastUsed":"2026-01-01T00:00:00","settings":{"type":"SSH"}},
        {"name":"middle","lastUsed":"2024-06-01T12:00:00","settings":{"type":"SSH"}},
        {"name":"no-time","settings":{"type":"SSH"}}
    ])JSON";
    writeRaw(path, json);

    SessionManager manager(path);
    QVERIFY(manager.loadSessions());
    const QList<SessionManager::Session> sessions = manager.getSessions();
    QCOMPARE(sessions.size(), 4);
    QCOMPARE(sessions.at(0).name, QString("no-time"));   // 缺失时间按当前时间处理
    QCOMPARE(sessions.at(1).name, QString("new"));
    QCOMPARE(sessions.at(2).name, QString("middle"));
    QCOMPARE(sessions.at(3).name, QString("old"));
}

void TestSessionManager::legacySessionsGetDefaults()
{
    const QString path = configPath(QStringLiteral("legacy"));
    const QByteArray json = R"JSON([
        {"name":"ssh-no-port","settings":{"type":"SSH","host":"a"}},
        {"name":"telnet-no-port","settings":{"type":"Telnet","host":"b"}},
        {"name":"serial-no-port","settings":{"type":"Serial","portName":"COM1"}},
        {"name":"named","settings":{"type":"SSH","sessionName":"Display Name"}},
        {"name":"legacy-enums","settings":{"type":"Serial","dataBits":"7","parity":"EvenParity","flowControl":"HardwareControl"}}
    ])JSON";
    writeRaw(path, json);

    SessionManager manager(path);
    QSignalSpy loaded(&manager, &SessionManager::sessionsLoaded);
    QVERIFY(manager.loadSessions());
    QCOMPARE(loaded.count(), 1);

    QCOMPARE(manager.getSession(QStringLiteral("ssh-no-port")).port, quint16(22));
    QCOMPARE(manager.getSession(QStringLiteral("telnet-no-port")).port, quint16(23));
    QCOMPARE(manager.getSession(QStringLiteral("serial-no-port")).port, quint16(23));

    // settings 缺 sessionName 时回退为会话名；已存在时保留显示名
    QCOMPARE(manager.getSession(QStringLiteral("ssh-no-port")).sessionName,
             QString("ssh-no-port"));
    QCOMPARE(manager.getSession(QStringLiteral("named")).sessionName,
             QString("Display Name"));

    const Common::ConnectionSettings legacy = manager.getSession(QStringLiteral("legacy-enums"));
    QCOMPARE(legacy.dataBits, QSerialPort::Data7);
    QCOMPARE(legacy.parity, QSerialPort::EvenParity);
    QCOMPARE(legacy.flowControl, QSerialPort::HardwareControl);
}

void TestSessionManager::invalidRootReportsErrorAndKeepsList()
{
    const QString path = configPath(QStringLiteral("invalid-root"));
    writeRaw(path, R"JSON({"not":"an array"})JSON");

    SessionManager manager(path);
    Common::ConnectionSettings existing;
    existing.host = QStringLiteral("keep.me");
    manager.addSession(QStringLiteral("existing"), existing);

    QSignalSpy errors(&manager, &SessionManager::errorOccurred);
    QVERIFY(!manager.loadSessions());
    QCOMPARE(errors.count(), 1);
    // 解析失败不应清空已有列表
    QCOMPARE(manager.getSessions().size(), 1);
    QCOMPARE(manager.getSession(QStringLiteral("existing")).host,
             QString("keep.me"));
}

void TestSessionManager::invalidJsonReportsError()
{
    const QString path = configPath(QStringLiteral("invalid-json"));
    writeRaw(path, "{broken");

    SessionManager manager(path);
    QSignalSpy errors(&manager, &SessionManager::errorOccurred);
    QVERIFY(!manager.loadSessions());
    QCOMPARE(errors.count(), 1);
}

void TestSessionManager::skipsMalformedEntries()
{
    const QString path = configPath(QStringLiteral("malformed"));
    const QByteArray json = R"JSON([
        "not-an-object",
        {"settings":{"type":"SSH"}},
        {"name":123,"settings":{"type":"SSH"}},
        {"name":"no-settings"},
        {"name":"settings-not-object","settings":"x"},
        {"name":"good","settings":{"type":"SSH","host":"ok"}}
    ])JSON";
    writeRaw(path, json);

    SessionManager manager(path);
    QVERIFY(manager.loadSessions());
    QCOMPARE(manager.getSessions().size(), 1);
    QCOMPARE(manager.getSessions().first().name, QString("good"));
}

void TestSessionManager::saveFailureReportsError()
{
    // 把"配置文件"指向一个已存在的目录，写入必然失败
    const QString dirPath = m_tempDir.filePath(QStringLiteral("save-failure-dir"));
    QDir().mkpath(dirPath);

    SessionManager manager(dirPath);
    QSignalSpy errors(&manager, &SessionManager::errorOccurred);
    QVERIFY(!manager.saveSessions());
    QCOMPARE(errors.count(), 1);
}

QTEST_GUILESS_MAIN(TestSessionManager)
#include "tst_sessionmanager.moc"
