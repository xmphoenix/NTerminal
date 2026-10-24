#ifndef FILETRANSFER_H
#define FILETRANSFER_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

// ============================================================================
// 终端文件传输协议基类
//
// 协议引擎只做字节流状态机：发送用 setWriter() 注入的写回调（终端会话），
// 接收数据由 TerminalProxy 的入站过滤器转交 onData()。这样引擎不依赖
// 终端/会话的具体实现，既能在 GUI 里用，也能在独立测试程序里接 TCP 验证。
// ============================================================================
class FileTransfer : public QObject
{
    Q_OBJECT

public:
    enum class Kind {
        Zmodem,
        Ymodem,
        Xmodem,
        Kermit,
        Ascii
    };
    enum class Direction {
        Send,
        Receive
    };

    FileTransfer(Kind kind, Direction direction, const QStringList &paths,
                 QObject *parent = nullptr);
    ~FileTransfer() override;

    Kind kind() const { return m_kind; }
    Direction direction() const { return m_direction; }
    QString protocolName() const;
    QString directionName() const;
    QString fileSummary() const;
    const QStringList &paths() const { return m_paths; }

    // 开始传输（调用后协议开始等待/发送）
    virtual void start() = 0;
    // 用户取消：协议应尽量通知对端并结束
    virtual void cancel() = 0;
    // 会话收到的原始字节
    virtual void onData(const QByteArray &data) = 0;

    void setWriter(std::function<void(const QByteArray &)> writer);

    qint64 transferred() const { return m_transferred; }
    qint64 total() const { return m_total; }
    QString statusText() const { return m_status; }
    bool isFinished() const { return m_finished; }
    QString errorString() const { return m_error; }

signals:
    void progress(qint64 transferred, qint64 total);
    void statusChanged(const QString &status);
    void logMessage(const QString &line);
    void finished(bool success, const QString &message);

protected:
    void write(const QByteArray &data);
    void setStatus(const QString &status);
    void setProgress(qint64 transferred, qint64 total);
    void log(const QString &line);
    void finish(bool success, const QString &message);
    void fail(const QString &message) { finish(false, message); }

    // 接收侧：目录（ZMODEM/YMODEM/Kermit）或目标文件（XMODEM/ASCII）
    QString receiveDirectory() const;
    QString receiveFile() const;
    // 去掉对端给出的路径成分，避免写出目标目录
    static QString sanitizeName(const QString &name);

    QStringList m_paths;
    qint64 m_transferred = 0;
    qint64 m_total = 0;
    QString m_status;
    QString m_error;
    bool m_finished = false;

private:
    Kind m_kind;
    Direction m_direction;
    std::function<void(const QByteArray &)> m_writer;
};

#endif // FILETRANSFER_H
