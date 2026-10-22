// ============================================================================
// tst_connectionhandler —— src/session/connectionhandler 接口契约测试
//
// 用最小 TestHandler 实现验证抽象接口的可实现性与行为契约：
//   - 纯虚接口集合（open/close/sendData/...）可被子类完整实现
//   - dataReceived/connected/disconnected/errorOccurred 信号可从子类发射
//   - setTerminalSize 默认实现为安全空操作
//   - QObject 父子所有权正常
//
// 注：ConnectionHandlerFactory 按类型创建具体协议实现（含 libssh 依赖），
// 其分发正确性由产品完整构建与联调验证，不在无 libssh 的单元测试中链接。
// ============================================================================
#include <QtTest>
#include <QSignalSpy>
#include "session/connectionhandler.h"

// 测试用最小实现：记录调用并转发数据
class TestHandler : public ConnectionHandler
{
    Q_OBJECT
public:
    using ConnectionHandler::ConnectionHandler;

    bool open() override { m_open = true; emit connected(); return true; }
    void close() override { m_open = false; m_connected = false; emit disconnected(); }
    bool isOpen() const override { return m_open; }
    bool isConnected() const override { return m_connected; }
    void sendData(const QByteArray &data) override { m_lastSent = data; emit dataReceived(data); }
    QString connectionInfo() const override { return QStringLiteral("test-handler"); }
    void setTerminalSize(int columns, int rows) override { m_cols = columns; m_rows = rows; }

    void markConnected() { m_connected = true; }
    QByteArray m_lastSent;
    int m_cols = -1;
    int m_rows = -1;

private:
    bool m_open = false;
    bool m_connected = false;
};

class TestConnectionHandler : public QObject
{
    Q_OBJECT

private slots:
    void lifecycleContract();
    void dataThroughputContract();
    void signalContract();
    void defaultTerminalSizeIsNoOp();
    void parentOwnership();
};

void TestConnectionHandler::lifecycleContract()
{
    TestHandler handler;
    QVERIFY(!handler.isOpen());
    QVERIFY(!handler.isConnected());

    QVERIFY(handler.open());
    QVERIFY(handler.isOpen());
    handler.markConnected();
    QVERIFY(handler.isConnected());

    handler.close();
    QVERIFY(!handler.isOpen());
    QVERIFY(!handler.isConnected());
}

void TestConnectionHandler::dataThroughputContract()
{
    TestHandler handler;
    QSignalSpy received(&handler, &ConnectionHandler::dataReceived);

    handler.sendData("ls -l\r");
    QCOMPARE(handler.m_lastSent, QByteArray("ls -l\r"));
    QCOMPARE(received.count(), 1);
    QCOMPARE(received.at(0).value(0).toByteArray(), QByteArray("ls -l\r"));
}

void TestConnectionHandler::signalContract()
{
    TestHandler handler;
    QSignalSpy connected(&handler, &ConnectionHandler::connected);
    QSignalSpy disconnected(&handler, &ConnectionHandler::disconnected);
    QSignalSpy error(&handler, &ConnectionHandler::errorOccurred);

    handler.open();                       // 发射 connected
    QCOMPARE(connected.count(), 1);

    emit handler.errorOccurred(QStringLiteral("boom"));
    QCOMPARE(error.count(), 1);
    QCOMPARE(error.at(0).value(0).toString(), QString("boom"));

    handler.close();                      // 发射 disconnected
    QCOMPARE(disconnected.count(), 1);
}

void TestConnectionHandler::defaultTerminalSizeIsNoOp()
{
    // 基类默认 setTerminalSize 必须是安全空操作（不崩溃、无副作用）
    class MinimalHandler : public ConnectionHandler
    {
    public:
        using ConnectionHandler::ConnectionHandler;
        bool open() override { return true; }
        void close() override {}
        bool isOpen() const override { return false; }
        bool isConnected() const override { return false; }
        void sendData(const QByteArray &) override {}
        QString connectionInfo() const override { return QString(); }
    };

    MinimalHandler handler;
    handler.setTerminalSize(132, 43);     // 默认实现：不崩溃即通过
    QVERIFY(true);
}

void TestConnectionHandler::parentOwnership()
{
    QObject parent;
    auto *handler = new TestHandler(&parent);
    QCOMPARE(handler->parent(), &parent);
    // parent 析构时 handler 被回收（无泄漏即通过）
}

QTEST_MAIN(TestConnectionHandler)
#include "tst_connectionhandler.moc"
