#include "ui/sessionitemdelegate.h"
#include "ui/iconprovider.h"
#include "theme/thememanager.h"
#include "common/connectionsettings.h"

#include <QPainter>

namespace {

struct ProtocolStyle {
    const char *icon;     // IconProvider 名称
    QColor color;         // 图标/徽标色
    const char *badge;    // 徽标文字
};

ProtocolStyle protocolStyle(Common::ConnectionType type)
{
    switch (type) {
    case Common::ConnectionType::Serial:
        return { "serial", QColor(0xD2, 0x99, 0x22), "SERIAL" };
    case Common::ConnectionType::Telnet:
        return { "telnet", QColor(0xC9, 0xA0, 0xFF), "TELNET" };
    case Common::ConnectionType::Rlogin:
        return { "terminal", QColor(0xC9, 0xA0, 0xFF), "RLOGIN" };
    case Common::ConnectionType::SFTP:
        return { "sftp", QColor(0x4C, 0x8D, 0xFF), "SFTP" };
    case Common::ConnectionType::FTP:
    case Common::ConnectionType::TFTP:
        return { "transfer", QColor(0x8B, 0x91, 0x9A), "FTP" };
    case Common::ConnectionType::SSH:
    default:
        return { "server", QColor(0x4C, 0x8D, 0xFF), "SSH" };
    }
}

} // namespace

SessionItemDelegate::SessionItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QSize SessionItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    Q_UNUSED(option);
    return QSize(200, index.data(SessionGroupRole).toBool() ? 28 : 44);
}

void SessionItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    if (index.data(SessionGroupRole).toBool()) {
        paintGroup(painter, option, index);
    } else {
        paintSession(painter, option, index);
    }
}

void SessionItemDelegate::paintGroup(QPainter *painter,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    const AppTheme theme = ThemeManager::instance()->currentAppTheme();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect r = option.rect;
    // 展开箭头
    const bool expanded = option.state & QStyle::State_Open;
    const QRect chevronRect(r.left() + 4, r.center().y() - 6, 12, 12);
    painter->drawPixmap(chevronRect,
                        IconProvider::pixmap(expanded ? "chevron-down" : "chevron-right",
                                             theme.textFaint, 12, option.widget ? option.widget->devicePixelRatioF() : 1.0));
    // 文件夹图标
    const QRect folderRect(r.left() + 18, r.center().y() - 7, 14, 14);
    painter->drawPixmap(folderRect,
                        IconProvider::pixmap("folder", theme.textDim, 14, option.widget ? option.widget->devicePixelRatioF() : 1.0));

    // 标题（小号大写）
    QFont font = option.font;
    font.setPixelSize(11);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
    painter->setFont(font);
    painter->setPen(theme.textDim);
    const QRect textRect(r.left() + 38, r.top(), r.width() - 60, r.height());
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(Qt::DisplayRole).toString());

    // 数量
    const int count = index.data(Qt::UserRole + 10).toInt();
    if (count > 0) {
        QFont countFont = option.font;
        countFont.setPixelSize(10);
        painter->setFont(countFont);
        painter->setPen(theme.textFaint);
        painter->drawText(QRect(r.left(), r.top(), r.width() - 8, r.height()),
                          Qt::AlignRight | Qt::AlignVCenter, QString::number(count));
    }

    painter->restore();
}

void SessionItemDelegate::paintSession(QPainter *painter,
                                       const QStyleOptionViewItem &option,
                                       const QModelIndex &index) const
{
    const AppTheme theme = ThemeManager::instance()->currentAppTheme();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool active = index.data(SessionActiveRole).toBool();
    const Common::ConnectionType type =
        static_cast<Common::ConnectionType>(index.data(SessionTypeRole).toInt());
    const ProtocolStyle style = protocolStyle(type);
    const qreal dpr = option.widget ? option.widget->devicePixelRatioF() : 1.0;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect r = option.rect.adjusted(2, 1, -2, -1);

    // 背景
    if (selected) {
        QColor bg = theme.accent;
        bg.setAlphaF(0.14);
        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawRoundedRect(r, theme.radius, theme.radius);
        // 左侧选中指示条
        QRect bar(r.left(), r.top() + 9, 3, r.height() - 18);
        painter->setBrush(theme.accent);
        painter->drawRoundedRect(bar, 1.5, 1.5);
    } else if (hovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(theme.panelHover);
        painter->drawRoundedRect(r, theme.radius, theme.radius);
    }

    // 状态点
    const QColor dotColor = active ? theme.success : theme.textFaint;
    painter->setPen(Qt::NoPen);
    if (active) {
        QColor glow = dotColor;
        glow.setAlphaF(0.18);
        painter->setBrush(glow);
        painter->drawEllipse(QPoint(r.left() + 14, r.center().y()), 5, 5);
    }
    painter->setBrush(dotColor);
    painter->drawEllipse(QPoint(r.left() + 14, r.center().y()), 3, 3);

    // 协议图标
    const QRect iconRect(r.left() + 24, r.center().y() - 7, 14, 14);
    painter->drawPixmap(iconRect, IconProvider::pixmap(style.icon, style.color, 14, dpr));

    // 徽标（右对齐）
    QFont badgeFont = option.font;
    badgeFont.setPixelSize(9);
    badgeFont.setWeight(QFont::Bold);
    badgeFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.3);
    const QFontMetrics badgeMetrics(badgeFont);
    const QString badge = QString::fromLatin1(style.badge);
    const int badgeW = badgeMetrics.horizontalAdvance(badge) + 8;
    const QRect badgeRect(r.right() - badgeW - 6, r.center().y() - 8, badgeW, 16);
    QColor badgeBg = style.color;
    badgeBg.setAlphaF(0.12);
    QColor badgeBorder = style.color;
    badgeBorder.setAlphaF(0.28);
    painter->setBrush(badgeBg);
    painter->setPen(QPen(badgeBorder, 1));
    painter->drawRoundedRect(badgeRect, 3, 3);
    painter->setFont(badgeFont);
    painter->setPen(style.color);
    painter->drawText(badgeRect, Qt::AlignCenter, badge);

    // 名称 + 副文本
    const int textLeft = r.left() + 44;
    const int textRight = badgeRect.left() - 6;
    QFont nameFont = option.font;
    nameFont.setPixelSize(12);
    painter->setFont(nameFont);
    painter->setPen(theme.windowText);
    painter->drawText(QRect(textLeft, r.top() + 5, textRight - textLeft, 17),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(Qt::DisplayRole).toString());

    QFont subFont = option.font;
    subFont.setPixelSize(10);
    subFont.setFamily(QStringLiteral("JetBrains Mono"));
    subFont.setFamily(QStringLiteral("Consolas"));
    painter->setFont(subFont);
    painter->setPen(theme.textFaint);
    painter->drawText(QRect(textLeft, r.top() + 22, textRight - textLeft, 15),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(SessionHostRole).toString());

    painter->restore();
}
