#ifndef KERMITTRANSFER_H
#define KERMITTRANSFER_H

#include "transfer/filetransfer.h"

class QFile;
class QTimer;

// Kermit（经典 Kermit 协议，与 ckermit 互通）
class KermitTransfer : public FileTransfer
{
    Q_OBJECT

public:
    KermitTransfer(Direction direction, const QStringList &paths,
                   QObject *parent = nullptr);
    ~KermitTransfer() override;

    void start() override;
    void cancel() override;
    void onData(const QByteArray &data) override;

    struct Packet {
        quint8 sequence = 0;
        char type = 0;
        QByteArray data;      // 已解码（S/Y 为空或参数）
        bool valid = false;
    };

private:
    enum class State {
        Idle,
        SendWaitNak,      // 发送：等接收方 NAK 触发初始化
        SendWaitInitAck,  // 发送：等 S 的 ACK
        SendWaitFileAck,  // 发送：等 F 的 ACK
        SendWaitDataAck,  // 发送：等 D 的 ACK
        SendWaitEofAck,
        SendWaitEotAck,
        RecvWaitFile,     // 接收：已发 S，等 F
        RecvReceiving
    };

    void startSend();
    void startReceive();
    void processIncoming();

    // 发送
    void sendInit();
    void sendFileHeader();
    void sendNextData();
    void sendEof();
    void sendEot();
    bool handleSendPacket(const Packet &packet);

    // 接收
    void sendInitPacket();
    void sendAck(quint8 sequence);
    void sendNak(quint8 sequence);
    void sendAckWithParams(quint8 sequence);
    void armTimer(int milliseconds);
    void onTimeout();
    bool handleReceivePacket(const Packet &packet);

    // 编解码
    static QByteArray makePacket(quint8 sequence, char type, const QByteArray &data,
                                 int checkType, bool quoteData, int maxLength);
    bool extractPacket(Packet *packet, int *consumed);
    static int paramMaxLength(const QByteArray &params);
    static int paramCheckType(const QByteArray &params);

    QFile *m_file = nullptr;
    QTimer *m_timer = nullptr;
    QByteArray m_buffer;
    State m_state = State::Idle;

    quint8 m_sequence = 0;
    int m_retries = 0;
    int m_timeoutCount = 0;
    int m_maxPacket = 94;
    int m_checkType = 1;     // 对方要求我们发送时使用的校验类型
    bool m_initSent = false;

    QStringList m_files;
    int m_fileIndex = 0;
    qint64 m_fileSize = 0;
    qint64 m_offset = 0;
    QByteArray m_lastPacket;
    qint64 m_sentBytes = 0;

    quint8 m_lastReceiveSequence = 0;
    bool m_haveLastReceive = false;
    QString m_receiveName;
    qint64 m_receivedBytes = 0;
    int m_filesReceived = 0;
};

#endif // KERMITTRANSFER_H
