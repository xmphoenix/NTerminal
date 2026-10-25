#include "ui/sessionitemdelegate.h"
#include "ui/iconprovider.h"
#include "ui/protocolstyle.h"
#include "theme/thememanager.h"
#include "common/connectionsettings.h"

#include <QPainter>

SessionItemDelegate::SessionItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QSize SessionItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    Q_UNUSED(option);
    return QSize(200, index.data(SessionGroupRole).toBool() ? 30 : 52);
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
    const ProtocolVisual style = protocolVisual(type);
    const qreal dpr = option.widget ? option.widget->devicePixelRatioF() : 1.0;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect r = option.rect.adjusted(2, 2, -2, -2);

    // 背景：Material 列表项式圆角填充，不再画侧边指示条
    if (selected) {
        QColor bg = theme.accent;
        bg.setAlphaF(0.16);
        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawRoundedRect(r, theme.radius, theme.radius);
    } else if (hovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(theme.panelHover);
        painter->drawRoundedRect(r, theme.radius, theme.radius);
    }

    // 协议图标底座（圆角方块，类似手机端 App 图标）
    const QRect chip(r.left() + 8, r.center().y() - 14, 28, 28);
    QColor chipBg = style.color;
    chipBg.setAlphaF(selected ? 0.20 : 0.12);
    painter->setPen(Qt::NoPen);
    painter->setBrush(chipBg);
    painter->drawRoundedRect(chip, 8, 8);
    const QRect iconRect(chip.center().x() - 8, chip.center().y() - 8, 16, 16);
    painter->drawPixmap(iconRect, IconProvider::pixmap(style.icon, style.color, 16, dpr));

    // 连接状态点：贴在图标底座右下角，外圈用底色留白
    const QPoint dotCenter(chip.right() - 1, chip.bottom() - 1);
    const QColor dotColor = active ? theme.success : theme.textFaint;
    painter->setPen(Qt::NoPen);
    painter->setBrush(selected ? theme.panelRaised : theme.panel);
    painter->drawEllipse(dotCenter, 5, 5);
    painter->setBrush(dotColor);
    painter->drawEllipse(dotCenter, 3, 3);

    // 徽标（右对齐，胶囊形）
    QFont badgeFont = option.font;
    badgeFont.setPixelSize(9);
    badgeFont.setWeight(QFont::DemiBold);
    badgeFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.3);
    const QFontMetrics badgeMetrics(badgeFont);
    const QString badge = style.badge;
    const int badgeW = badgeMetrics.horizontalAdvance(badge) + 12;
    const QRect badgeRect(r.right() - badgeW - 8, r.center().y() - 9, badgeW, 18);
    QColor badgeBg = style.color;
    badgeBg.setAlphaF(0.12);
    painter->setBrush(badgeBg);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(badgeRect, theme.radiusSm, theme.radiusSm);
    painter->setFont(badgeFont);
    painter->setPen(style.color);
    painter->drawText(badgeRect, Qt::AlignCenter, badge);

    // 名称 + 副文本
    const int textLeft = chip.right() + 10;
    const int textRight = badgeRect.left() - 8;
    QFont nameFont = option.font;
    nameFont.setPixelSize(13);
    nameFont.setWeight(QFont::Medium);
    painter->setFont(nameFont);
    painter->setPen(theme.windowText);
    painter->drawText(QRect(textLeft, r.top() + 8, textRight - textLeft, 18),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(Qt::DisplayRole).toString());

    QFont subFont = option.font;
    subFont.setPixelSize(10);
    subFont.setFamily(QStringLiteral("JetBrains Mono"));
    subFont.setFamily(QStringLiteral("Consolas"));
    painter->setFont(subFont);
    painter->setPen(theme.textFaint);
    painter->drawText(QRect(textLeft, r.top() + 26, textRight - textLeft, 15),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(SessionHostRole).toString());

    painter->restore();
}
