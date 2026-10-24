#ifndef XMODEMTRANSFER_H
#define XMODEMTRANSFER_H

#include "transfer/filetransfer.h"

class QFile;
class QTimer;

// XMODEM（128 字节块，CRC-16；兼容 lrzsz 的 sx/rx -X）
class XmodemTransfer : public FileTransfer
{
    Q_OBJECT

public:
    XmodemTransfer(Direction direction, const QStringList &paths,
                   QObject *parent = nullptr);
    ~XmodemTransfer() override;

    void start() override;
    void cancel() override;
    void onData(const QByteArray &data) override;

private:
    enum class State {
        Idle,
        WaitStart,     // 发送：等接收方的 'C' / NAK
        WaitAck,       // 发送：等块 ACK
        WaitEotAck,    // 发送：等 EOT 的 ACK
        Receiving      // 接收：收块
    };

    void startSend();
    void startReceive();
    void beginSending();
    void sendNextBlock();
    void resendLast();
    void sendEot();
    void handleSendBytes(const QByteArray &data);
    void handleReceiveBytes();
    void handleBlock(const QByteArray &packet, int headerOffset);
    void sendAck();
    void sendNak();
    void armTimer(int milliseconds);
    void onTimeout();
    void abortTransfer(const QString &reason);

    QFile *m_file = nullptr;
    QTimer *m_timer = nullptr;
    State m_state = State::Idle;

    // 发送
    QByteArray m_lastPacket;
    qint64 m_blockOffset = 0;
    int m_lastPayloadSize = 0;
    quint8 m_blockSequence = 1;
    bool m_useCrc = true;
    bool m_lastBlock = false;
    int m_retries = 0;
    int m_handshakeTries = 0;
    int m_canCount = 0;

    // 接收
    QByteArray m_rxBuffer;
    quint8 m_expectedSequence = 0;
    qint64 m_receivedBytes = 0;
    bool m_printedFirstBlock = false;
};

#endif // XMODEMTRANSFER_H
