#ifndef ZMODEMTRANSFER_H
#define ZMODEMTRANSFER_H

#include "transfer/filetransfer.h"

class QFile;
class QTimer;

// ZMODEM（CRC16 二进制帧，与 lrzsz 的 sz/rz 互通）
class ZmodemTransfer : public FileTransfer
{
    Q_OBJECT

public:
    ZmodemTransfer(Direction direction, const QStringList &paths,
                   QObject *parent = nullptr);
    ~ZmodemTransfer() override;

    void start() override;
    void cancel() override;
    void onData(const QByteArray &data) override;

private:
    enum class State {
        Idle,
        SendWaitRinit,
        SendWaitFileAck,
        SendWaitZack,
        SendWaitRinitAfterEof,
        SendWaitZfin,
        RecvWaitHeader,
        RecvSubpacket
    };

    enum class SubPurpose {
        FileData,
        FileInfo,
        Attention
    };

    struct Header {
        quint8 type = 0;
        QByteArray data;   // 4 字节
        bool crc32 = false;
    };

    void startSend();
    void startReceive();

    // 发送
    void sendHeader(quint8 type, quint32 data, bool hex);
    void sendZrinit();
    void sendZfile();
    void sendDataFrame();
    void sendEof();
    void sendZfin();
    void sendZack(quint32 offset);
    void sendZpos(quint32 offset);
    void handleSendHeader(const Header &header);
    void sendFileInfo();
    void armTimer(int milliseconds);
    void onTimeout();
    bool openNextFile();

    // 接收
    void processReceiveBuffer();
    bool tryParseHeader(Header *header, bool *needMore);
    void handleReceiveHeader(const Header &header);
    void feedDataByte(quint8 byte);
    void finishSubpacket();
    void openFileFromInfo();
    void sendZrinitForNextFile();

    // 编解码
    static QByteArray makeHeaderBytes(quint8 type, quint32 data, bool hex, bool crc32 = false);
    static QByteArray makeSubpacket(const QByteArray &data, quint8 marker, bool crc32 = false);
    static quint32 updateCrc32(quint8 byte, quint32 crc);
    static void appendEscaped(QByteArray &out, quint8 byte);
    static quint16 updateCrc16(quint8 byte, quint16 crc);

    QFile *m_file = nullptr;
    QTimer *m_timer = nullptr;
    State m_state = State::Idle;
    int m_retries = 0;

    // 发送
    QStringList m_files;
    int m_fileIndex = 0;
    qint64 m_fileSize = 0;
    qint64 m_offset = 0;
    quint32 m_bufferSize = 8192;
    QByteArray m_lastPacket;
    qint64 m_sentTotal = 0;

    // 接收
    QByteArray m_buffer;
    int m_dataPart = 0;          // 0=数据 1=刚收到 ZDLE 2=CRC
    QByteArray m_subData;
    quint8 m_marker = 0;
    int m_crcBytesLeft = 0;
    bool m_crc32 = false;   // 发送：对端支持 CRC32
    quint16 m_crc = 0;
    quint32 m_crc32Value = 0xFFFFFFFF;
    bool m_subCrc32 = false;
    QByteArray m_crcBytes;
    SubPurpose m_subPurpose = SubPurpose::FileData;
    bool m_currentFileOpen = false;
    qint64 m_rxOffset = 0;
    qint64 m_rxBytes = 0;
    int m_filesReceived = 0;
    quint32 m_rxBufferSize = 8192;
    QByteArray m_fileInfo;
};

#endif // ZMODEMTRANSFER_H
