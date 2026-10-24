#include "ui/terminalsplitpage.h"

#include "terminal/terminalproxy.h"

#include <QList>
#include <QSplitter>
#include <QVBoxLayout>

namespace {

QSplitter *makeSplitter(Qt::Orientation orientation, QWidget *parent)
{
    auto *splitter = new QSplitter(orientation, parent);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(4);
    return splitter;
}

void setEqualSizes(QSplitter *splitter)
{
    QList<int> sizes;
    sizes.reserve(splitter->count());
    for (int i = 0; i < splitter->count(); ++i) {
        sizes.append(1);
    }
    splitter->setSizes(sizes);
}

} // namespace

TerminalSplitPage::TerminalSplitPage(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
}

void TerminalSplitPage::setLayoutMode(
    TerminalLayoutMode mode, const QVector<TerminalProxy *> &panes)
{
    // 先把旧布局中的终端取出来，避免删除旧 QSplitter 时把要保留的终端一起删掉。
    for (TerminalProxy *terminal : m_terminals) {
        terminal->hide();
        terminal->setParent(this);
    }

    m_mode = mode;
    m_terminals = panes;
    if (m_terminals.isEmpty()) {
        m_active = nullptr;
    } else if (!m_terminals.contains(m_active)) {
        m_active = m_terminals.first();
    }
    for (TerminalProxy *terminal : m_terminals) {
        terminal->hide();
        terminal->setParent(this);
    }

    if (m_root) {
        m_layout->removeWidget(m_root);
        delete m_root;
        m_root = nullptr;
    }

    rebuild();

    for (TerminalProxy *terminal : m_terminals) {
        terminal->show();
    }
    if (m_active) {
        m_active->setFocus();
    }
}

bool TerminalSplitPage::contains(TerminalProxy *terminal) const
{
    return terminal && m_terminals.contains(terminal);
}

void TerminalSplitPage::setActiveTerminal(TerminalProxy *terminal)
{
    if (m_terminals.contains(terminal)) {
        m_active = terminal;
    }
}

void TerminalSplitPage::rebuild()
{
    if (m_terminals.isEmpty()) {
        return;
    }

    // 分屏里终端数量由 MainWindow 保证；这里仍按实际数量做防御性布局。
    if (m_mode == TerminalLayoutMode::Four && m_terminals.size() >= 4) {
        m_root = makeSplitter(Qt::Horizontal, this);
        auto *left = makeSplitter(Qt::Vertical, m_root);
        auto *right = makeSplitter(Qt::Vertical, m_root);

        left->addWidget(m_terminals.at(0));
        left->addWidget(m_terminals.at(1));
        right->addWidget(m_terminals.at(2));
        right->addWidget(m_terminals.at(3));
        m_root->addWidget(left);
        m_root->addWidget(right);

        setEqualSizes(left);
        setEqualSizes(right);
        setEqualSizes(m_root);
    } else {
        const Qt::Orientation orientation =
            m_mode == TerminalLayoutMode::TwoHorizontal
                ? Qt::Vertical
                : Qt::Horizontal;
        m_root = makeSplitter(orientation, this);
        const int count =
            m_mode == TerminalLayoutMode::Single
                ? qMin(1, m_terminals.size())
                : qMin(2, m_terminals.size());
        for (int i = 0; i < count; ++i) {
            m_root->addWidget(m_terminals.at(i));
        }
        setEqualSizes(m_root);
    }

    m_layout->addWidget(m_root);
}
