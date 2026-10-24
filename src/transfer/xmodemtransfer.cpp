#include "transfer/xmodemtransfer.h"
#include "transfer/xmodemcodec.h"

#include <QFile>
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
constexpr int kBlockSize = 128;
constexpr int kBlockSize1K = 1024;
constexpr int kHandshakeRetryMs = 3000;
constexpr int kBlockRetryMs = 10000;
constexpr int kMaxRetries = 10;
} // namespace

XmodemTransfer::XmodemTransfer(Direction direction, const QStringList &paths,
                               QObject *parent)
    : FileTransfer(Kind::Xmodem, direction, paths, parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &XmodemTransfer::onTimeout);
}

XmodemTransfer::~XmodemTransfer()
{
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}

void XmodemTransfer::start()
{
    if (direction() == Direction::Send) {
        startSend();
    } else {
        startReceive();
    }
}

void XmodemTransfer::startSend()
{
    if (paths().isEmpty()) {
        fail(tr("没有要发送的文件"));
        return;
    }
    m_file = new QFile(paths().first());
    if (!m_file->open(QIODevice::ReadOnly)) {
        fail(tr("无法打开文件：%1").arg(m_file->errorString()));
        return;
    }
    m_total = m_file->size();
    m_blockOffset = 0;
    m_blockSequence = 1;
    m_state = State::WaitStart;
    setProgress(0, m_total);
    setStatus(tr("等待接收方（请在对端运行 rx -X）"));
    log(tr("XMODEM 发送 %1（%2 字节）").arg(paths().first()).arg(m_total));
    armTimer(60000);
}

void XmodemTransfer::startReceive()
{
    if (paths().isEmpty()) {
        fail(tr("没有指定保存文件"));
        return;
    }
    m_file = new QFile(paths().first());
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("无法创建文件：%1").arg(m_file->errorString()));
        return;
    }
    m_state = State::Receiving;
    // XMODEM 的数据块从 1 开始编号（与 YMODEM 的块 0 文件头不同）
    m_expectedSequence = 1;
    m_receivedBytes = 0;
    m_handshakeTries = 0;
    m_rxBuffer.clear();
    setStatus(tr("等待发送方（请在对端运行 sx -X 文件）"));
    log(tr("XMODEM 接收保存到 %1").arg(paths().first()));
    write(QByteArray(1, CRC_REQUEST));
    armTimer(kHandshakeRetryMs);
}

void XmodemTransfer::onData(const QByteArray &data)
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

void XmodemTransfer::handleSendBytes(const QByteArray &data)
{
    for (const char raw : data) {
        const quint8 byte = quint8(raw);
        if (m_state == State::WaitStart) {
            if (byte == quint8(CRC_REQUEST)) {
                m_useCrc = true;
                beginSending();
            } else if (byte == quint8(NAK)) {
                m_useCrc = false;
                beginSending();
            }
            continue;
        }

        if (byte == quint8(CAN)) {
            if (++m_canCount >= 2) {
                abortTransfer(tr("接收方取消了传输"));
            }
            continue;
        }
        m_canCount = 0;

        if (m_state == State::WaitAck) {
            if (byte == quint8(ACK)) {
                m_retries = 0;
                m_blockOffset += m_lastPayloadSize;
                ++m_blockSequence;
                setProgress(m_blockOffset, m_total);
                if (m_blockOffset >= m_total) {
                    sendEot();
                } else {
                    sendNextBlock();
                }
            } else if (byte == quint8(NAK)) {
                resendLast();
            }
        } else if (m_state == State::WaitEotAck) {
            if (byte == quint8(ACK)) {
                finish(true, tr("XMODEM 发送完成：%1 字节").arg(m_total));
            } else if (byte == quint8(NAK)) {
                write(QByteArray(1, EOT));
                armTimer(kBlockRetryMs);
            }
        }
    }
}

void XmodemTransfer::beginSending()
{
    setStatus(tr("正在发送…"));
    log(m_useCrc ? tr("接收方使用 CRC 校验") : tr("接收方使用累加和校验"));
    m_retries = 0;
    sendNextBlock();
}

void XmodemTransfer::sendNextBlock()
{
    if (!m_file || m_finished) {
        return;
    }
    if (m_blockOffset >= m_total) {
        sendEot();
        return;
    }
    m_file->seek(m_blockOffset);
    QByteArray payload = m_file->read(kBlockSize);
    m_lastPayloadSize = payload.size();
    m_lastBlock = (m_blockOffset + payload.size() >= m_total);
    if (payload.size() < kBlockSize) {
        payload.append(QByteArray(kBlockSize - payload.size(), SUB));
    }
    m_lastPacket = XmodemCodec::makeBlock(SOH, m_blockSequence, payload, m_useCrc);
    write(m_lastPacket);
    m_state = State::WaitAck;
    armTimer(kBlockRetryMs);
}

void XmodemTransfer::resendLast()
{
    if (m_lastPacket.isEmpty()) {
        return;
    }
    if (++m_retries > kMaxRetries) {
        fail(tr("重传次数过多，传输中止"));
        return;
    }
    log(tr("重发第 %1 块（第 %2 次）").arg(m_blockSequence).arg(m_retries));
    write(m_lastPacket);
    armTimer(kBlockRetryMs);
}

void XmodemTransfer::sendEot()
{
    setStatus(tr("发送结束信号…"));
    write(QByteArray(1, EOT));
    m_state = State::WaitEotAck;
    armTimer(kBlockRetryMs);
}

void XmodemTransfer::handleReceiveBytes()
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
            finish(true, tr("XMODEM 接收完成：%1 字节").arg(m_receivedBytes));
            return;
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
            blockSize = kBlockSize;
        } else if (first == quint8(STX)) {
            blockSize = kBlockSize1K;
        } else {
            m_rxBuffer.remove(0, 1);   // 噪声
            continue;
        }

        const int packetSize = 1 + 2 + blockSize + 2;
        if (m_rxBuffer.size() < packetSize) {
            return;   // 等更多数据
        }
        const QByteArray packet = m_rxBuffer.left(packetSize);
        const quint8 sequence = quint8(packet.at(1));
        const quint8 complement = quint8(packet.at(2));
        const QByteArray payload = packet.mid(3, blockSize);
        const quint16 receivedCrc = quint16((quint8(packet.at(packetSize - 2)) << 8)
                                            | quint8(packet.at(packetSize - 1)));
        const quint16 expectedCrc = XmodemCodec::crc16(payload);

        if (quint8(~sequence) != complement || receivedCrc != expectedCrc) {
            log(tr("块 %1 校验失败，请求重发").arg(sequence));
            m_rxBuffer.remove(0, 1);   // 重新同步
            sendNak();
            continue;
        }

        if (sequence == m_expectedSequence) {
            if (m_file) {
                m_file->write(payload);
            }
            m_receivedBytes += payload.size();
            ++m_expectedSequence;
            if (m_receivedBytes > 0 && !m_printedFirstBlock) {
                m_printedFirstBlock = true;
                setStatus(tr("正在接收…"));
            }
            setProgress(m_receivedBytes, -1);
            sendAck();
        } else if (sequence == quint8(m_expectedSequence - 1)) {
            sendAck();   // 重复块：确认即可
        } else {
            log(tr("收到意外块 %1（期望 %2），请求重发")
                    .arg(sequence).arg(m_expectedSequence));
            sendNak();
        }
        m_rxBuffer.remove(0, packetSize);
        armTimer(kBlockRetryMs);
    }
}

void XmodemTransfer::sendAck()
{
    write(QByteArray(1, ACK));
}

void XmodemTransfer::sendNak()
{
    write(QByteArray(1, NAK));
}

void XmodemTransfer::armTimer(int milliseconds)
{
    m_timer->start(milliseconds);
}

void XmodemTransfer::onTimeout()
{
    if (m_finished) {
        return;
    }
    if (direction() == Direction::Send) {
        if (m_state == State::WaitStart) {
            if (++m_handshakeTries >= 20) {
                fail(tr("接收方未响应（超时）"));
            } else {
                armTimer(kHandshakeRetryMs);
            }
            return;
        }
        if (++m_retries > kMaxRetries) {
            fail(tr("等待接收方响应超时"));
            return;
        }
        if (m_state == State::WaitEotAck) {
            write(QByteArray(1, EOT));
        } else if (!m_lastPacket.isEmpty()) {
            log(tr("超时，重发第 %1 块（第 %2 次）").arg(m_blockSequence).arg(m_retries));
            write(m_lastPacket);
        }
        armTimer(kBlockRetryMs);
        return;
    }

    // 接收：还没开始就继续发 'C'，块传输中途超时就 NAK
    if (m_receivedBytes == 0) {
        if (++m_handshakeTries >= 30) {
            fail(tr("等待发送方超时"));
            return;
        }
        write(QByteArray(1, CRC_REQUEST));
        armTimer(kHandshakeRetryMs);
    } else {
        log(tr("接收超时，请求重发"));
        m_rxBuffer.clear();
        sendNak();
        armTimer(kBlockRetryMs);
    }
}

void XmodemTransfer::abortTransfer(const QString &reason)
{
    write(QByteArray(8, CAN));
    if (m_file) {
        m_file->close();
    }
    fail(reason);
}

void XmodemTransfer::cancel()
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
