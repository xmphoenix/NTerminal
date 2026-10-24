#include "transfer/zmodemtransfer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QVector>

namespace {
constexpr quint8 ZPAD = 0x2A;
constexpr quint8 ZDLE = 0x18;
constexpr quint8 ZBIN = 'A';
constexpr quint8 ZHEX = 'B';
constexpr quint8 ZBIN32 = 'C';

constexpr quint8 ZRQINIT = 0;
constexpr quint8 ZRINIT = 1;
constexpr quint8 ZSINIT = 2;
constexpr quint8 ZACK = 3;
constexpr quint8 ZFILE = 4;
constexpr quint8 ZSKIP = 5;
constexpr quint8 ZNAK = 6;
constexpr quint8 ZABORT = 7;
constexpr quint8 ZFIN = 8;
constexpr quint8 ZRPOS = 9;
constexpr quint8 ZDATA = 10;
constexpr quint8 ZEOF = 11;

constexpr quint8 ZCRCE = 0x68;
constexpr quint8 ZCRCG = 0x69;
constexpr quint8 ZCRCQ = 0x6A;
constexpr quint8 ZCRCW = 0x6B;

constexpr quint32 kZrinFlags = 0x03 | 0x20;   // CANFDX | CANOVIO | CANFC32
constexpr quint32 kCrc32Residue = 0xDEBB20E3;
constexpr quint32 kBufSize = 8192;
constexpr int kAckTimeoutMs = 10000;
constexpr int kMaxRetries = 10;

quint16 crcTableEntry(int index)
{
    quint16 crc = quint16(index << 8);
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
    }
    return crc;
}

quint16 updcrc(quint8 byte, quint16 crc)
{
    static const QVector<quint16> table = [] {
        QVector<quint16> values(256);
        for (int i = 0; i < 256; ++i) {
            values[i] = crcTableEntry(i);
        }
        return values;
    }();
    return quint16(table[(crc >> 8) & 0xFF] ^ ((crc << 8) & 0xFFFF) ^ byte);
}

quint32 crc32TableEntry(int index)
{
    quint32 crc = quint32(index);
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return crc;
}

quint32 updcrc32(quint8 byte, quint32 crc)
{
    static const QVector<quint32> table = [] {
        QVector<quint32> values(256);
        for (int i = 0; i < 256; ++i) {
            values[i] = crc32TableEntry(i);
        }
        return values;
    }();
    return table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
}

quint32 decodeLe32(const QByteArray &data)
{
    return quint32(quint8(data.at(0)))
           | (quint32(quint8(data.at(1))) << 8)
           | (quint32(quint8(data.at(2))) << 16)
           | (quint32(quint8(data.at(3))) << 24);
}

QByteArray encodeLe32(quint32 value)
{
    QByteArray out(4, '\0');
    out[0] = char(value & 0xFF);
    out[1] = char((value >> 8) & 0xFF);
    out[2] = char((value >> 16) & 0xFF);
    out[3] = char((value >> 24) & 0xFF);
    return out;
}

} // namespace

ZmodemTransfer::ZmodemTransfer(Direction direction, const QStringList &paths,
                               QObject *parent)
    : FileTransfer(Kind::Zmodem, direction, paths, parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &ZmodemTransfer::onTimeout);
}

ZmodemTransfer::~ZmodemTransfer()
{
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}

void ZmodemTransfer::start()
{
    if (direction() == Direction::Send) {
        startSend();
    } else {
        startReceive();
    }
}

// ---------------------------------------------------------------------------
// 编解码
// ---------------------------------------------------------------------------
quint16 ZmodemTransfer::updateCrc16(quint8 byte, quint16 crc)
{
    return updcrc(byte, crc);
}

quint32 ZmodemTransfer::updateCrc32(quint8 byte, quint32 crc)
{
    return updcrc32(byte, crc);
}

void ZmodemTransfer::appendEscaped(QByteArray &out, quint8 byte)
{
    const bool escape = byte < 0x20
                        || byte == 0x7F || byte == 0xFF
                        || byte == 0x8D || byte == 0x90
                        || byte == 0x91 || byte == 0x93;
    if (!escape) {
        out.append(char(byte));
        return;
    }
    out.append(char(ZDLE));
    if (byte == 0x7F) {
        out.append('l');             // ZRUB0 -> 0x7F
    } else if (byte == 0xFF) {
        out.append('m');             // ZRUB1 -> 0xFF
    } else {
        out.append(char(byte ^ 0x40));
    }
}

QByteArray ZmodemTransfer::makeHeaderBytes(quint8 type, quint32 data, bool hex, bool crc32)
{
    QByteArray dataBytes = encodeLe32(data);
    quint16 crc = 0;
    quint32 crc32Value = 0xFFFFFFFF;
    if (crc32) {
        crc32Value = updcrc32(type, crc32Value);
        for (const char byte : dataBytes) {
            crc32Value = updcrc32(quint8(byte), crc32Value);
        }
        crc32Value ^= 0xFFFFFFFFu;
    } else {
        crc = updcrc(type, crc);
        for (const char byte : dataBytes) {
            crc = updcrc(quint8(byte), crc);
        }
        crc = updcrc(0, crc);
        crc = updcrc(0, crc);
    }

    QByteArray out;
    if (hex) {
        out.append(char(ZPAD));
        out.append(char(ZPAD));
        out.append(char(ZDLE));
        out.append(char(ZHEX));
        static const char digits[] = "0123456789abcdef";
        auto appendHex = [&out](quint8 value) {
            out.append(digits[(value >> 4) & 0xF]);
            out.append(digits[value & 0xF]);
        };
        appendHex(type);
        for (const char byte : dataBytes) {
            appendHex(quint8(byte));
        }
        appendHex(quint8(crc >> 8));
        appendHex(quint8(crc & 0xFF));
        out.append('\r');
        out.append('\n');
        if (type != ZACK && type != ZFIN) {
            out.append(char(0x11));   // XON
        }
    } else {
        out.append(char(ZPAD));
        out.append(char(ZDLE));
        out.append(char(crc32 ? ZBIN32 : ZBIN));
        appendEscaped(out, type);
        for (const char byte : dataBytes) {
            appendEscaped(out, quint8(byte));
        }
        if (crc32) {
            appendEscaped(out, quint8(crc32Value & 0xFF));
            appendEscaped(out, quint8((crc32Value >> 8) & 0xFF));
            appendEscaped(out, quint8((crc32Value >> 16) & 0xFF));
            appendEscaped(out, quint8((crc32Value >> 24) & 0xFF));
        } else {
            appendEscaped(out, quint8(crc >> 8));
            appendEscaped(out, quint8(crc & 0xFF));
        }
    }
    return out;
}

QByteArray ZmodemTransfer::makeSubpacket(const QByteArray &data, quint8 marker, bool crc32)
{
    quint16 crc = 0;
    quint32 crc32Value = 0xFFFFFFFF;
    if (crc32) {
        for (const char byte : data) {
            crc32Value = updcrc32(quint8(byte), crc32Value);
        }
        crc32Value = updcrc32(marker, crc32Value);
        crc32Value ^= 0xFFFFFFFFu;
    } else {
        for (const char byte : data) {
            crc = updcrc(quint8(byte), crc);
        }
        crc = updcrc(marker, crc);
        crc = updcrc(0, crc);
        crc = updcrc(0, crc);
    }

    QByteArray out;
    out.reserve(data.size() + 8);
    for (const char byte : data) {
        appendEscaped(out, quint8(byte));
    }
    out.append(char(ZDLE));
    out.append(char(marker));
    if (crc32) {
        appendEscaped(out, quint8(crc32Value & 0xFF));
        appendEscaped(out, quint8((crc32Value >> 8) & 0xFF));
        appendEscaped(out, quint8((crc32Value >> 16) & 0xFF));
        appendEscaped(out, quint8((crc32Value >> 24) & 0xFF));
    } else {
        appendEscaped(out, quint8(crc >> 8));
        appendEscaped(out, quint8(crc & 0xFF));
    }
    return out;
}

// ---------------------------------------------------------------------------
// 发送
// ---------------------------------------------------------------------------
void ZmodemTransfer::startSend()
{
    m_files = paths();
    if (m_files.isEmpty()) {
        fail(tr("没有要发送的文件"));
        return;
    }
    m_fileIndex = 0;
    if (!openNextFile()) {
        return;
    }
    m_state = State::SendWaitRinit;
    m_sentTotal = 0;
    setProgress(0, m_fileSize);
    setStatus(tr("等待接收方（请在对端运行 rz）"));
    log(tr("ZMODEM 发送 %1（%2 字节）").arg(m_files.first()).arg(m_fileSize));
    sendHeader(ZRQINIT, 0, true);
    armTimer(kAckTimeoutMs);
}

bool ZmodemTransfer::openNextFile()
{
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    if (m_fileIndex >= m_files.size()) {
        return false;
    }
    m_file = new QFile(m_files.at(m_fileIndex));
    if (!m_file->open(QIODevice::ReadOnly)) {
        fail(tr("无法打开文件：%1").arg(m_file->errorString()));
        return false;
    }
    m_fileSize = m_file->size();
    m_offset = 0;
    setProgress(0, m_fileSize);
    setStatus(tr("发送 %1…").arg(QFileInfo(m_files.at(m_fileIndex)).fileName()));
    return true;
}

void ZmodemTransfer::sendHeader(quint8 type, quint32 data, bool hex)
{
    m_lastPacket = makeHeaderBytes(type, data, hex, !hex && m_crc32);
    write(m_lastPacket);
}

void ZmodemTransfer::sendZrinit()
{
    // ZRINIT 的 4 字节：低 3 字节是接收缓冲大小，最高字节是能力标志
    sendHeader(ZRINIT, (kZrinFlags << 24) | (kBufSize & 0xFFFFFF), true);
}

void ZmodemTransfer::sendZfile()
{
    if (!m_file) {
        return;
    }
    const QFileInfo info(m_files.at(m_fileIndex));
    QByteArray payload = info.fileName().toUtf8();
    payload.append('\0');
    payload.append(QByteArray::number(m_fileSize));
    payload.append(' ');
    payload.append(QByteArray::number(info.lastModified().toSecsSinceEpoch(), 8));
    payload.append(" 100644 0");
    payload.append('\0');

    QByteArray packet = makeHeaderBytes(ZFILE, 0, false, m_crc32);
    packet.append(makeSubpacket(payload, ZCRCW, m_crc32));
    m_lastPacket = packet;
    write(m_lastPacket);
    m_state = State::SendWaitFileAck;
    armTimer(kAckTimeoutMs);
    log(tr("发送文件头：%1").arg(info.fileName()));
}

void ZmodemTransfer::sendDataFrame()
{
    if (!m_file || m_finished) {
        return;
    }
    const qint64 remaining = m_fileSize - m_offset;
    if (remaining <= 0) {
        sendEof();
        return;
    }
    const int chunk = int(qMin<qint64>(remaining, qMin<quint32>(m_bufferSize, 4096)));
    m_file->seek(m_offset);
    const QByteArray data = m_file->read(chunk);
    QByteArray packet = makeHeaderBytes(ZDATA, quint32(m_offset), false, m_crc32);
    packet.append(makeSubpacket(data, ZCRCW, m_crc32));
    m_lastPacket = packet;
    write(m_lastPacket);
    m_state = State::SendWaitZack;
    armTimer(kAckTimeoutMs);
}

void ZmodemTransfer::sendEof()
{
    sendHeader(ZEOF, quint32(m_fileSize), false);
    m_state = State::SendWaitRinitAfterEof;
    armTimer(kAckTimeoutMs);
    m_sentTotal += m_fileSize;
    setStatus(tr("发送文件结束…"));
}

void ZmodemTransfer::sendZfin()
{
    sendHeader(ZFIN, 0, true);
    m_state = State::SendWaitZfin;
    armTimer(5000);
}

void ZmodemTransfer::sendZack(quint32 offset)
{
    sendHeader(ZACK, offset, true);
}

void ZmodemTransfer::sendZpos(quint32 offset)
{
    sendHeader(ZRPOS, offset, true);
}

void ZmodemTransfer::handleSendHeader(const Header &header)
{
    switch (header.type) {
    case ZRINIT:
        if (m_state == State::SendWaitRinitAfterEof) {
            if (m_fileIndex + 1 < m_files.size()) {
                ++m_fileIndex;
                if (!openNextFile()) {
                    return;
                }
                sendZfile();
            } else {
                sendZfin();
            }
        } else if (m_state == State::SendWaitRinit) {
            if (header.data.size() == 4) {
                // ZF0（最高字节）是标志，低 3 字节是缓冲大小
                const quint32 flags = quint8(header.data.at(3));
                const quint32 bufSize = quint32(quint8(header.data.at(0)))
                                        | (quint32(quint8(header.data.at(1))) << 8)
                                        | (quint32(quint8(header.data.at(2))) << 16);
                m_crc32 = (flags & 0x20) != 0;   // CANFC32
                m_bufferSize = bufSize ? qBound<quint32>(1024, bufSize, 32768) : 8192;
            }
            sendZfile();
        }
        // 其余状态下收到重复的 ZRINIT 不重发文件头，避免重复 ZFILE
        break;
    case ZRPOS:
        if (header.data.size() == 4) {
            m_offset = qint64(decodeLe32(header.data));
            if (m_offset > m_fileSize) {
                m_offset = m_fileSize;
            }
            m_offset = qMin(m_offset, m_fileSize);
        }
        sendDataFrame();
        break;
    case ZACK:
        if (m_state == State::SendWaitZack && header.data.size() == 4) {
            const quint32 offset = decodeLe32(header.data);
            if (qint64(offset) >= m_fileSize) {
                sendEof();
            } else {
                m_offset = qint64(offset);
                sendDataFrame();
            }
        }
        break;
    case ZSKIP:
        fail(tr("接收方跳过了文件"));
        break;
    case ZABORT:
        fail(tr("接收方中止了传输"));
        break;
    case ZNAK:
        if (!m_lastPacket.isEmpty()) {
            write(m_lastPacket);
            armTimer(kAckTimeoutMs);
        }
        break;
    case ZFIN:
        if (m_state == State::SendWaitZfin) {
            write(QByteArrayLiteral("OO"));
            finish(true, tr("ZMODEM 发送完成：%1 个文件，%2 字节")
                             .arg(m_files.size()).arg(m_sentTotal));
        }
        break;
    case ZRQINIT:
        if (m_state == State::SendWaitRinit) {
            sendHeader(ZRQINIT, 0, true);
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 接收
// ---------------------------------------------------------------------------
void ZmodemTransfer::startReceive()
{
    if (!QFileInfo(receiveDirectory()).isDir()) {
        fail(tr("保存目录不存在：%1").arg(receiveDirectory()));
        return;
    }
    m_state = State::RecvWaitHeader;
    m_dataPart = 0;
    m_rxOffset = 0;
    m_rxBytes = 0;
    m_filesReceived = 0;
    m_currentFileOpen = false;
    setStatus(tr("等待发送方（请在对端运行 sz 文件）"));
    log(tr("ZMODEM 接收保存到 %1").arg(receiveDirectory()));
    sendZrinit();
    armTimer(5000);
}

void ZmodemTransfer::sendZrinitForNextFile()
{
    sendZrinit();
    armTimer(kAckTimeoutMs);
}

void ZmodemTransfer::onData(const QByteArray &data)
{
    if (m_finished) {
        return;
    }
    if (direction() == Direction::Send) {
        // 发送方收到的都是控制头；先解析（可能和回显文本混在一起）
        m_buffer.append(data);
        while (!m_finished) {
            Header header;
            bool needMore = false;
            if (!tryParseHeader(&header, &needMore)) {
                if (needMore) {
                    return;
                }
                if (m_buffer.isEmpty()) {
                    return;
                }
                continue;
            }
            handleSendHeader(header);
        }
        return;
    }

    m_buffer.append(data);
    processReceiveBuffer();
}

bool ZmodemTransfer::tryParseHeader(Header *header, bool *needMore)
{
    *needMore = false;
    int start = m_buffer.indexOf(char(ZPAD));
    if (start < 0) {
        m_buffer.clear();   // 提示文字/噪声
        *needMore = true;
        return false;
    }
    if (start > 0) {
        m_buffer.remove(0, start);
    }
    if (m_buffer.size() < 2) {
        *needMore = true;
        return false;
    }

    if (quint8(m_buffer.at(1)) == ZPAD) {
        // 十六进制头：** ZDLE B + 14 hex + CR LF
        if (m_buffer.size() < 4) {
            *needMore = true;
            return false;
        }
        if (quint8(m_buffer.at(2)) != ZDLE || quint8(m_buffer.at(3)) != ZHEX) {
            m_buffer.remove(0, 1);
            return false;
        }
        if (m_buffer.size() < 4 + 14 + 1) {
            *needMore = true;
            return false;
        }
        auto hexValue = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        QByteArray values;
        for (int i = 0; i < 14; i += 2) {
            const int hi = hexValue(m_buffer.at(4 + i));
            const int lo = hexValue(m_buffer.at(5 + i));
            if (hi < 0 || lo < 0) {
                m_buffer.remove(0, 1);
                return false;
            }
            values.append(char((hi << 4) | lo));
        }
        const quint8 cr = quint8(m_buffer.at(4 + 14));
        if (cr != '\r' && cr != '\n') {
            m_buffer.remove(0, 1);
            return false;
        }
        int consumed = 4 + 14 + 1;
        // 可选 LF / XON
        while (m_buffer.size() > consumed
               && (quint8(m_buffer.at(consumed)) == '\n'
                   || quint8(m_buffer.at(consumed)) == 0x11)) {
            ++consumed;
        }
        const quint8 type = quint8(values.at(0));
        const QByteArray data = values.mid(1, 4);
        const quint8 crcHi = quint8(values.at(5));
        const quint8 crcLo = quint8(values.at(6));
        quint16 crc = 0;
        crc = updcrc(type, crc);
        for (const char byte : data) {
            crc = updcrc(quint8(byte), crc);
        }
        crc = updcrc(crcHi, crc);
        crc = updcrc(crcLo, crc);
        m_buffer.remove(0, consumed);
        if (crc != 0) {
            log(tr("十六进制头 CRC 错误（type %1）").arg(type));
            return false;
        }
        header->type = type;
        header->data = data;
        header->crc32 = false;
        return true;
    }

    if (quint8(m_buffer.at(1)) == ZDLE) {
        if (m_buffer.size() < 3) {
            *needMore = true;
            return false;
        }
        const quint8 kind = quint8(m_buffer.at(2));
        if (kind != ZBIN && kind != ZBIN32) {
            m_buffer.remove(0, 1);
            return false;
        }
        const bool crc32 = kind == ZBIN32;
        const int wanted = crc32 ? 9 : 7;   // type + 4 data + crc
        QByteArray values;
        int pos = 3;
        while (values.size() < wanted) {
            if (pos >= m_buffer.size()) {
                *needMore = true;
                return false;
            }
            quint8 byte = quint8(m_buffer.at(pos));
            if (byte == ZDLE) {
                if (pos + 1 >= m_buffer.size()) {
                    *needMore = true;
                    return false;
                }
                const quint8 next = quint8(m_buffer.at(pos + 1));
                if (next == 'l') {
                    byte = 0x7F;          // ZRUB0
                } else if (next == 'm') {
                    byte = 0xFF;          // ZRUB1
                } else {
                    byte = quint8(next ^ 0x40);
                }
                pos += 2;
            } else {
                ++pos;
            }
            values.append(char(byte));
        }
        m_buffer.remove(0, pos);
        if (!crc32) {
            quint16 crc = 0;
            for (const char byte : values) {
                crc = updcrc(quint8(byte), crc);
            }
            if (crc != 0) {
                return false;
            }
        } else {
            quint32 crc = 0xFFFFFFFF;
            for (const char byte : values) {
                crc = updcrc32(quint8(byte), crc);
            }
            if (crc != kCrc32Residue) {
                return false;
            }
        }
        header->type = quint8(values.at(0));
        header->data = values.mid(1, 4);
        header->crc32 = crc32;
        return true;
    }

    m_buffer.remove(0, 1);
    return false;
}

void ZmodemTransfer::processReceiveBuffer()
{
    while (!m_finished && !m_buffer.isEmpty()) {
        if (m_state == State::RecvWaitHeader) {
            Header header;
            bool needMore = false;
            if (!tryParseHeader(&header, &needMore)) {
                if (needMore || m_buffer.isEmpty()) {
                    return;
                }
                continue;
            }
            handleReceiveHeader(header);
            continue;
        }
        if (m_state == State::RecvSubpacket) {
            const quint8 byte = quint8(m_buffer.at(0));
            m_buffer.remove(0, 1);
            feedDataByte(byte);
            continue;
        }
        return;
    }
}

void ZmodemTransfer::handleReceiveHeader(const Header &header)
{
    switch (header.type) {
    case ZRQINIT:
        sendZrinit();
        break;
    case ZSINIT:
        m_subPurpose = SubPurpose::Attention;
        m_subCrc32 = header.crc32;
        m_subData.clear();
        m_crc = 0;
        m_crc32Value = 0xFFFFFFFF;
        m_marker = 0;
        m_crcBytesLeft = 0;
        m_crcBytes.clear();
        m_dataPart = 0;
        m_state = State::RecvSubpacket;
        break;
    case ZFILE:
        m_subPurpose = SubPurpose::FileInfo;
        m_subCrc32 = header.crc32;
        m_fileInfo.clear();
        m_subData.clear();
        m_crc = 0;
        m_crc32Value = 0xFFFFFFFF;
        m_marker = 0;
        m_crcBytesLeft = 0;
        m_crcBytes.clear();
        m_dataPart = 0;
        m_state = State::RecvSubpacket;
        break;
    case ZDATA: {
        if (m_state != State::RecvWaitHeader) {
            break;
        }
        const quint32 offset = header.data.size() == 4 ? decodeLe32(header.data) : 0;
        if (qint64(offset) != m_rxOffset) {
            sendZpos(quint32(m_rxOffset));
            break;
        }
        m_subPurpose = SubPurpose::FileData;
        m_subCrc32 = header.crc32;
        m_subData.clear();
        m_crc = 0;
        m_crc32Value = 0xFFFFFFFF;
        m_marker = 0;
        m_crcBytesLeft = 0;
        m_crcBytes.clear();
        m_dataPart = 0;
        m_state = State::RecvSubpacket;
        break;
    }
    case ZEOF:
        if (m_file) {
            m_file->flush();
            m_file->close();
        }
        m_currentFileOpen = false;
        setStatus(tr("等待下一个文件或结束…"));
        sendZrinitForNextFile();
        break;
    case ZFIN:
        sendHeader(ZFIN, 0, true);
        finish(true, tr("ZMODEM 接收完成：%1 个文件，%2 字节")
                         .arg(m_filesReceived).arg(m_rxBytes));
        break;
    case ZSKIP:
        fail(tr("发送方跳过了文件"));
        break;
    case ZABORT:
        fail(tr("发送方中止了传输"));
        break;
    case ZNAK:
        if (!m_lastPacket.isEmpty()) {
            write(m_lastPacket);
        }
        break;
    default:
        break;
    }
}

void ZmodemTransfer::feedDataByte(quint8 byte)
{
    if (m_dataPart == 0) {
        if (byte == ZDLE) {
            m_dataPart = 1;
            return;
        }
        m_subData.append(char(byte));
        m_crc = updcrc(byte, m_crc);
        m_crc32Value = updcrc32(byte, m_crc32Value);
        return;
    }
    if (m_dataPart == 1) {
        m_dataPart = 0;
        if (byte == ZCRCE || byte == ZCRCG || byte == ZCRCQ || byte == ZCRCW) {
            m_marker = byte;
            m_crc = updcrc(byte, m_crc);
            m_crc32Value = updcrc32(byte, m_crc32Value);
            m_dataPart = 2;
            m_crcBytesLeft = m_subCrc32 ? 4 : 2;
            m_crcBytes.clear();
            return;
        }
        quint8 value = byte;
        if (byte == 'l') {
            value = 0x7F;
        } else if (byte == 'm') {
            value = 0xFF;
        } else {
            value = quint8(byte ^ 0x40);
        }
        m_subData.append(char(value));
        m_crc = updcrc(value, m_crc);
        m_crc32Value = updcrc32(value, m_crc32Value);
        return;
    }
    if (m_dataPart == 2) {
        if (byte == ZDLE) {
            m_dataPart = 3;
            return;
        }
        m_crc = updcrc(byte, m_crc);
        m_crc32Value = updcrc32(byte, m_crc32Value);
        m_crcBytes.append(char(byte));
        if (--m_crcBytesLeft == 0) {
            finishSubpacket();
        }
        return;
    }
    if (m_dataPart == 3) {
        quint8 value = byte;
        if (byte == 'l') {
            value = 0x7F;
        } else if (byte == 'm') {
            value = 0xFF;
        } else {
            value = quint8(byte ^ 0x40);
        }
        m_crc = updcrc(value, m_crc);
        m_crc32Value = updcrc32(value, m_crc32Value);
        m_crcBytes.append(char(value));
        m_dataPart = 2;
        if (--m_crcBytesLeft == 0) {
            finishSubpacket();
        }
    }
}

void ZmodemTransfer::finishSubpacket()
{
    const bool good = m_subCrc32 ? (m_crc32Value == kCrc32Residue) : (m_crc == 0);
    const quint8 marker = m_marker;
    const QByteArray data = m_subData;
    const SubPurpose purpose = m_subPurpose;

    m_subData.clear();
    m_crc = 0;
    m_crc32Value = 0xFFFFFFFF;
    m_marker = 0;
    m_crcBytesLeft = 0;
    m_crcBytes.clear();
    m_dataPart = 0;

    if (!good) {
        log(tr("数据块 CRC 错误，请求重发"));
        if (purpose == SubPurpose::FileData) {
            sendZpos(quint32(m_rxOffset));
        } else {
            sendHeader(ZNAK, 0, true);
        }
        m_state = State::RecvWaitHeader;
        return;
    }

    if (purpose == SubPurpose::FileInfo) {
        QByteArray info = data;
        if (info.endsWith('\0')) {
            info.chop(1);
        }
        m_fileInfo = info;
        openFileFromInfo();
        return;
    }
    if (purpose == SubPurpose::Attention) {
        sendZack(0);
        m_state = State::RecvWaitHeader;
        return;
    }

    // 文件数据
    if (m_currentFileOpen && m_file) {
        m_file->write(data);
        m_rxOffset += data.size();
        m_rxBytes += data.size();
        setProgress(m_rxOffset, m_fileSize > 0 ? m_fileSize : -1);
    }

    if (marker == ZCRCQ || marker == ZCRCW) {
        sendZack(quint32(m_rxOffset));
    }
    if (marker == ZCRCE || marker == ZCRCW) {
        m_state = State::RecvWaitHeader;
    } else {
        m_state = State::RecvSubpacket;
    }
}

void ZmodemTransfer::openFileFromInfo()
{
    const int nul = m_fileInfo.indexOf('\0');
    const QByteArray nameBytes = nul >= 0 ? m_fileInfo.left(nul) : m_fileInfo;
    QString name = sanitizeName(QString::fromUtf8(nameBytes));
    if (name.isEmpty()) {
        name = QStringLiteral("zmodem-received.bin");
    }

    qint64 size = -1;
    if (nul >= 0) {
        QByteArray rest = m_fileInfo.mid(nul + 1).trimmed();
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
    m_fileSize = size;

    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    const QString path = QDir(receiveDirectory()).filePath(name);
    m_file = new QFile(path);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sendHeader(ZNAK, 0, true);
        fail(tr("无法创建文件：%1").arg(m_file->errorString()));
        return;
    }
    m_currentFileOpen = true;
    m_rxOffset = 0;
    ++m_filesReceived;
    setProgress(0, m_fileSize > 0 ? m_fileSize : -1);
    setStatus(tr("正在接收 %1…").arg(name));
    log(tr("接收文件：%1（%2）")
            .arg(path, m_fileSize >= 0 ? tr("%1 字节").arg(m_fileSize)
                                       : tr("大小未知")));
    sendZpos(0);
    m_state = State::RecvWaitHeader;
}

void ZmodemTransfer::armTimer(int milliseconds)
{
    m_timer->start(milliseconds);
}

void ZmodemTransfer::onTimeout()
{
    if (m_finished) {
        return;
    }
    if (++m_retries > kMaxRetries) {
        fail(tr("等待对端响应超时"));
        return;
    }
    if (direction() == Direction::Send) {
        if (!m_lastPacket.isEmpty()) {
            log(tr("超时，重发（第 %1 次）").arg(m_retries));
            write(m_lastPacket);
            armTimer(kAckTimeoutMs);
        }
        return;
    }
    if (m_state == State::RecvWaitHeader) {
        if (m_currentFileOpen) {
            sendZpos(quint32(m_rxOffset));
        } else {
            sendZrinit();
        }
        armTimer(3000);
    }
}

void ZmodemTransfer::cancel()
{
    if (m_finished) {
        return;
    }
    m_timer->stop();
    write(QByteArray(8, char(0x18)));   // 8 x CAN
    if (m_file) {
        m_file->flush();
        m_file->close();
    }
    finish(false, tr("传输已取消"));
}

