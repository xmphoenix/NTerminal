#ifndef ASCIITRANSFER_H
#define ASCIITRANSFER_H

#include "transfer/filetransfer.h"

class QFile;
class QTimer;

// ASCII 传输：不做块校验，按文本行发送/接收（适合抓取命令输出）
class AsciiTransfer : public FileTransfer
{
    Q_OBJECT

public:
    AsciiTransfer(Direction direction, const QStringList &paths,
                  QObject *parent = nullptr);
    ~AsciiTransfer() override;

    void start() override;
    void cancel() override;
    void onData(const QByteArray &data) override;

private:
    void startSend();
    void startReceive();
    void sendChunk();
    void finishReceive();

    QFile *m_file = nullptr;
    QTimer *m_timer = nullptr;
    QByteArray m_payload;
    qint64 m_offset = 0;
    qint64 m_received = 0;
};

#endif // ASCIITRANSFER_H
