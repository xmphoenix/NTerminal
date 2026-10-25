#include "transfer/kermittransfer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QVector>

namespace {
constexpr quint8 MARK = 0x01;
constexpr quint8 EOL = 0x0D;
constexpr quint8 PREFIX = 0x23;
constexpr int kInitTimeoutMs = 10000;
constexpr int kAckTimeoutMs = 10000;
constexpr int kMaxRetries = 10;
constexpr int kDefaultMaxPacket = 94;

QByteArray encodeQuoted(const QByteArray &data)
{
    QByteArray out;
    out.reserve(data.size() * 2);
    for (const char raw : data) {
        const quint8 byte = quint8(raw);
        if (byte == PREFIX) {
            // 前缀字符本身用重复两次表示（Kermit 规范）
            out.append(char(PREFIX));
            out.append(char(PREFIX));
        } else if (byte < 0x20 || byte == 0x7F) {
            out.append(char(PREFIX));
            out.append(char(byte ^ 0x40));
        } else {
            out.append(char(byte));
        }
    }
    return out;
}

quint16 kermitCrc(const QByteArray &data)
{
    quint16 crc = 0;
    for (const char raw : data) {
        crc ^= quint8(raw);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1) {
                crc = quint16((crc >> 1) ^ 0x8408);
            } else {
                crc = quint16(crc >> 1);
            }
        }
    }
    return crc;
}

quint8 foldedSum(const QByteArray &data)
{
    quint32 sum = 0;
    for (const char raw : data) {
        sum += quint8(raw);
    }
    sum = (sum + ((sum & 0xC0) >> 6)) & 0x3F;
    return quint8(sum + 32);
}

} // namespace

KermitTransfer::KermitTransfer(Direction direction, const QStringList &paths,
                               QObject *parent)
    : FileTransfer(Kind::Kermit, direction, paths, parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &KermitTransfer::onTimeout);
}

KermitTransfer::~KermitTransfer()
{
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}

void KermitTransfer::start()
{
    if (direction() == Direction::Send) {
        startSend();
    } else {
        startReceive();
    }
}

// ---------------------------------------------------------------------------
// 发送
// ---------------------------------------------------------------------------
void KermitTransfer::startSend()
{
    m_files = paths();
    if (m_files.isEmpty()) {
        fail(tr("没有要发送的文件"));
        return;
    }
    m_fileIndex = 0;
    m_file = new QFile(m_files.first());
    if (!m_file->open(QIODevice::ReadOnly)) {
        fail(tr("无法打开文件：%1").arg(m_file->errorString()));
        return;
    }
    m_fileSize = m_file->size();
    m_offset = 0;
    m_sentBytes = 0;
    m_sequence = 0;
    setProgress(0, m_fileSize);
    setStatus(tr("等待接收方（请在对端运行 kermit -r）"));
    log(tr("Kermit 发送 %1（%2 字节）").arg(m_files.first()).arg(m_fileSize));
    sendInit();
}

void KermitTransfer::sendInit()
{
    // S 数据不编码：MAXL TIME NPAD PADC EOL QCTL QBIN CHKT
    QByteArray params;
    params.append(char(94 + 32));
    params.append(char(10 + 32));
    params.append(char(0 + 32));
    params.append(char(0x40));       // PADC = NUL
    params.append(char(13 + 32));
    params.append(char(PREFIX));     // QCTL '#'
    params.append(char(' '));        // QBIN: 不做 8 位引号
    params.append(char('1'));        // CHKT: 1 字符校验和
    m_lastPacket = makePacket(0, 'S', params, 1, false, 94);
    m_sequence = 0;
    m_retries = 0;
    write(m_lastPacket);
    m_state = State::SendWaitInitAck;
    armTimer(kInitTimeoutMs);
}

void KermitTransfer::sendFileHeader()
{
    if (m_fileIndex >= m_files.size()) {
        return;
    }
    const QString name = QFileInfo(m_files.at(m_fileIndex)).fileName();
    ++m_sequence;   // 0 -> 1
    m_lastPacket = makePacket(m_sequence, 'F', name.toUtf8(), m_checkType, true,
                              m_maxPacket);
    m_offset = 0;
    m_file->seek(0);
    m_retries = 0;
    write(m_lastPacket);
    m_state = State::SendWaitFileAck;
    armTimer(kAckTimeoutMs);
    log(tr("发送文件头：%1").arg(name));
}

void KermitTransfer::sendNextData()
{
    if (!m_file || m_finished) {
        return;
    }
    if (m_offset >= m_fileSize) {
        sendEof();
        return;
    }
    // 数据块长度自适应：保证编码后的包不超过协商的最大包长
    int chunk = qMin<qint64>(80, m_fileSize - m_offset);
    QByteArray packet;
    QByteArray data;
    while (chunk > 0) {
        m_file->seek(m_offset);
        data = m_file->read(chunk);
        packet = makePacket(quint8((m_sequence + 1) & 0x3F), 'D', data, m_checkType,
                            true, m_maxPacket);
        if (!packet.isEmpty() && packet.size() <= m_maxPacket + 2) {
            break;
        }
        chunk -= 8;
    }
    if (chunk <= 0) {
        fail(tr("无法协商数据包长度"));
        return;
    }
    ++m_sequence;
    m_sequence &= 0x3F;
    m_lastPacket = packet;
    m_offset += data.size();
    setProgress(m_offset, m_fileSize);
    m_retries = 0;
    write(m_lastPacket);
    m_state = State::SendWaitDataAck;
    armTimer(kAckTimeoutMs);
}

void KermitTransfer::sendEof()
{
    ++m_sequence;
    m_sequence &= 0x3F;
    m_lastPacket = makePacket(m_sequence, 'Z', QByteArray(), m_checkType, true,
                              m_maxPacket);
    m_retries = 0;
    write(m_lastPacket);
    m_state = State::SendWaitEofAck;
    armTimer(kAckTimeoutMs);
    m_sentBytes += m_fileSize;
    setStatus(tr("发送文件结束…"));
}

void KermitTransfer::sendEot()
{
    ++m_sequence;
    m_sequence &= 0x3F;
    m_lastPacket = makePacket(m_sequence, 'B', QByteArray(), m_checkType, true,
                              m_maxPacket);
    m_retries = 0;
    write(m_lastPacket);
    m_state = State::SendWaitEotAck;
    armTimer(1000);
    setStatus(tr("结束会话…"));
}

bool KermitTransfer::handleSendPacket(const Packet &packet)
{
    if (packet.type == 'Y' || packet.type == 'N') {
        if (m_state == State::SendWaitInitAck && packet.type == 'Y') {
            if (packet.data.size() >= 8) {
                m_maxPacket = paramMaxLength(packet.data);
                m_checkType = paramCheckType(packet.data);
            }
            log(tr("协商参数：最大包 %1，校验类型 %2")
                    .arg(m_maxPacket).arg(m_checkType));
            sendFileHeader();
            return true;
        }
        if (m_state == State::SendWaitInitAck && packet.type == 'N') {
            // 对端没回参数 ACK，直接用默认值发文件头
            m_maxPacket = kDefaultMaxPacket;
            m_checkType = 1;
            sendFileHeader();
            return true;
        }
        if (m_state == State::SendWaitFileAck) {
            // 只接受对文件头（F）的确认：接收方在协商阶段的 ACK 序号为 0，
            // 不能把它当成文件头已确认而提前发送数据
            if (packet.type == 'Y' && packet.sequence == m_sequence) {
                sendNextData();
                return true;
            }
            if (packet.type == 'N' && packet.sequence == m_sequence) {
                write(m_lastPacket);
                armTimer(kAckTimeoutMs);
                return true;
            }
            return false;
        }
        if (m_state == State::SendWaitDataAck) {
            if (packet.type == 'Y' && packet.sequence == m_sequence) {
                sendNextData();
                return true;
            }
            if (packet.type == 'N') {
                if (packet.sequence == m_sequence) {
                    write(m_lastPacket);
                    armTimer(kAckTimeoutMs);
                    return true;
                }
                if (packet.sequence == quint8((m_sequence + 1) & 0x3F)) {
                    sendNextData();
                    return true;
                }
            }
        }
        if (m_state == State::SendWaitEofAck) {
            if (packet.type == 'Y' && packet.sequence == m_sequence) {
                if (m_fileIndex + 1 < m_files.size()) {
                    ++m_fileIndex;
                    if (m_file) {
                        m_file->close();
                        delete m_file;
                        m_file = nullptr;
                    }
                    m_file = new QFile(m_files.at(m_fileIndex));
                    if (!m_file->open(QIODevice::ReadOnly)) {
                        fail(tr("无法打开文件：%1").arg(m_file->errorString()));
                        return true;
                    }
                    m_fileSize = m_file->size();
                    setProgress(0, m_fileSize);
                    setStatus(tr("发送 %1…").arg(QFileInfo(m_files.at(m_fileIndex)).fileName()));
                    sendFileHeader();
                } else {
                    sendEot();
                }
                return true;
            }
            if (packet.type == 'N' && packet.sequence == m_sequence) {
                write(m_lastPacket);
                armTimer(kAckTimeoutMs);
                return true;
            }
        }
        if (m_state == State::SendWaitEotAck) {
            if (packet.type == 'Y') {
                finish(true, tr("Kermit 发送完成：%1 个文件，%2 字节")
                                 .arg(m_files.size()).arg(m_sentBytes));
                return true;
            }
        }
    } else if (packet.type == 'S') {
        // 接收方主动发来的 Init（可能先于我们的 ACK）
        if (packet.data.size() >= 8) {
            m_maxPacket = paramMaxLength(packet.data);
            m_checkType = paramCheckType(packet.data);
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 接收
// ---------------------------------------------------------------------------
void KermitTransfer::startReceive()
{
    if (!QFileInfo(receiveDirectory()).isDir()) {
        fail(tr("保存目录不存在：%1").arg(receiveDirectory()));
        return;
    }
    m_filesReceived = 0;
    m_receivedBytes = 0;
    m_checkType = 1;
    m_state = State::RecvWaitFile;
    setStatus(tr("等待发送方（请在对端运行 kermit -s 文件）"));
    log(tr("Kermit 接收保存到 %1").arg(receiveDirectory()));
    sendInitPacket();
    // 对端的 S 包可能在传输接管前就已经发出（被终端显示掉了），
    // 这里主动补一个带参数的 ACK，促使发送方进入文件传输阶段。
    sendAckWithParams(0);
    armTimer(5000);
}

void KermitTransfer::sendInitPacket()
{
    QByteArray params;
    params.append(char(94 + 32));
    params.append(char(10 + 32));
    params.append(char(0 + 32));
    params.append(char(0x40));
    params.append(char(13 + 32));
    params.append(char(PREFIX));
    params.append(char(' '));
    params.append(char('1'));
    m_lastPacket = makePacket(0, 'S', params, 1, false, 94);
    write(m_lastPacket);
}

void KermitTransfer::sendAck(quint8 sequence)
{
    write(makePacket(sequence, 'Y', QByteArray(), 1, false, 94));
}

void KermitTransfer::sendAckWithParams(quint8 sequence)
{
    QByteArray params;
    params.append(char(94 + 32));
    params.append(char(10 + 32));
    params.append(char(0 + 32));
    params.append(char(0x40));
    params.append(char(13 + 32));
    params.append(char(PREFIX));
    params.append(char(' '));
    params.append(char('1'));
    write(makePacket(sequence, 'Y', params, 1, false, 94));
}

void KermitTransfer::sendNak(quint8 sequence)
{
    write(makePacket(sequence, 'N', QByteArray(), 1, false, 94));
}

bool KermitTransfer::handleReceivePacket(const Packet &packet)
{
    if (packet.type == 'S') {
        if (packet.data.size() >= 8) {
            m_maxPacket = paramMaxLength(packet.data);
            m_checkType = paramCheckType(packet.data);
            log(tr("协商参数：最大包 %1，校验类型 %2")
                    .arg(m_maxPacket).arg(m_checkType));
        }
        sendAckWithParams(packet.sequence);
        m_state = State::RecvWaitFile;
        armTimer(kInitTimeoutMs);
        return true;
    }
    if (packet.type == 'F') {
        if (m_state == State::RecvReceiving && m_file) {
            m_file->close();
        }
        QString name = QString::fromUtf8(packet.data);
        name = sanitizeName(name);
        if (name.isEmpty()) {
            name = QStringLiteral("kermit-received.bin");
        }
        const QString path = QDir(receiveDirectory()).filePath(name);
        if (m_file) {
            m_file->close();
            delete m_file;
        }
        m_file = new QFile(path);
        if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            sendNak(packet.sequence);
            fail(tr("无法创建文件：%1").arg(m_file->errorString()));
            return true;
        }
        ++m_filesReceived;
        m_receivedBytes = 0;
        setProgress(0, -1);
        setStatus(tr("正在接收 %1…").arg(name));
        log(tr("接收文件：%1").arg(path));
        sendAck(packet.sequence);
        m_state = State::RecvReceiving;
        m_haveLastReceive = true;
        m_lastReceiveSequence = packet.sequence;
        armTimer(kAckTimeoutMs);
        return true;
    }
    if (packet.type == 'D') {
        if (m_state != State::RecvReceiving || !m_file) {
            sendNak(packet.sequence);
            return true;
        }
        if (m_haveLastReceive && packet.sequence == m_lastReceiveSequence) {
            sendAck(packet.sequence);   // 重复包
            return true;
        }
        m_file->write(packet.data);
        m_receivedBytes += packet.data.size();
        setProgress(m_receivedBytes, -1);
        m_lastReceiveSequence = packet.sequence;
        m_haveLastReceive = true;
        sendAck(packet.sequence);
        armTimer(kAckTimeoutMs);
        return true;
    }
    if (packet.type == 'Z') {
        if (m_file) {
            m_file->flush();
            m_file->close();
        }
        sendAck(packet.sequence);
        m_state = State::RecvWaitFile;
        armTimer(kInitTimeoutMs);
        return true;
    }
    if (packet.type == 'B') {
        if (m_file) {
            m_file->flush();
            m_file->close();
        }
        sendAck(packet.sequence);
        finish(true, tr("Kermit 接收完成：%1 个文件，%2 字节")
                         .arg(m_filesReceived).arg(m_receivedBytes));
        return true;
    }
    if (packet.type == 'Y' || packet.type == 'N') {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 编解码
// ---------------------------------------------------------------------------
QByteArray KermitTransfer::makePacket(quint8 sequence, char type,
                                      const QByteArray &data, int checkType,
                                      bool quoteData, int maxLength)
{
    const QByteArray encodedData = quoteData ? encodeQuoted(data) : data;
    const int checkLength = checkType == 3 ? 2 : 1;
    const int bodyLength = 2 + encodedData.size() + checkLength;
    if (bodyLength > maxLength) {
        return {};
    }

    QByteArray head;
    head.append(char(bodyLength + 32));
    head.append(char((sequence + 32) & 0xFF));
    head.append(type);
    head.append(encodedData);

    QByteArray check;
    if (checkType == 3) {
        const quint16 crc = kermitCrc(head);
        check.append(char(crc & 0xFF));
        check.append(char(crc >> 8));
    } else {
        check.append(char(foldedSum(head)));
    }

    QByteArray packet;
    packet.reserve(head.size() + check.size() + 2);
    packet.append(char(MARK));
    packet.append(head);
    packet.append(check);   // 校验字段按原始字节发送，不参与引号编码
    packet.append(char(EOL));
    return packet;
}

bool KermitTransfer::extractPacket(Packet *packet, int *consumed)
{
    int mark = m_buffer.indexOf(char(MARK));
    if (mark < 0) {
        // 没有包起始符：整段都是提示文字/填充，直接丢弃，避免原地死循环
        const int noise = m_buffer.size();
        m_buffer.clear();
        *consumed = noise;
        return false;
    }
    if (mark > 0) {
        m_buffer.remove(0, mark);
    }
    if (m_buffer.size() < 2) {
        *consumed = 0;
        return false;
    }
    const int bodyLength = quint8(m_buffer.at(1)) - 32;
    if (bodyLength < 3 || bodyLength > 200) {
        m_buffer.remove(0, 1);
        *consumed = 1;
        return false;
    }
    if (m_buffer.size() < 2 + bodyLength + 1) {
        *consumed = 0;
        return false;
    }
    const QByteArray body = m_buffer.mid(2, bodyLength);
    const quint8 eol = quint8(m_buffer.at(2 + bodyLength));
    if (eol != EOL && eol != 0x0A) {
        m_buffer.remove(0, 1);
        *consumed = 1;
        return false;
    }

    for (int checkLength = 1; checkLength <= 2; ++checkLength) {
        if (body.size() < 2 + checkLength) {
            continue;
        }
        const QByteArray seqType = body.left(2);
        const QByteArray encodedData = body.mid(2, body.size() - 2 - checkLength);
        const QByteArray check = body.right(checkLength);

        QByteArray rawHead;
        rawHead.append(char(bodyLength + 32));
        rawHead.append(body.left(body.size() - checkLength));

        bool ok = false;
        if (checkLength == 1) {
            ok = quint8(check.at(0)) == foldedSum(rawHead);
        } else {
            const quint16 crc = kermitCrc(rawHead);
            const quint16 received = quint16(quint8(check.at(0))
                                             | (quint16(quint8(check.at(1))) << 8));
            const quint16 swapped = quint16(quint8(check.at(1))
                                            | (quint16(quint8(check.at(0))) << 8));
            ok = (crc == received) || (crc == swapped);
        }
        if (!ok) {
            continue;
        }

        Packet result;
        result.valid = true;
        result.sequence = quint8((quint8(seqType.at(0)) - 32) & 0x3F);
        result.type = char(seqType.at(1));
        if (result.type == 'S' || result.type == 'Y') {
            // 初始化/确认包的数据字段不编码
            result.data = encodedData;
        } else {
            for (int i = 0; i < encodedData.size();) {
                const quint8 byte = quint8(encodedData.at(i));
                if (byte == PREFIX && i + 1 < encodedData.size()) {
                    const quint8 next = quint8(encodedData.at(i + 1));
                    quint8 value = next;
                    if (next == PREFIX) {
                        value = PREFIX;              // "##" -> '#'
                    } else if (next == quint8(PREFIX | 0x80)) {
                        // 高位版本的前缀字符（如 0xA3）同样按原样表示
                        value = next;
                    } else {
                        value = quint8(next ^ 0x40);
                    }
                    result.data.append(char(value));
                    i += 2;
                } else {
                    result.data.append(char(byte));
                    ++i;
                }
            }
        }
        const int total = 2 + bodyLength + 1;
        m_buffer.remove(0, total);
        *consumed = total;
        *packet = result;
        return true;
    }

    const int total = 2 + bodyLength + 1;
    m_buffer.remove(0, total);
    *consumed = total;
    return false;
}

int KermitTransfer::paramMaxLength(const QByteArray &params)
{
    if (params.isEmpty()) {
        return kDefaultMaxPacket;
    }
    const int value = quint8(params.at(0)) - 32;
    return qBound(20, value, 200);
}

int KermitTransfer::paramCheckType(const QByteArray &params)
{
    if (params.size() < 8) {
        return 1;
    }
    if (params.at(7) == '3') {
        return 3;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// 收包入口
// ---------------------------------------------------------------------------
void KermitTransfer::onData(const QByteArray &data)
{
    if (m_finished) {
        return;
    }
    m_buffer.append(data);
    processIncoming();
}

void KermitTransfer::processIncoming()
{
    while (!m_finished) {
        Packet packet;
        int consumed = 0;
        if (!extractPacket(&packet, &consumed)) {
            if (consumed == 0) {
                return;   // 等更多数据
            }
            if (m_buffer.isEmpty()) {
                return;
            }
            continue;
        }
        if (!packet.valid) {
            continue;
        }
        if (direction() == Direction::Send) {
            handleSendPacket(packet);
        } else {
            handleReceivePacket(packet);
        }
    }
}

void KermitTransfer::armTimer(int milliseconds)
{
    m_timer->start(milliseconds);
}

void KermitTransfer::onTimeout()
{
    if (m_finished) {
        return;
    }
    if (++m_retries > kMaxRetries) {
        fail(tr("等待对端响应超时"));
        return;
    }
    if (direction() == Direction::Send) {
        if (m_state == State::SendWaitInitAck && !m_lastPacket.isEmpty()) {
            write(m_lastPacket);
            armTimer(kInitTimeoutMs);
            return;
        }
        if (!m_lastPacket.isEmpty()) {
            log(tr("超时，重发（第 %1 次）").arg(m_retries));
            write(m_lastPacket);
            armTimer(kAckTimeoutMs);
        }
        return;
    }

    if (m_state == State::RecvWaitFile) {
        sendInitPacket();
        armTimer(5000);
    } else if (m_state == State::RecvReceiving) {
        sendNak(quint8((m_lastReceiveSequence + 1) & 0x3F));
        armTimer(kAckTimeoutMs);
    }
}

void KermitTransfer::cancel()
{
    if (m_finished) {
        return;
    }
    m_timer->stop();
    // 发送 EOT 尽早结束对端会话
    write(makePacket(quint8((m_sequence + 1) & 0x3F), 'B', QByteArray(), 1, false, 94));
    if (m_file) {
        m_file->close();
    }
    finish(false, tr("传输已取消"));
}
