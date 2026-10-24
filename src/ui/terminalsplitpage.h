#ifndef TERMINALSPLITPAGE_H
#define TERMINALSPLITPAGE_H

#include <QVector>
#include <QWidget>

class QSplitter;
class QVBoxLayout;
class TerminalProxy;

// 终端标签页内部的分屏布局：
//   Single         单终端
//   TwoVertical    左右两个终端（垂直分隔条）
//   TwoHorizontal  上下两个终端（水平分隔条）
//   Four           2x2 四个终端
enum class TerminalLayoutMode {
    Single,
    TwoVertical,
    TwoHorizontal,
    Four
};

class TerminalSplitPage : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalSplitPage(QWidget *parent = nullptr);

    // 重新排列已有终端；不负责终端的创建/销毁。
    void setLayoutMode(TerminalLayoutMode mode,
                       const QVector<TerminalProxy *> &panes);
    TerminalLayoutMode layoutMode() const { return m_mode; }

    QVector<TerminalProxy *> terminals() const { return m_terminals; }
    bool contains(TerminalProxy *terminal) const;
    TerminalProxy *activeTerminal() const { return m_active; }
    void setActiveTerminal(TerminalProxy *terminal);

private:
    void rebuild();

    QVBoxLayout *m_layout = nullptr;
    QSplitter *m_root = nullptr;
    TerminalLayoutMode m_mode = TerminalLayoutMode::Single;
    QVector<TerminalProxy *> m_terminals;
    TerminalProxy *m_active = nullptr;
};

#endif // TERMINALSPLITPAGE_H
