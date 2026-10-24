#include "ui/terminaltabbar.h"
#include "ui/iconprovider.h"
#include "theme/thememanager.h"

#include <QPainter>
#include <QPixmap>
#include <QStyle>

namespace {
// 标签左侧状态图标：位置与 QSS 中 ::tab 的 26px 左内边距对应
constexpr int kIconLeft = 8;
constexpr int kIconSize = 13;
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

void TerminalTabBar::setStateProvider(StateProvider provider)
{
    m_stateProvider = std::move(provider);
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

    // 背景自绘：QSS 对 QTabBar 容器设置 background-color 会触发
    // QStyleSheetStyle 在自绘标签栏上的绘制崩溃（Qt 6.10），因此由本类填充。
    {
        QPainter bg(this);
        bg.fillRect(rect(), theme.panel);
        // 标签条底边线：选中标签随后会用自己的背景把它盖住，
        // 于是"选中的标签与终端面板连成一体，其余部分有分界线"。
        bg.fillRect(0, height() - 1, width(), 1, theme.hairline);

        // 选中标签的 QSS 背景是透明的，这里补上终端底色，并盖住底边线
        if (current >= 0) {
            bg.fillRect(tabRect(current), terminalBg);
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

    if (!m_stateProvider) {
        return;
    }

    QPainter painter(this);
    const qreal dpr = devicePixelRatioF();

    for (int i = 0; i < count(); ++i) {
        const QString tabId = tabData(i).toString();
        const bool connected = m_stateProvider(tabId);

        // 连接状态用图标颜色表达：绿 = 已连接，灰 = 未连接
        const QColor color = connected ? theme.success : theme.textFaint;
        const QPixmap icon =
            IconProvider::pixmap(QStringLiteral("server"), color, kIconSize, dpr);
        if (icon.isNull()) {
            continue;
        }
        const QRect r = tabRect(i);
        painter.drawPixmap(r.left() + kIconLeft,
                           r.center().y() + 1 - kIconSize / 2, icon);
    }
}
