#ifndef TERMINALTABBAR_H
#define TERMINALTABBAR_H

#include <QTabBar>
#include <functional>

// ============================================================================
// 终端标签栏（GUI 重设计）
//
// 扁平、首尾相接的标签条：底色与发丝分隔线自绘，选中标签的底色取终端
// 文字区背景色（随终端主题变化），从而与下方终端内容连成一片。
// 标签左侧 26px 内边距处绘制连接状态图标：
//   绿 = 已连接 / 灰 = 未连接（连接中/断开）
// 文字/选中态配色仍由 resources/styles/app.qss 的 #terminalTabBar 规则驱动。
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
    // 界面/终端主题变化后调用：重算"选中标签文字色"并重绘
    void refreshTheme();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    StateProvider m_stateProvider;
};

#endif // TERMINALTABBAR_H
