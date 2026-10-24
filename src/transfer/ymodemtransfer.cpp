#include "transfer/ymodemtransfer.h"
#include "transfer/xmodemcodec.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace {
constexpr char SOH = char(0x01);
constexpr char STX = char(0x02);
constexpr char EOT = char(0x04);
constexpr char ACK = char(0x06);
constexpr char NAK = char(0x15);
constexpr char CAN = char(0x18);
constexpr char SUB = char(0x1A);
constexpr char CRC_REQUEST = 'C';
constexpr int kBlock0Size = 128;
constexpr int kDataBlockSize = 1024;
constexpr int kRetryMs = 10000;
constexpr int kHandshakeRetryMs = 3000;
constexpr int kMaxRetries = 10;
} // namespace

YmodemTransfer::YmodemTransfer(Direction direction, const QStringList &paths,
                               QObject *parent)
    : FileTransfer(Kind::Ymodem, direction, paths, parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &YmodemTransfer::onTimeout);
}

YmodemTransfer::~YmodemTransfer()
{
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}

void YmodemTransfer::start()
{
    if (direction() == Direction::Send) {
        startSend();
    } else {
        startReceive();
    }
}

void YmodemTransfer::startSend()
{
    m_files = paths();
    if (m_files.isEmpty()) {
        fail(tr("没有要发送的文件"));
        return;
    }
    m_fileIndex = 0;
    if (!QFileInfo(m_files.first()).isFile()) {
        fail(tr("文件不存在：%1").arg(m_files.first()));
        return;
    }
    openCurrentFile();
    m_state = State::WaitStart;
    m_sentFilesBytes = 0;
    setStatus(tr("等待接收方（请在对端运行 rx -Y）"));
    log(tr("YMODEM 发送 %1 个文件").arg(m_files.size()));
    armTimer(60000);
}

void YmodemTransfer::startReceive()
{
    const QFileInfo dirInfo(receiveDirectory());
    if (!dirInfo.isDir()) {
        fail(tr("保存目录不存在：%1").arg(receiveDirectory()));
        return;
    }
    m_state = State::WaitBlock0;
    m_expectedSequence = 1;
    m_rxBuffer.clear();
    m_filesReceived = 0;
    m_totalReceived = 0;
    m_receiveHandshakeTries = 0;
    setStatus(tr("等待发送方（请在对端运行 sx -Y 文件）"));
    log(tr("YMODEM 接收保存到 %1").arg(receiveDirectory()));
    sendCrcRequest();
    armTimer(kHandshakeRetryMs);
}

void YmodemTransfer::onData(const QByteArray &data)
{
    if (m_finished) {
        return;
    }
    if (direction() == Direction::Send) {
        handleSendBytes(data);
    } else {
        m_rxBuffer.append(data);
        handleReceiveBytes();
    }
}

void YmodemTransfer::openCurrentFile()
{
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    m_file = new QFile(m_files.at(m_fileIndex));
    if (!m_file->open(QIODevice::ReadOnly)) {
        fail(tr("无法打开文件：%1").arg(m_file->errorString()));
        return;
    }
    m_fileSize = m_file->size();
    m_blockOffset = 0;
    m_sequence = 0;
    setProgress(0, m_fileSize);
    setStatus(tr("准备发送 %1（%2 字节）")
                  .arg(QFileInfo(m_files.at(m_fileIndex)).fileName())
                  .arg(m_fileSize));
}

void YmodemTransfer::sendBlock0()
{
    const QFileInfo info(m_files.at(m_fileIndex));
    QByteArray payload = info.fileName().toUtf8();
    payload.append('\0');
    payload.append(QByteArray::number(m_fileSize));
    payload.append(' ');
    payload.append(QByteArray::number(info.lastModified().toSecsSinceEpoch(), 8));
    payload.append(" 100644 0");
    payload.append('\0');
    if (payload.size() > kBlock0Size) {
        payload.truncate(kBlock0Size);
    }
    payload.append(QByteArray(kBlock0Size - payload.size(), '\0'));

    m_lastPacket = XmodemCodec::makeBlock(SOH, 0, payload, true);
    m_retries = 0;
    write(m_lastPacket);
    m_state = State::WaitBlock0Ack;
    armTimer(kRetryMs);
    log(tr("发送文件头：%1（%2 字节）").arg(info.fileName()).arg(m_fileSize));
}

void YmodemTransfer::sendEndBlock0()
{
    QByteArray payload(kBlock0Size, '\0');
    m_lastPacket = XmodemCodec::makeBlock(SOH, 0, payload, true);
    m_retries = 0;
    write(m_lastPacket);
    m_state = State::WaitEndAck;
    setStatus(tr("结束批量传输…"));
    // lrzsz 收到批量结束块后直接退出、不再回 ACK；数据块都已确认，这里直接完成
    finish(true, tr("YMODEM 发送完成：%1 个文件，%2 字节")
                     .arg(m_files.size()).arg(m_sentFilesBytes));
}

void YmodemTransfer::sendNextDataBlock()
{
    if (!m_file || m_finished) {
        return;
    }
    if (m_blockOffset >= m_fileSize) {
        sendEot();
        return;
    }
    m_file->seek(m_blockOffset);
    QByteArray payload = m_file->read(kDataBlockSize);
    m_lastPayloadSize = payload.size();
    if (payload.size() < kDataBlockSize) {
        payload.append(QByteArray(kDataBlockSize - payload.size(), SUB));
    }
    m_lastPacket = XmodemCodec::makeBlock(STX, m_sequence, payload, true);
    write(m_lastPacket);
    m_state = State::WaitAck;
    armTimer(kRetryMs);
}

void YmodemTransfer::resendLast()
{
    if (m_lastPacket.isEmpty()) {
        return;
    }
    if (++m_retries > kMaxRetries) {
        fail(tr("重传次数过多，传输中止"));
        return;
    }
    log(tr("重发数据块（第 %1 次）").arg(m_retries));
    write(m_lastPacket);
    armTimer(kRetryMs);
}

void YmodemTransfer::sendEot()
{
    write(QByteArray(1, EOT));
    m_state = State::WaitEotAck;
    m_retries = 0;
    armTimer(kRetryMs);
}

void YmodemTransfer::handleSendBytes(const QByteArray &data)
{
    for (const char raw : data) {
        const quint8 byte = quint8(raw);
        if (byte == quint8(CAN)) {
            if (++m_canCount >= 2) {
                abortTransfer(tr("接收方取消了传输"));
                return;
            }
            continue;
        }
        m_canCount = 0;

        switch (m_state) {
        case State::WaitStart:
            if (byte == quint8(CRC_REQUEST)) {
                sendBlock0();
            }
            break;
        case State::WaitBlock0Ack:
            if (byte == quint8(ACK)) {
                m_state = State::WaitDataStart;
                m_handshakeTries = 0;
                armTimer(30000);
            } else if (byte == quint8(NAK)) {
                resendLast();
            }
            break;
        case State::WaitDataStart:
            if (byte == quint8(CRC_REQUEST)) {
                m_blockOffset = 0;
                m_sequence = 1;
                log(tr("开始发送数据"));
                sendNextDataBlock();
            }
            break;
        case State::WaitAck:
            if (byte == quint8(ACK)) {
                m_retries = 0;
                m_blockOffset += m_lastPayloadSize;
                ++m_sequence;
                setProgress(m_blockOffset, m_fileSize);
                if (m_blockOffset >= m_fileSize) {
                    sendEot();
                } else {
                    sendNextDataBlock();
                }
            } else if (byte == quint8(NAK)) {
                resendLast();
            }
            break;
        case State::WaitEotAck:
            if (byte == quint8(ACK)) {
                m_sentFilesBytes += m_fileSize;
                m_state = State::WaitBatch;
                m_handshakeTries = 0;
                armTimer(30000);
            } else if (byte == quint8(NAK)) {
                write(QByteArray(1, EOT));
                armTimer(kRetryMs);
            }
            break;
        case State::WaitBatch:
            if (byte == quint8(CRC_REQUEST)) {
                if (m_fileIndex + 1 < m_files.size()) {
                    ++m_fileIndex;
                    openCurrentFile();
                    if (m_finished) {
                        return;
                    }
                    sendBlock0();
                } else {
                    sendEndBlock0();
                }
            }
            break;
        case State::WaitEndAck:
            if (byte == quint8(ACK)) {
                finish(true, tr("YMODEM 发送完成：%1 个文件，%2 字节")
                                 .arg(m_files.size()).arg(m_sentFilesBytes));
            } else if (byte == quint8(NAK)) {
                resendLast();
            }
            break;
        default:
            break;
        }
    }
}

void YmodemTransfer::handleReceiveBytes()
{
    while (!m_finished && !m_rxBuffer.isEmpty()) {
        const quint8 first = quint8(m_rxBuffer.at(0));
        if (first == quint8(EOT)) {
            m_rxBuffer.remove(0, 1);
            sendAck();
            if (m_file) {
                m_file->flush();
                m_file->close();
            }
            if (m_state == State::Receiving) {
                m_state = State::WaitNextBlock0;
                m_handshakeTries = 0;
                sendCrcRequest();
                armTimer(10000);
            }
            continue;
        }
        if (first == quint8(CAN)) {
            m_rxBuffer.remove(0, 1);
            if (++m_canCount >= 2) {
                abortTransfer(tr("发送方取消了传输"));
                return;
            }
            continue;
        }
        m_canCount = 0;

        int blockSize = 0;
        if (first == quint8(SOH)) {
            blockSize = kBlock0Size;
        } else if (first == quint8(STX)) {
            blockSize = kDataBlockSize;
        } else {
            m_rxBuffer.remove(0, 1);
            continue;
        }
        const int packetSize = 1 + 2 + blockSize + 2;
        if (m_rxBuffer.size() < packetSize) {
            return;
        }
        const QByteArray packet = m_rxBuffer.left(packetSize);
        const quint8 sequence = quint8(packet.at(1));
        const quint8 complement = quint8(packet.at(2));
        const QByteArray payload = packet.mid(3, blockSize);
        const quint16 receivedCrc = quint16((quint8(packet.at(packetSize - 2)) << 8)
                                            | quint8(packet.at(packetSize - 1)));
        if (quint8(~sequence) != complement
            || receivedCrc != XmodemCodec::crc16(payload)) {
            log(tr("块 %1 校验失败，请求重发").arg(sequence));
            m_rxBuffer.remove(0, 1);
            sendNak();
            continue;
        }

        if (m_state == State::WaitBlock0 || m_state == State::WaitNextBlock0) {
            if (sequence != 0) {
                m_rxBuffer.remove(0, packetSize);
                sendNak();
                continue;
            }
            const bool opened = openReceiveFile(payload);
            m_rxBuffer.remove(0, packetSize);
            sendAck();
            if (!opened) {
                finishReceive();
                return;
            }
            m_expectedSequence = 1;
            m_state = State::Receiving;
            sendCrcRequest();
            armTimer(kRetryMs);
            continue;
        }

        if (m_state == State::Receiving) {
            m_rxBuffer.remove(0, packetSize);
            if (sequence == m_expectedSequence) {
                QByteArray data = payload;
                if (m_fileRemaining >= 0) {
                    const qint64 keep = qMin<qint64>(m_fileRemaining, data.size());
                    data = data.left(int(keep));
                }
                if (m_file) {
                    m_file->write(data);
                }
                m_fileReceived += data.size();
                m_totalReceived += data.size();
                if (m_fileRemaining > 0) {
                    m_fileRemaining -= data.size();
                }
                ++m_expectedSequence;
                setProgress(m_fileReceived, m_fileSize > 0 ? m_fileSize : -1);
                sendAck();
                armTimer(kRetryMs);
            } else if (sequence == quint8(m_expectedSequence - 1)) {
                sendAck();
            } else {
                sendNak();
            }
        }
    }
}

bool YmodemTransfer::openReceiveFile(const QByteArray &block0)
{
    const int nul = block0.indexOf('\0');
    const QByteArray nameBytes = nul >= 0 ? block0.left(nul) : block0;
    QString name = sanitizeName(QString::fromUtf8(nameBytes));
    if (name.isEmpty()) {
        return false;   // 批量结束块
    }

    qint64 size = -1;
    if (nul >= 0 && nul + 1 < block0.size()) {
        QByteArray rest = block0.mid(nul + 1);
        rest = rest.trimmed();
        QByteArray digits;
        for (const char c : rest) {
            if (c >= '0' && c <= '9') {
                digits.append(c);
            } else {
                break;
            }
        }
        if (!digits.isEmpty()) {
            bool ok = false;
            size = digits.toLongLong(&ok);
            if (!ok) {
                size = -1;
            }
        }
    }

    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    const QString path = QDir(receiveDirectory()).filePath(name);
    m_file = new QFile(path);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("无法创建文件：%1").arg(m_file->errorString()));
        return false;
    }
    m_fileSize = size;
    m_fileRemaining = size;
    m_fileReceived = 0;
    ++m_filesReceived;
    setStatus(tr("正在接收 %1…").arg(name));
    log(tr("接收文件：%1（%2）")
            .arg(path, size >= 0 ? tr("%1 字节").arg(size) : tr("大小未知")));
    return true;
}

void YmodemTransfer::finishReceive()
{
    if (m_file) {
        m_file->flush();
        m_file->close();
    }
    finish(true, tr("YMODEM 接收完成：%1 个文件，%2 字节")
                     .arg(m_filesReceived).arg(m_totalReceived));
}

void YmodemTransfer::sendAck()
{
    write(QByteArray(1, ACK));
}

void YmodemTransfer::sendNak()
{
    write(QByteArray(1, NAK));
}

void YmodemTransfer::sendCrcRequest()
{
    write(QByteArray(1, CRC_REQUEST));
}

void YmodemTransfer::armTimer(int milliseconds)
{
    m_timer->start(milliseconds);
}

void YmodemTransfer::onTimeout()
{
    if (m_finished) {
        return;
    }
    switch (m_state) {
    case State::WaitStart:
        if (++m_handshakeTries >= 20) {
            fail(tr("接收方未响应（超时）"));
        } else {
            armTimer(kHandshakeRetryMs);
        }
        break;
    case State::WaitEndAck:
        // lrzsz 收完批量结束块即退出，不会回 ACK；短等待后按完成处理
        finish(true, tr("YMODEM 发送完成：%1 个文件，%2 字节")
                         .arg(m_files.size()).arg(m_sentFilesBytes));
        break;
    case State::WaitBlock0Ack:
    case State::WaitAck:
        if (!m_lastPacket.isEmpty()) {
            if (++m_retries > kMaxRetries) {
                fail(tr("等待接收方响应超时"));
            } else {
                resendLast();
            }
        }
        break;
    case State::WaitDataStart:
    case State::WaitBatch:
        if (++m_handshakeTries >= 10) {
            fail(tr("等待接收方请求超时"));
        } else {
            armTimer(30000);
        }
        break;
    case State::WaitEotAck:
        if (++m_retries > kMaxRetries) {
            fail(tr("等待接收方确认超时"));
        } else {
            write(QByteArray(1, EOT));
            armTimer(kRetryMs);
        }
        break;
    case State::WaitBlock0:
        if (++m_receiveHandshakeTries >= 30) {
            fail(tr("等待发送方超时"));
        } else {
            sendCrcRequest();
            armTimer(kHandshakeRetryMs);
        }
        break;
    case State::WaitNextBlock0:
        // EOT 之后没有批量结束块：按完成处理
        finishReceive();
        break;
    case State::Receiving:
        log(tr("接收超时，请求重发"));
        m_rxBuffer.clear();
        sendNak();
        armTimer(kRetryMs);
        break;
    default:
        break;
    }
}

void YmodemTransfer::abortTransfer(const QString &reason)
{
    write(QByteArray(8, CAN));
    if (m_file) {
        m_file->close();
    }
    fail(reason);
}

void YmodemTransfer::cancel()
{
    if (m_finished) {
        return;
    }
    m_timer->stop();
    write(QByteArray(8, CAN));
    if (m_file) {
        m_file->flush();
        m_file->close();
    }
    finish(false, tr("传输已取消"));
}
