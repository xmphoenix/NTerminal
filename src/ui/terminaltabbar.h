#ifndef TERMINALTABBAR_H
#define TERMINALTABBAR_H

#include <QTabBar>
#include <functional>

// ============================================================================
// 终端标签栏（GUI 重设计）
//
// 在 QTabBar 默认绘制（QSS 驱动）之上叠加连接状态点：
//   绿 = 已连接 / 灰 = 未连接（连接中/断开）
// 文字左侧保留 28px 内边距（见 resources/styles/app.qss 的 QTabBar::tab）。
//
// 状态通过 StateProvider 回调查询（tabData(tabId) -> 是否已连接），
// 使 tabbar 与 MainWindow 的 TerminalProxy 字典解耦。
// ============================================================================
class TerminalTabBar : public QTabBar
{
    Q_OBJECT

public:
    using StateProvider = std::function<bool(const QString &tabId)>;

    explicit TerminalTabBar(QWidget *parent = nullptr);

    void setStateProvider(StateProvider provider);
    // 连接状态变化后调用，触发重绘
    void refreshStates();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    StateProvider m_stateProvider;
};

#endif // TERMINALTABBAR_H
