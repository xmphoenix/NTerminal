#include "thememanager.h"
#include <QSettings>
#include <QApplication>
#include <QPalette>
#include <QDebug>

ThemeManager *ThemeManager::m_instance = nullptr;

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
}

ThemeManager::~ThemeManager()
{
}

ThemeManager* ThemeManager::instance()
{
    if (!m_instance) {
        m_instance = new ThemeManager();
    }
    return m_instance;
}

void ThemeManager::initialize()
{
    initTerminalThemes();
    initAppThemes();
    loadSettings();
}

QColor TerminalTheme::ansi256Color(int index) const
{
    if (index < 16) {
        return ansiColors[index];
    }
    
    if (index < 232) {
        int n = index - 16;
        int r = (n / 36) ? (n / 36) * 40 + 55 : 0;
        int g = ((n % 36) / 6) ? ((n % 36) / 6) * 40 + 55 : 0;
        int b = (n % 6) ? (n % 6) * 40 + 55 : 0;
        return QColor(r, g, b);
    }
    
    int gray = (index - 232) * 10 + 8;
    return QColor(gray, gray, gray);
}

void ThemeManager::initTerminalThemes()
{
    TerminalTheme theme;
    
    // 1. Default (Classic)
    theme = TerminalTheme();
    theme.id = "default";
    theme.name = tr("Default");
    theme.defaultForeground = QColor(255, 255, 255);
    theme.defaultBackground = QColor(0, 0, 0);
    theme.cursorColor = QColor(255, 255, 255);
    theme.selectionBackground = QColor(0, 120, 215);
    theme.selectionForeground = QColor(255, 255, 255);
    
    theme.ansiColors[0]  = QColor(0, 0, 0);
    theme.ansiColors[1]  = QColor(170, 0, 0);
    theme.ansiColors[2]  = QColor(0, 170, 0);
    theme.ansiColors[3]  = QColor(170, 85, 0);
    theme.ansiColors[4]  = QColor(0, 0, 170);
    theme.ansiColors[5]  = QColor(170, 0, 170);
    theme.ansiColors[6]  = QColor(0, 170, 170);
    theme.ansiColors[7]  = QColor(170, 170, 170);
    theme.ansiColors[8]  = QColor(85, 85, 85);
    theme.ansiColors[9]  = QColor(255, 85, 85);
    theme.ansiColors[10] = QColor(85, 255, 85);
    theme.ansiColors[11] = QColor(255, 255, 85);
    theme.ansiColors[12] = QColor(85, 85, 255);
    theme.ansiColors[13] = QColor(255, 85, 255);
    theme.ansiColors[14] = QColor(85, 255, 255);
    theme.ansiColors[15] = QColor(255, 255, 255);
    m_terminalThemes[theme.id] = theme;
    
    // 2. Solarized Dark
    theme = TerminalTheme();
    theme.id = "solarized-dark";
    theme.name = tr("Solarized Dark");
    theme.defaultForeground = QColor(131, 148, 150);
    theme.defaultBackground = QColor(0, 43, 54);
    theme.cursorColor = QColor(147, 161, 161);
    theme.selectionBackground = QColor(0, 86, 107);
    theme.selectionForeground = QColor(238, 232, 213);
    
    theme.ansiColors[0]  = QColor(7, 54, 66);
    theme.ansiColors[1]  = QColor(220, 50, 47);
    theme.ansiColors[2]  = QColor(133, 153, 0);
    theme.ansiColors[3]  = QColor(181, 137, 0);
    theme.ansiColors[4]  = QColor(38, 139, 210);
    theme.ansiColors[5]  = QColor(211, 54, 130);
    theme.ansiColors[6]  = QColor(42, 161, 152);
    theme.ansiColors[7]  = QColor(238, 232, 213);
    theme.ansiColors[8]  = QColor(0, 43, 54);
    theme.ansiColors[9]  = QColor(203, 75, 22);
    theme.ansiColors[10] = QColor(88, 110, 117);
    theme.ansiColors[11] = QColor(101, 123, 131);
    theme.ansiColors[12] = QColor(131, 148, 150);
    theme.ansiColors[13] = QColor(108, 113, 196);
    theme.ansiColors[14] = QColor(147, 161, 161);
    theme.ansiColors[15] = QColor(253, 246, 227);
    m_terminalThemes[theme.id] = theme;
    
    // 3. Solarized Light
    theme = TerminalTheme();
    theme.id = "solarized-light";
    theme.name = tr("Solarized Light");
    theme.defaultForeground = QColor(101, 123, 131);
    theme.defaultBackground = QColor(253, 246, 227);
    theme.cursorColor = QColor(147, 161, 161);
    theme.selectionBackground = QColor(238, 232, 213);
    theme.selectionForeground = QColor(0, 43, 54);
    
    theme.ansiColors[0]  = QColor(7, 54, 66);
    theme.ansiColors[1]  = QColor(220, 50, 47);
    theme.ansiColors[2]  = QColor(133, 153, 0);
    theme.ansiColors[3]  = QColor(181, 137, 0);
    theme.ansiColors[4]  = QColor(38, 139, 210);
    theme.ansiColors[5]  = QColor(211, 54, 130);
    theme.ansiColors[6]  = QColor(42, 161, 152);
    theme.ansiColors[7]  = QColor(238, 232, 213);
    theme.ansiColors[8]  = QColor(0, 43, 54);
    theme.ansiColors[9]  = QColor(203, 75, 22);
    theme.ansiColors[10] = QColor(88, 110, 117);
    theme.ansiColors[11] = QColor(101, 123, 131);
    theme.ansiColors[12] = QColor(131, 148, 150);
    theme.ansiColors[13] = QColor(108, 113, 196);
    theme.ansiColors[14] = QColor(147, 161, 161);
    theme.ansiColors[15] = QColor(253, 246, 227);
    m_terminalThemes[theme.id] = theme;
    
    // 4. Dracula
    theme = TerminalTheme();
    theme.id = "dracula";
    theme.name = tr("Dracula");
    theme.defaultForeground = QColor(248, 248, 242);
    theme.defaultBackground = QColor(40, 42, 54);
    theme.cursorColor = QColor(248, 248, 242);
    theme.selectionBackground = QColor(68, 71, 90);
    theme.selectionForeground = QColor(248, 248, 242);
    
    theme.ansiColors[0]  = QColor(0, 0, 0);
    theme.ansiColors[1]  = QColor(255, 85, 85);
    theme.ansiColors[2]  = QColor(80, 250, 123);
    theme.ansiColors[3]  = QColor(241, 250, 140);
    theme.ansiColors[4]  = QColor(98, 114, 164);
    theme.ansiColors[5]  = QColor(255, 121, 198);
    theme.ansiColors[6]  = QColor(139, 233, 253);
    theme.ansiColors[7]  = QColor(248, 248, 242);
    theme.ansiColors[8]  = QColor(98, 114, 164);
    theme.ansiColors[9]  = QColor(255, 85, 85);
    theme.ansiColors[10] = QColor(80, 250, 123);
    theme.ansiColors[11] = QColor(241, 250, 140);
    theme.ansiColors[12] = QColor(98, 114, 164);
    theme.ansiColors[13] = QColor(255, 121, 198);
    theme.ansiColors[14] = QColor(139, 233, 253);
    theme.ansiColors[15] = QColor(255, 255, 255);
    m_terminalThemes[theme.id] = theme;
    
    // 5. One Dark
    theme = TerminalTheme();
    theme.id = "one-dark";
    theme.name = tr("One Dark");
    theme.defaultForeground = QColor(171, 178, 191);
    theme.defaultBackground = QColor(40, 44, 52);
    theme.cursorColor = QColor(171, 178, 191);
    theme.selectionBackground = QColor(61, 66, 77);
    theme.selectionForeground = QColor(255, 255, 255);
    
    theme.ansiColors[0]  = QColor(40, 44, 52);
    theme.ansiColors[1]  = QColor(224, 108, 117);
    theme.ansiColors[2]  = QColor(152, 195, 121);
    theme.ansiColors[3]  = QColor(229, 192, 123);
    theme.ansiColors[4]  = QColor(97, 175, 239);
    theme.ansiColors[5]  = QColor(198, 120, 221);
    theme.ansiColors[6]  = QColor(86, 182, 194);
    theme.ansiColors[7]  = QColor(171, 178, 191);
    theme.ansiColors[8]  = QColor(92, 99, 112);
    theme.ansiColors[9]  = QColor(224, 108, 117);
    theme.ansiColors[10] = QColor(152, 195, 121);
    theme.ansiColors[11] = QColor(229, 192, 123);
    theme.ansiColors[12] = QColor(97, 175, 239);
    theme.ansiColors[13] = QColor(198, 120, 221);
    theme.ansiColors[14] = QColor(86, 182, 194);
    theme.ansiColors[15] = QColor(255, 255, 255);
    m_terminalThemes[theme.id] = theme;
    
    // 6. Monokai
    theme = TerminalTheme();
    theme.id = "monokai";
    theme.name = tr("Monokai");
    theme.defaultForeground = QColor(248, 248, 242);
    theme.defaultBackground = QColor(39, 40, 34);
    theme.cursorColor = QColor(248, 248, 242);
    theme.selectionBackground = QColor(73, 72, 62);
    theme.selectionForeground = QColor(255, 255, 255);
    
    theme.ansiColors[0]  = QColor(39, 40, 34);
    theme.ansiColors[1]  = QColor(249, 38, 114);
    theme.ansiColors[2]  = QColor(166, 226, 46);
    theme.ansiColors[3]  = QColor(253, 151, 31);
    theme.ansiColors[4]  = QColor(102, 217, 239);
    theme.ansiColors[5]  = QColor(174, 129, 255);
    theme.ansiColors[6]  = QColor(117, 113, 94);
    theme.ansiColors[7]  = QColor(248, 248, 242);
    theme.ansiColors[8]  = QColor(117, 113, 94);
    theme.ansiColors[9]  = QColor(249, 38, 114);
    theme.ansiColors[10] = QColor(166, 226, 46);
    theme.ansiColors[11] = QColor(253, 151, 31);
    theme.ansiColors[12] = QColor(102, 217, 239);
    theme.ansiColors[13] = QColor(174, 129, 255);
    theme.ansiColors[14] = QColor(117, 113, 94);
    theme.ansiColors[15] = QColor(255, 255, 255);
    m_terminalThemes[theme.id] = theme;
    
    // 7. Nord
    theme = TerminalTheme();
    theme.id = "nord";
    theme.name = tr("Nord");
    theme.defaultForeground = QColor(216, 222, 233);
    theme.defaultBackground = QColor(46, 52, 64);
    theme.cursorColor = QColor(216, 222, 233);
    theme.selectionBackground = QColor(94, 129, 172);
    theme.selectionForeground = QColor(255, 255, 255);
    
    theme.ansiColors[0]  = QColor(59, 66, 82);
    theme.ansiColors[1]  = QColor(191, 97, 106);
    theme.ansiColors[2]  = QColor(163, 190, 140);
    theme.ansiColors[3]  = QColor(235, 203, 139);
    theme.ansiColors[4]  = QColor(129, 161, 193);
    theme.ansiColors[5]  = QColor(180, 142, 173);
    theme.ansiColors[6]  = QColor(136, 192, 208);
    theme.ansiColors[7]  = QColor(229, 233, 240);
    theme.ansiColors[8]  = QColor(76, 86, 106);
    theme.ansiColors[9]  = QColor(191, 97, 106);
    theme.ansiColors[10] = QColor(163, 190, 140);
    theme.ansiColors[11] = QColor(235, 203, 139);
    theme.ansiColors[12] = QColor(129, 161, 193);
    theme.ansiColors[13] = QColor(180, 142, 173);
    theme.ansiColors[14] = QColor(143, 188, 187);
    theme.ansiColors[15] = QColor(236, 239, 244);
    m_terminalThemes[theme.id] = theme;
    
    // 8. Gruvbox Dark
    theme = TerminalTheme();
    theme.id = "gruvbox-dark";
    theme.name = tr("Gruvbox Dark");
    theme.defaultForeground = QColor(235, 219, 178);
    theme.defaultBackground = QColor(40, 40, 40);
    theme.cursorColor = QColor(235, 219, 178);
    theme.selectionBackground = QColor(80, 73, 69);
    theme.selectionForeground = QColor(235, 219, 178);
    
    theme.ansiColors[0]  = QColor(40, 40, 40);
    theme.ansiColors[1]  = QColor(251, 73, 52);
    theme.ansiColors[2]  = QColor(184, 187, 38);
    theme.ansiColors[3]  = QColor(250, 189, 47);
    theme.ansiColors[4]  = QColor(131, 165, 152);
    theme.ansiColors[5]  = QColor(211, 134, 155);
    theme.ansiColors[6]  = QColor(142, 192, 124);
    theme.ansiColors[7]  = QColor(235, 219, 178);
    theme.ansiColors[8]  = QColor(124, 111, 100);
    theme.ansiColors[9]  = QColor(251, 73, 52);
    theme.ansiColors[10] = QColor(184, 187, 38);
    theme.ansiColors[11] = QColor(250, 189, 47);
    theme.ansiColors[12] = QColor(131, 165, 152);
    theme.ansiColors[13] = QColor(211, 134, 155);
    theme.ansiColors[14] = QColor(142, 192, 124);
    theme.ansiColors[15] = QColor(235, 219, 178);
    m_terminalThemes[theme.id] = theme;
    
    // 9. GitHub Dark
    theme = TerminalTheme();
    theme.id = "github-dark";
    theme.name = tr("GitHub Dark");
    theme.defaultForeground = QColor(230, 237, 243);
    theme.defaultBackground = QColor(13, 17, 23);
    theme.cursorColor = QColor(230, 237, 243);
    theme.selectionBackground = QColor(56, 139, 253);
    theme.selectionForeground = QColor(255, 255, 255);
    
    theme.ansiColors[0]  = QColor(13, 17, 23);
    theme.ansiColors[1]  = QColor(248, 81, 73);
    theme.ansiColors[2]  = QColor(63, 185, 80);
    theme.ansiColors[3]  = QColor(210, 153, 34);
    theme.ansiColors[4]  = QColor(88, 166, 255);
    theme.ansiColors[5]  = QColor(163, 113, 247);
    theme.ansiColors[6]  = QColor(56, 139, 253);
    theme.ansiColors[7]  = QColor(230, 237, 243);
    theme.ansiColors[8]  = QColor(110, 118, 129);
    theme.ansiColors[9]  = QColor(248, 81, 73);
    theme.ansiColors[10] = QColor(63, 185, 80);
    theme.ansiColors[11] = QColor(210, 153, 34);
    theme.ansiColors[12] = QColor(88, 166, 255);
    theme.ansiColors[13] = QColor(163, 113, 247);
    theme.ansiColors[14] = QColor(56, 139, 253);
    theme.ansiColors[15] = QColor(255, 255, 255);
    m_terminalThemes[theme.id] = theme;
    
    // 10. Ubuntu
    theme = TerminalTheme();
    theme.id = "ubuntu";
    theme.name = tr("Ubuntu");
    theme.defaultForeground = QColor(233, 216, 196);
    theme.defaultBackground = QColor(48, 10, 36);
    theme.cursorColor = QColor(233, 216, 196);
    theme.selectionBackground = QColor(119, 76, 61);
    theme.selectionForeground = QColor(255, 255, 255);
    
    theme.ansiColors[0]  = QColor(48, 10, 36);
    theme.ansiColors[1]  = QColor(222, 75, 45);
    theme.ansiColors[2]  = QColor(38, 162, 67);
    theme.ansiColors[3]  = QColor(193, 144, 31);
    theme.ansiColors[4]  = QColor(36, 101, 188);
    theme.ansiColors[5]  = QColor(157, 59, 147);
    theme.ansiColors[6]  = QColor(37, 177, 179);
    theme.ansiColors[7]  = QColor(233, 216, 196);
    theme.ansiColors[8]  = QColor(118, 109, 102);
    theme.ansiColors[9]  = QColor(222, 75, 45);
    theme.ansiColors[10] = QColor(38, 162, 67);
    theme.ansiColors[11] = QColor(193, 144, 31);
    theme.ansiColors[12] = QColor(36, 101, 188);
    theme.ansiColors[13] = QColor(157, 59, 147);
    theme.ansiColors[14] = QColor(37, 177, 179);
    theme.ansiColors[15] = QColor(233, 216, 196);
    m_terminalThemes[theme.id] = theme;
    
    // 11. VS Code Dark (Default Dark+)
    theme = TerminalTheme();
    theme.id = "vscode-dark";
    theme.name = tr("VS Code Dark");
    theme.defaultForeground = QColor(204, 204, 204);
    theme.defaultBackground = QColor(30, 30, 30);
    theme.cursorColor = QColor(140, 140, 140);
    theme.selectionBackground = QColor(58, 61, 65);
    theme.selectionForeground = QColor(255, 255, 255);

    theme.ansiColors[0]  = QColor(0, 0, 0);
    theme.ansiColors[1]  = QColor(205, 49, 49);
    theme.ansiColors[2]  = QColor(13, 188, 75);
    theme.ansiColors[3]  = QColor(229, 192, 123);
    theme.ansiColors[4]  = QColor(37, 117, 252);
    theme.ansiColors[5]  = QColor(136, 23, 152);
    theme.ansiColors[6]  = QColor(20, 179, 178);
    theme.ansiColors[7]  = QColor(204, 204, 204);
    theme.ansiColors[8]  = QColor(128, 128, 128);
    theme.ansiColors[9]  = QColor(241, 76, 76);
    theme.ansiColors[10] = QColor(35, 209, 91);
    theme.ansiColors[11] = QColor(250, 179, 135);
    theme.ansiColors[12] = QColor(59, 142, 234);
    theme.ansiColors[13] = QColor(180, 0, 158);
    theme.ansiColors[14] = QColor(23, 201, 100);
    theme.ansiColors[15] = QColor(229, 229, 229);
    m_terminalThemes[theme.id] = theme;

    // 12. VS Code Light (Default Light+)
    theme = TerminalTheme();
    theme.id = "vscode-light";
    theme.name = tr("VS Code Light");
    theme.defaultForeground = QColor(51, 51, 51);
    theme.defaultBackground = QColor(255, 255, 255);
    theme.cursorColor = QColor(85, 85, 85);
    theme.selectionBackground = QColor(221, 221, 221);
    theme.selectionForeground = QColor(0, 0, 0);

    theme.ansiColors[0]  = QColor(0, 0, 0);
    theme.ansiColors[1]  = QColor(156, 39, 99);
    theme.ansiColors[2]  = QColor(0, 119, 170);
    theme.ansiColors[3]  = QColor(163, 190, 140);
    theme.ansiColors[4]  = QColor(0, 119, 170);
    theme.ansiColors[5]  = QColor(156, 39, 99);
    theme.ansiColors[6]  = QColor(20, 179, 178);
    theme.ansiColors[7]  = QColor(204, 204, 204);
    theme.ansiColors[8]  = QColor(128, 128, 128);
    theme.ansiColors[9]  = QColor(205, 49, 49);
    theme.ansiColors[10] = QColor(13, 188, 75);
    theme.ansiColors[11] = QColor(229, 192, 123);
    theme.ansiColors[12] = QColor(37, 117, 252);
    theme.ansiColors[13] = QColor(136, 23, 152);
    theme.ansiColors[14] = QColor(20, 179, 178);
    theme.ansiColors[15] = QColor(229, 229, 229);
    m_terminalThemes[theme.id] = theme;

    // 13. VS Code Dark Dimmed
    theme = TerminalTheme();
    theme.id = "vscode-dark-dimmed";
    theme.name = tr("VS Code Dark Dimmed");
    theme.defaultForeground = QColor(185, 185, 185);
    theme.defaultBackground = QColor(40, 40, 40);
    theme.cursorColor = QColor(120, 120, 120);
    theme.selectionBackground = QColor(50, 50, 50);
    theme.selectionForeground = QColor(255, 255, 255);

    theme.ansiColors[0]  = QColor(30, 30, 30);
    theme.ansiColors[1]  = QColor(171, 41, 41);
    theme.ansiColors[2]  = QColor(13, 155, 63);
    theme.ansiColors[3]  = QColor(198, 160, 105);
    theme.ansiColors[4]  = QColor(30, 95, 212);
    theme.ansiColors[5]  = QColor(113, 19, 127);
    theme.ansiColors[6]  = QColor(20, 149, 148);
    theme.ansiColors[7]  = QColor(185, 185, 185);
    theme.ansiColors[8]  = QColor(108, 108, 108);
    theme.ansiColors[9]  = QColor(201, 64, 64);
    theme.ansiColors[10] = QColor(30, 175, 79);
    theme.ansiColors[11] = QColor(217, 149, 115);
    theme.ansiColors[12] = QColor(50, 120, 212);
    theme.ansiColors[13] = QColor(150, 30, 170);
    theme.ansiColors[14] = QColor(30, 169, 168);
    theme.ansiColors[15] = QColor(200, 200, 200);
    m_terminalThemes[theme.id] = theme;

    // 14. VS Code Dark Modern
    theme = TerminalTheme();
    theme.id = "vscode-dark-modern";
    theme.name = tr("VS Code Dark Modern");
    theme.defaultForeground = QColor(204, 204, 204);
    theme.defaultBackground = QColor(17, 17, 17);
    theme.cursorColor = QColor(140, 140, 140);
    theme.selectionBackground = QColor(38, 38, 38);
    theme.selectionForeground = QColor(255, 255, 255);

    theme.ansiColors[0]  = QColor(0, 0, 0);
    theme.ansiColors[1]  = QColor(255, 85, 85);
    theme.ansiColors[2]  = QColor(85, 255, 136);
    theme.ansiColors[3]  = QColor(255, 255, 136);
    theme.ansiColors[4]  = QColor(85, 170, 255);
    theme.ansiColors[5]  = QColor(255, 136, 255);
    theme.ansiColors[6]  = QColor(85, 255, 255);
    theme.ansiColors[7]  = QColor(204, 204, 204);
    theme.ansiColors[8]  = QColor(76, 76, 76);
    theme.ansiColors[9]  = QColor(255, 85, 85);
    theme.ansiColors[10] = QColor(85, 255, 136);
    theme.ansiColors[11] = QColor(255, 255, 136);
    theme.ansiColors[12] = QColor(85, 170, 255);
    theme.ansiColors[13] = QColor(255, 136, 255);
    theme.ansiColors[14] = QColor(85, 255, 255);
    theme.ansiColors[15] = QColor(255, 255, 255);
    m_terminalThemes[theme.id] = theme;

    // 15. VS Code Dark High Contrast
    theme = TerminalTheme();
    theme.id = "vscode-dark-hc";
    theme.name = tr("VS Code Dark High Contrast");
    theme.defaultForeground = QColor(255, 255, 255);
    theme.defaultBackground = QColor(0, 0, 0);
    theme.cursorColor = QColor(255, 255, 255);
    theme.selectionBackground = QColor(58, 61, 65);
    theme.selectionForeground = QColor(255, 255, 255);

    theme.ansiColors[0]  = QColor(0, 0, 0);
    theme.ansiColors[1]  = QColor(255, 0, 0);
    theme.ansiColors[2]  = QColor(0, 255, 0);
    theme.ansiColors[3]  = QColor(255, 255, 0);
    theme.ansiColors[4]  = QColor(0, 0, 255);
    theme.ansiColors[5]  = QColor(255, 0, 255);
    theme.ansiColors[6]  = QColor(0, 255, 255);
    theme.ansiColors[7]  = QColor(255, 255, 255);
    theme.ansiColors[8]  = QColor(128, 128, 128);
    theme.ansiColors[9]  = QColor(255, 85, 85);
    theme.ansiColors[10] = QColor(85, 255, 85);
    theme.ansiColors[11] = QColor(255, 255, 85);
    theme.ansiColors[12] = QColor(85, 85, 255);
    theme.ansiColors[13] = QColor(255, 85, 255);
    theme.ansiColors[14] = QColor(85, 255, 255);
    theme.ansiColors[15] = QColor(255, 255, 255);
    m_terminalThemes[theme.id] = theme;

    // 16. VS Code Light Modern
    theme = TerminalTheme();
    theme.id = "vscode-light-modern";
    theme.name = tr("VS Code Light Modern");
    theme.defaultForeground = QColor(51, 51, 51);
    theme.defaultBackground = QColor(255, 255, 255);
    theme.cursorColor = QColor(0, 0, 0);
    theme.selectionBackground = QColor(221, 221, 221);
    theme.selectionForeground = QColor(0, 0, 0);

    theme.ansiColors[0]  = QColor(0, 0, 0);
    theme.ansiColors[1]  = QColor(165, 30, 30);
    theme.ansiColors[2]  = QColor(15, 128, 15);
    theme.ansiColors[3]  = QColor(148, 107, 0);
    theme.ansiColors[4]  = QColor(0, 68, 170);
    theme.ansiColors[5]  = QColor(136, 0, 136);
    theme.ansiColors[6]  = QColor(0, 136, 136);
    theme.ansiColors[7]  = QColor(204, 204, 204);
    theme.ansiColors[8]  = QColor(128, 128, 128);
    theme.ansiColors[9]  = QColor(205, 49, 49);
    theme.ansiColors[10] = QColor(13, 188, 75);
    theme.ansiColors[11] = QColor(229, 192, 123);
    theme.ansiColors[12] = QColor(37, 117, 252);
    theme.ansiColors[13] = QColor(136, 23, 152);
    theme.ansiColors[14] = QColor(20, 179, 178);
    theme.ansiColors[15] = QColor(229, 229, 229);
    m_terminalThemes[theme.id] = theme;

    m_currentTerminalThemeId = "solarized-light";
}

void ThemeManager::initAppThemes()
{
    AppTheme theme;
    
    // 1. Default Light
    theme = AppTheme();
    theme.id = "default-light";
    theme.name = tr("Default Light");
    theme.windowBackground = QColor(240, 240, 240);
    theme.windowText = QColor(0, 0, 0);
    theme.buttonBackground = QColor(240, 240, 240);
    theme.buttonText = QColor(0, 0, 0);
    theme.highlight = QColor(0, 120, 215);
    theme.highlightText = QColor(255, 255, 255);
    theme.link = QColor(0, 0, 255);
    theme.menuBackground = QColor(240, 240, 240);
    theme.menuText = QColor(0, 0, 0);
    theme.toolTipBase = QColor(255, 255, 220);
    theme.toolTipText = QColor(0, 0, 0);
    m_appThemes[theme.id] = theme;
    
    // 2. Default Dark
    theme = AppTheme();
    theme.id = "default-dark";
    theme.name = tr("Default Dark");
    theme.windowBackground = QColor(45, 45, 45);
    theme.windowText = QColor(220, 220, 220);
    theme.buttonBackground = QColor(55, 55, 55);
    theme.buttonText = QColor(220, 220, 220);
    theme.highlight = QColor(42, 130, 218);
    theme.highlightText = QColor(255, 255, 255);
    theme.link = QColor(42, 130, 218);
    theme.menuBackground = QColor(45, 45, 45);
    theme.menuText = QColor(220, 220, 220);
    theme.toolTipBase = QColor(45, 45, 45);
    theme.toolTipText = QColor(220, 220, 220);
    m_appThemes[theme.id] = theme;
    
    // 3. Solarized Light
    theme = AppTheme();
    theme.id = "solarized-light";
    theme.name = tr("Solarized Light");
    theme.windowBackground = QColor(253, 246, 227);
    theme.windowText = QColor(101, 123, 131);
    theme.buttonBackground = QColor(238, 232, 213);
    theme.buttonText = QColor(101, 123, 131);
    theme.highlight = QColor(38, 139, 210);
    theme.highlightText = QColor(253, 246, 227);
    theme.link = QColor(38, 139, 210);
    theme.menuBackground = QColor(238, 232, 213);
    theme.menuText = QColor(101, 123, 131);
    theme.toolTipBase = QColor(253, 246, 227);
    theme.toolTipText = QColor(101, 123, 131);
    m_appThemes[theme.id] = theme;
    
    // 4. Solarized Dark
    theme = AppTheme();
    theme.id = "solarized-dark";
    theme.name = tr("Solarized Dark");
    theme.windowBackground = QColor(7, 54, 66);
    theme.windowText = QColor(131, 148, 150);
    theme.buttonBackground = QColor(0, 43, 54);
    theme.buttonText = QColor(131, 148, 150);
    theme.highlight = QColor(38, 139, 210);
    theme.highlightText = QColor(238, 232, 213);
    theme.link = QColor(38, 139, 210);
    theme.menuBackground = QColor(0, 43, 54);
    theme.menuText = QColor(131, 148, 150);
    theme.toolTipBase = QColor(0, 43, 54);
    theme.toolTipText = QColor(131, 148, 150);
    m_appThemes[theme.id] = theme;
    
    // 5. Dracula
    theme = AppTheme();
    theme.id = "dracula";
    theme.name = tr("Dracula");
    theme.windowBackground = QColor(40, 42, 54);
    theme.windowText = QColor(248, 248, 242);
    theme.buttonBackground = QColor(68, 71, 90);
    theme.buttonText = QColor(248, 248, 242);
    theme.highlight = QColor(98, 114, 164);
    theme.highlightText = QColor(248, 248, 242);
    theme.link = QColor(139, 233, 253);
    theme.menuBackground = QColor(40, 42, 54);
    theme.menuText = QColor(248, 248, 242);
    theme.toolTipBase = QColor(68, 71, 90);
    theme.toolTipText = QColor(248, 248, 242);
    m_appThemes[theme.id] = theme;
    
    // 6. Nord
    theme = AppTheme();
    theme.id = "nord";
    theme.name = tr("Nord");
    theme.windowBackground = QColor(46, 52, 64);
    theme.windowText = QColor(216, 222, 233);
    theme.buttonBackground = QColor(59, 66, 82);
    theme.buttonText = QColor(216, 222, 233);
    theme.highlight = QColor(94, 129, 172);
    theme.highlightText = QColor(236, 239, 244);
    theme.link = QColor(136, 192, 208);
    theme.menuBackground = QColor(59, 66, 82);
    theme.menuText = QColor(216, 222, 233);
    theme.toolTipBase = QColor(59, 66, 82);
    theme.toolTipText = QColor(216, 222, 233);
    m_appThemes[theme.id] = theme;
    
    m_currentAppThemeId = "default-light";
}

QVector<TerminalTheme> ThemeManager::terminalThemes() const
{
    return m_terminalThemes.values().toVector();
}

QVector<AppTheme> ThemeManager::appThemes() const
{
    return m_appThemes.values().toVector();
}

TerminalTheme ThemeManager::terminalTheme(const QString &id) const
{
    return m_terminalThemes.value(id);
}

AppTheme ThemeManager::appTheme(const QString &id) const
{
    return m_appThemes.value(id);
}

QString ThemeManager::currentTerminalThemeId() const
{
    return m_currentTerminalThemeId;
}

QString ThemeManager::currentAppThemeId() const
{
    return m_currentAppThemeId;
}

void ThemeManager::setTerminalTheme(const QString &id)
{
    if (m_terminalThemes.contains(id) && m_currentTerminalThemeId != id) {
        m_currentTerminalThemeId = id;
        emit terminalThemeChanged(m_terminalThemes[id]);
        saveSettings();
    }
}

void ThemeManager::setAppTheme(const QString &id)
{
    if (m_appThemes.contains(id) && m_currentAppThemeId != id) {
        m_currentAppThemeId = id;
        emit appThemeChanged(m_appThemes[id]);
        applyAppTheme(m_appThemes[id]);
        saveSettings();
    }
}

TerminalTheme ThemeManager::currentTerminalTheme() const
{
    return m_terminalThemes.value(m_currentTerminalThemeId);
}

AppTheme ThemeManager::currentAppTheme() const
{
    return m_appThemes.value(m_currentAppThemeId);
}

void ThemeManager::saveSettings()
{
    QSettings settings;
    settings.beginGroup("Theme");
    settings.setValue("TerminalTheme", m_currentTerminalThemeId);
    settings.setValue("AppTheme", m_currentAppThemeId);
    settings.endGroup();
}

void ThemeManager::loadSettings()
{
    QSettings settings;
    settings.beginGroup("Theme");

    QString terminalThemeId = settings.value("TerminalTheme", "solarized-light").toString();
    QString appThemeId = settings.value("AppTheme", "default-light").toString();
    
    if (m_terminalThemes.contains(terminalThemeId)) {
        m_currentTerminalThemeId = terminalThemeId;
    }
    
    if (m_appThemes.contains(appThemeId)) {
        m_currentAppThemeId = appThemeId;
        applyAppTheme(m_appThemes[appThemeId]);
    }
    
    settings.endGroup();
}

void ThemeManager::applyAppTheme(const AppTheme &theme)
{
    QPalette palette;
    
    palette.setColor(QPalette::Window, theme.windowBackground);
    palette.setColor(QPalette::WindowText, theme.windowText);
    palette.setColor(QPalette::Base, theme.windowBackground);
    palette.setColor(QPalette::AlternateBase, theme.buttonBackground);
    palette.setColor(QPalette::ToolTipBase, theme.toolTipBase);
    palette.setColor(QPalette::ToolTipText, theme.toolTipText);
    palette.setColor(QPalette::Text, theme.windowText);
    palette.setColor(QPalette::Button, theme.buttonBackground);
    palette.setColor(QPalette::ButtonText, theme.buttonText);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Link, theme.link);
    palette.setColor(QPalette::Highlight, theme.highlight);
    palette.setColor(QPalette::HighlightedText, theme.highlightText);
    
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.windowText.darker(150));
    palette.setColor(QPalette::Disabled, QPalette::Text, theme.windowText.darker(150));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.windowText.darker(150));
    
    qApp->setPalette(palette);
    
    QString styleSheet = QString(
        "QMainWindow { background-color: %1; }"
        "QMenuBar { background-color: %2; color: %3; font-size: 12px; }"
        "QMenuBar::item { padding: 2px 8px; font-size: 12px; color: %3; }"
        "QMenuBar::item:selected { background-color: %4; color: %3; }"
        "QMenu { background-color: %2; color: %3; font-size: 12px; }"
        "QMenu::item { padding: 2px 12px; font-size: 12px; color: %3; }"
        "QMenu::item:selected { background-color: %4; color: %3; }"
        "QToolBar { background-color: %2; color: %3; font-size: 12px; spacing: 2px; }"
        "QToolBar::separator { height: 1px; background-color: %4; margin: 2px 4px; }"
        "QToolButton { background-color: transparent; color: %3; padding: 2px 8px; }"
        "QToolButton:hover { background-color: %4; }"
        "QStatusBar { background-color: %2; color: %3; font-size: 12px; }"
        "QListWidget { border: none; border-top: 1px solid #0078D7; }"
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab { background-color: %2; color: %3; padding: 2px 12px; font-size: 12px; }"
        "QTabBar::tab:selected { background-color: %4; }"
        "QTextEdit { border: none; border-top: 1px solid #0078D7; }"
        "TerminalProxy { background-color: transparent; border: none; }"
        "QAbstractScrollArea { background-color: transparent; }"
    )
    .arg(theme.windowBackground.name())
    .arg(theme.menuBackground.name())
    .arg(theme.menuText.name())
    .arg(theme.highlight.name());
    
    qApp->setStyleSheet(styleSheet);
}
