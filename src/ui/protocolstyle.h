#ifndef PROTOCOLSTYLE_H
#define PROTOCOLSTYLE_H

#include "common/connectionsettings.h"

#include <QColor>
#include <QString>

// ============================================================================
// 协议视觉样式（侧栏会话项 / 终端标签条共用）
//
// 同一条连接在会话树和标签条上使用相同的图标、识别色与徽标文字，
// 用户可以把"侧栏里的 SSH 蓝"直接对应到"标签上的 SSH 蓝"。
// 颜色为深色主题基准值；浅色主题下用 protocolAccentColor() 适当加深。
// ============================================================================

struct ProtocolVisual {
    QString icon;   // IconProvider 名称
    QColor color;   // 图标 / 徽标 / 标签强调色
    QString badge;  // 侧栏徽标文字（大写短名）
};

inline ProtocolVisual protocolVisual(Common::ConnectionType type)
{
    switch (type) {
    case Common::ConnectionType::Serial:
        return { QStringLiteral("serial"), QColor(0xD2, 0x99, 0x22), QStringLiteral("SERIAL") };
    case Common::ConnectionType::Telnet:
        return { QStringLiteral("telnet"), QColor(0xC9, 0xA0, 0xFF), QStringLiteral("TELNET") };
    case Common::ConnectionType::Rlogin:
        return { QStringLiteral("terminal"), QColor(0xC9, 0xA0, 0xFF), QStringLiteral("RLOGIN") };
    case Common::ConnectionType::SFTP:
        return { QStringLiteral("sftp"), QColor(0x4C, 0x8D, 0xFF), QStringLiteral("SFTP") };
    case Common::ConnectionType::SCP:
        return { QStringLiteral("sftp"), QColor(0x4C, 0x8D, 0xFF), QStringLiteral("SCP") };
    case Common::ConnectionType::FTP:
    case Common::ConnectionType::TFTP:
        return { QStringLiteral("transfer"), QColor(0x8B, 0x91, 0x9A), QStringLiteral("FTP") };
    case Common::ConnectionType::RDP:
        return { QStringLiteral("rdp"), QColor(0x2E, 0xA0, 0x6A), QStringLiteral("RDP") };
    case Common::ConnectionType::SSH:
    default:
        return { QStringLiteral("server"), QColor(0x4C, 0x8D, 0xFF), QStringLiteral("SSH") };
    }
}

// 按界面主题明暗校正后的识别色：浅色主题上原色对比度偏低（尤其是淡紫），
// 统一加深一档，保证标签图标和徽标可读。
inline QColor protocolAccentColor(Common::ConnectionType type, bool dark)
{
    const QColor color = protocolVisual(type).color;
    return dark ? color : color.darker(118);
}

#endif // PROTOCOLSTYLE_H
