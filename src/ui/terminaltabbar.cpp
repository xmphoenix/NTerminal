#include "ui/terminaltabbar.h"
#include "ui/iconprovider.h"
#include "theme/thememanager.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QVector>

namespace {
// 标签左侧协议图标：位置与 QSS 中 ::tab 的 26px 左内边距对应
constexpr int kIconLeft = 8;
constexpr int kIconSize = 13;
// 选中标签顶部的协议识别色细条
constexpr int kAccentBarHeight = 2;

QColor blend(const QColor &base, const QColor &overlay, qreal amount)
{
    return QColor(qRound(base.red() * (1.0 - amount) + overlay.red() * amount),
                  qRound(base.green() * (1.0 - amount) + overlay.green() * amount),
                  qRound(base.blue() * (1.0 - amount) + overlay.blue() * amount));
}
} // namespace

TerminalTabBar::TerminalTabBar(QWidget *parent)
    : QTabBar(parent)
{
    setDrawBase(false);
    setExpanding(false);
    setMovable(true);
    setTabsClosable(true);
    setContextMenuPolicy(Qt::CustomContextMenu);

    refreshTheme();
}

void TerminalTabBar::setVisualProvider(VisualProvider provider)
{
    m_visualProvider = std::move(provider);
    update();
}

void TerminalTabBar::refreshStates()
{
    update();
}

void TerminalTabBar::refreshTheme()
{
    // 选中标签的底色取自终端配色，可能与界面主题明暗相反；
    // 用动态属性让 QSS 选中对应的文字色（见 app.qss 的 terminalBgLight）。
    const QColor terminalBg =
        ThemeManager::instance()->currentTerminalTheme().defaultBackground;
    const QString light =
        terminalBg.lightness() > 150 ? QStringLiteral("true") : QStringLiteral("false");
    if (property("terminalBgLight").toString() != light) {
        setProperty("terminalBgLight", light);
        // 属性选择器的颜色变化需要重新 polish 才会生效
        style()->unpolish(this);
        style()->polish(this);
    }
    update();
}

void TerminalTabBar::paintEvent(QPaintEvent *event)
{
    const AppTheme theme = ThemeManager::instance()->currentAppTheme();
    // 标签底色跟随终端文字区（而不是界面面板色）：选中标签因此与终端内容
    // 连成一片，同时随终端主题变化，不需要在 QSS 里再写一份色值。
    const QColor terminalBg =
        ThemeManager::instance()->currentTerminalTheme().defaultBackground;
    const int current = currentIndex();

    // 一次绘制内只查询一次每个标签的协议/状态
    QVector<TabVisual> visuals(count());
    if (m_visualProvider) {
        for (int i = 0; i < count(); ++i) {
            visuals[i] = m_visualProvider(tabData(i).toString());
        }
    }
    const auto accentFor = [&theme](const TabVisual &visual) {
        return visual.accent.isValid() ? visual.accent : theme.accent;
    };

    // 背景自绘：QSS 对 QTabBar 容器设置 background-color 会触发
    // QStyleSheetStyle 在自绘标签栏上的绘制崩溃（Qt 6.10），因此由本类填充。
    {
        QPainter bg(this);
        bg.fillRect(rect(), theme.panel);
        // 标签条底边线：选中标签随后会用自己的背景把它盖住，
        // 于是"选中的标签与终端面板连成一体，其余部分有分界线"。
        bg.fillRect(0, height() - 1, width(), 1, theme.hairline);

        // 选中标签：终端底色上叠一层很淡的协议识别色，并从顶部向下渐隐，
        // 既点明当前标签的协议，又让标签下缘继续与终端内容无缝衔接。
        if (current >= 0) {
            const QRect r = tabRect(current);
            const QColor accent = accentFor(visuals[current]);
            QLinearGradient tint(0, r.top(), 0, r.bottom());
            tint.setColorAt(0.0, blend(terminalBg, accent, theme.dark ? 0.16 : 0.10));
            tint.setColorAt(0.62, terminalBg);
            tint.setColorAt(1.0, terminalBg);
            bg.fillRect(r, tint);
        }
        // 相邻标签之间的发丝竖线；选中标签两侧不画，保持它的边缘干净
        for (int i = 0; i + 1 < count(); ++i) {
            if (i == current || i + 1 == current) {
                continue;
            }
            bg.fillRect(tabRect(i + 1).left(), 4, 1, height() - 8, theme.hairline);
        }
    }

    // 标签项外观（文字/背景/选中底线）交给 QSS
    QTabBar::paintEvent(event);

    if (!m_visualProvider) {
        return;
    }

    QPainter painter(this);
    const qreal dpr = devicePixelRatioF();

    for (int i = 0; i < count(); ++i) {
        const TabVisual &visual = visuals[i];
        QColor iconColor = accentFor(visual);
        // 协议色始终保留（用户可一眼认出标签类型），未连接时淡化表达状态；
        // 已连接则是实色，与原来的"绿点"含义一致但不丢失协议识别色。
        if (!visual.connected) {
            iconColor.setAlphaF(0.38);
        }
        const QPixmap icon =
            IconProvider::pixmap(visual.icon, iconColor, kIconSize, dpr);
        if (icon.isNull()) {
            continue;
        }
        const QRect r = tabRect(i);
        painter.drawPixmap(r.left() + kIconLeft,
                           r.center().y() + 1 - kIconSize / 2, icon);
    }

    // 选中标签顶部的识别色细条（画在文字/图标之上，hover 也不会被盖住）
    if (current >= 0) {
        const QRect r = tabRect(current);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accentFor(visuals[current]));
        painter.drawRect(QRect(r.left(), r.top(), r.width(), kAccentBarHeight));
    }
}
