#ifndef TERMINALTHEME_H
#define TERMINALTHEME_H

#include <QColor>
#include <QString>

// ============================================================================
// 终端渲染主题（纯数据结构，无单例依赖）
//
// 角色划分：
//  - ThemeManager（src/theme）负责生产/持久化 TerminalTheme 实例；
//  - TerminalProxy（src/terminal）只消费本结构完成渲染。
//
// 将数据结构从管理器单例中拆出，使终端核心不依赖 theme 模块。
// ============================================================================

struct TerminalTheme {
    QString name;
    QString id;

    QColor defaultForeground;
    QColor defaultBackground;
    QColor cursorColor;
    QColor selectionBackground;
    QColor selectionForeground;

    QColor ansiColors[16];

    // 返回 xterm 256 色调色板中 index 对应的颜色（0-15 取 ansiColors）
    QColor ansi256Color(int index) const;
};

#endif // TERMINALTHEME_H
