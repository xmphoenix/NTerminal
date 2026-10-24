#ifndef YMODEMTRANSFER_H
#define YMODEMTRANSFER_H

#include "transfer/filetransfer.h"

class QFile;
class QTimer;

// YMODEM（1K 块 + CRC，块 0 携带文件名/大小，支持批量）
class YmodemTransfer : public FileTransfer
{
    Q_OBJECT

public:
    YmodemTransfer(Direction direction, const QStringList &paths,
                   QObject *parent = nullptr);
    ~YmodemTransfer() override;

    void start() override;
    void cancel() override;
    void onData(const QByteArray &data) override;

private:
    enum class State {
        Idle,
        WaitStart,
        WaitBlock0Ack,
        WaitDataStart,
        WaitAck,
        WaitEotAck,
        WaitBatch,
        WaitEndAck,
        WaitBlock0,
        WaitNextBlock0,
        Receiving
    };

    void startSend();
    void startReceive();
    void openCurrentFile();
    void sendBlock0();
    void sendEndBlock0();
    void sendNextDataBlock();
    void resendLast();
    void sendEot();
    void handleSendBytes(const QByteArray &data);
    void handleReceiveBytes();
    void processReceiveBlock(const QByteArray &packet, int blockSize);
    bool openReceiveFile(const QByteArray &block0);
    void finishReceive();
    void sendAck();
    void sendNak();
    void sendCrcRequest();
    void armTimer(int milliseconds);
    void onTimeout();
    void abortTransfer(const QString &reason);

    QFile *m_file = nullptr;
    QTimer *m_timer = nullptr;
    State m_state = State::Idle;

    // 发送
    QStringList m_files;
    int m_fileIndex = 0;
    qint64 m_fileSize = 0;
    qint64 m_blockOffset = 0;
    int m_lastPayloadSize = 0;
    quint8 m_sequence = 0;
    QByteArray m_lastPacket;
    int m_retries = 0;
    int m_handshakeTries = 0;
    int m_canCount = 0;
    quint64 m_sentFilesBytes = 0;

    // 接收
    QByteArray m_rxBuffer;
    quint8 m_expectedSequence = 1;
    qint64 m_fileRemaining = -1;
    qint64 m_fileReceived = 0;
    qint64 m_totalReceived = 0;
    int m_filesReceived = 0;
    int m_receiveHandshakeTries = 0;
};

#endif // YMODEMTRANSFER_H
