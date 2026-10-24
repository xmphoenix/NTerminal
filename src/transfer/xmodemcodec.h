#ifndef XMODEMCODEC_H
#define XMODEMCODEC_H

#include <QByteArray>

// XMODEM / YMODEM / ZMODEM 共用的 CRC-16（多项式 0x1021，初值 0）
namespace XmodemCodec {

inline quint16 crc16(const char *data, int length, quint16 crc = 0)
{
    for (int i = 0; i < length; ++i) {
        crc ^= quint16(quint8(data[i])) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = quint16((crc << 1) ^ 0x1021);
            } else {
                crc = quint16(crc << 1);
            }
        }
    }
    return crc;
}

inline quint16 crc16(const QByteArray &data)
{
    return crc16(data.constData(), data.size());
}

inline quint8 checksum8(const QByteArray &data)
{
    quint8 sum = 0;
    for (const char byte : data) {
        sum = quint8(sum + quint8(byte));
    }
    return sum;
}

// 组一个 XMODEM/YMODEM 数据块（data 已经按块长补好 0x1A）
inline QByteArray makeBlock(char header, quint8 sequence, const QByteArray &data, bool useCrc)
{
    QByteArray packet;
    packet.reserve(data.size() + 5);
    packet.append(header);
    packet.append(char(sequence));
    packet.append(char(quint8(~sequence)));
    packet.append(data);
    if (useCrc) {
        const quint16 crc = crc16(data);
        packet.append(char(crc >> 8));
        packet.append(char(crc & 0xFF));
    } else {
        packet.append(char(checksum8(data)));
    }
    return packet;
}

} // namespace XmodemCodec

#endif // XMODEMCODEC_H
