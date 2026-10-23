#include "ui/terminaltabbar.h"
#include "theme/thememanager.h"

#include <QPainter>

TerminalTabBar::TerminalTabBar(QWidget *parent)
    : QTabBar(parent)
{
    setDrawBase(false);
    setExpanding(false);
    setMovable(true);
    setTabsClosable(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
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

void TerminalTabBar::paintEvent(QPaintEvent *event)
{
    const AppTheme theme = ThemeManager::instance()->currentAppTheme();

    // 背景自绘：QSS 对 QTabBar 容器设置 background-color 会触发
    // QStyleSheetStyle 在自绘标签栏上的绘制崩溃（Qt 6.10），因此由本类填充。
    {
        QPainter bg(this);
        bg.fillRect(rect(), theme.panel);
    }

    // 标签项外观（文字/背景/选中底线）交给 QSS
    QTabBar::paintEvent(event);

    if (!m_stateProvider) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    for (int i = 0; i < count(); ++i) {
        const QString tabId = tabData(i).toString();
        const bool connected = m_stateProvider(tabId);

        const QRect r = tabRect(i);
        const QPoint center(r.left() + 13, r.center().y());

        const QColor color = connected ? theme.success : theme.textFaint;
        // 连接态：外圈微光，提升可读性
        if (connected) {
            painter.setBrush(QColor(color.red(), color.green(), color.blue(), 46));
            painter.drawEllipse(center, 5, 5);
        }
        painter.setBrush(color);
        painter.drawEllipse(center, 3, 3);
    }
}
