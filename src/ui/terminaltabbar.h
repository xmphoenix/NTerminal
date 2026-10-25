#ifndef TERMINALTABBAR_H
#define TERMINALTABBAR_H

#include <QColor>
#include <QTabBar>
#include <QString>
#include <functional>

// ============================================================================
// 终端标签栏（GUI 重设计）
//
// 扁平、首尾相接的标签条：底色与发丝分隔线自绘，选中标签的底色取终端
// 文字区背景色（随终端主题变化），从而与下方终端内容连成一片。
//
// 标签带协议识别色（与侧栏会话树同色）：左侧 26px 内边距处绘制协议图标，
// 已连接为实色、未连接为淡化色；选中标签顶部有 2px 识别色细条，并向下
// 渐隐地铺一层轻微识别色，既突出当前标签又保持与终端内容的衔接。
// 文字/选中态文字配色仍由 resources/styles/app.qss 的 #terminalTabBar 规则驱动。
//
// 每个标签的协议与状态通过 VisualProvider 回调查询（tabId -> TabVisual），
// 使 tabbar 与 MainWindow 的会话字典解耦。
// ============================================================================
class TerminalTabBar : public QTabBar
{
    Q_OBJECT

public:
    struct TabVisual {
        bool connected = false;
        QColor accent;                                        // 协议识别色
        QString icon = QStringLiteral("server");               // IconProvider 名称
    };

    using VisualProvider = std::function<TabVisual(const QString &tabId)>;

    explicit TerminalTabBar(QWidget *parent = nullptr);

    void setVisualProvider(VisualProvider provider);
    // 连接状态变化后调用，触发重绘
    void refreshStates();
    // 界面/终端主题变化后调用：重算"选中标签文字色"并重绘
    void refreshTheme();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    VisualProvider m_visualProvider;
};

#endif // TERMINALTABBAR_H
