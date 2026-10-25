#include "theme/thememanager.h"
#include <QSettings>
#include <QApplication>
#include <QPalette>
#include <QFile>
#include <QDebug>
#include <QWidget>
#include <QEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// 动态加载 dwmapi：标题栏/边框着色只在 Windows 上生效，避免为这一项功能
// 在工程里增加链接依赖。
#ifdef Q_OS_WIN
using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);

DwmSetWindowAttributeFn dwmSetWindowAttribute()
{
    static const DwmSetWindowAttributeFn setAttribute = [] {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        return dwm ? reinterpret_cast<DwmSetWindowAttributeFn>(
                         GetProcAddress(dwm, "DwmSetWindowAttribute"))
                   : nullptr;
    }();
    return setAttribute;
}

// 深色主题时把 Windows 原生标题栏切成深色（Win10 1809+ 属性 20，旧版 19）。
void setImmersiveDarkTitleBar(HWND hwnd, bool dark)
{
    const DwmSetWindowAttributeFn setAttribute = dwmSetWindowAttribute();
    if (!setAttribute || !hwnd) {
        return;
    }
    BOOL value = dark ? TRUE : FALSE;
    if (FAILED(setAttribute(hwnd, 20, &value, sizeof(value)))) {
        setAttribute(hwnd, 19, &value, sizeof(value));
    }
}
#endif

} // namespace

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
    if (qApp) {
        qApp->installEventFilter(this);
    }
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

    // 缺省终端主题：明亮（与缺省外壳主题 default-light 配套）
    m_currentTerminalThemeId = "vscode-light";
}

// 从基础色派生扩展 tokens（手工主题只需定义基础色）。
// 深/浅模式由 windowBackground 亮度判定，保证任意主题都有完整色板。
// 逐项补齐：手工精调过的 token 保留原值，只补它没有定义的项。
static void deriveAppTokens(AppTheme &t)
{
    const bool dark = t.windowBackground.lightness() < 128;
    t.dark = dark;
    const QColor bg = t.windowBackground;
    // 浅色：面板比窗口底色略深（侧栏/工具栏下沉），输入框与终端为白色（抬起），
    // 用明度差 + 更清晰的描边划分子窗口；深色：面板比底色略亮。
    if (!t.panel.isValid())
        t.panel = dark ? bg.lighter(114) : bg.darker(106);
    if (!t.panelHover.isValid())
        t.panelHover = dark ? bg.lighter(124) : bg.darker(112);
    if (!t.panelRaised.isValid())
        t.panelRaised = dark ? bg.lighter(131) : QColor(255, 255, 255);
    if (!t.surfaceAlt.isValid())
        t.surfaceAlt = dark ? bg.lighter(108) : bg.darker(103);
    if (!t.border.isValid())
        t.border = dark ? bg.lighter(138) : bg.darker(120);
    if (!t.borderStrong.isValid())
        t.borderStrong = dark ? bg.lighter(155) : bg.darker(140);
    // 发丝线比普通描边更弱，只用来分区，不参与控件轮廓
    if (!t.hairline.isValid())
        t.hairline = dark ? bg.lighter(121) : bg.darker(110);
    if (!t.scrollbar.isValid())
        t.scrollbar = dark ? bg.lighter(150) : bg.darker(116);
    if (!t.textDim.isValid())
        t.textDim = dark ? t.windowText.darker(142) : t.windowText.lighter(148);
    if (!t.textFaint.isValid())
        t.textFaint = dark ? t.windowText.darker(182) : t.windowText.lighter(185);
    if (!t.accent.isValid())
        t.accent = t.highlight;
    if (!t.accentHover.isValid())
        t.accentHover = t.highlight.lighter(112);
    // 强调色用作文字时：深色主题提亮、浅色主题压暗，保证次级（tonal）按钮的对比度
    if (!t.accentText.isValid())
        t.accentText = dark ? t.accent.lighter(148) : t.accent.darker(118);
    if (!t.success.isValid())
        t.success = dark ? QColor(0x3F, 0xB9, 0x50) : QColor(0x1F, 0x88, 0x3D);
    if (!t.warning.isValid())
        t.warning = dark ? QColor(0xD2, 0x99, 0x22) : QColor(0x9A, 0x67, 0x00);
    if (!t.danger.isValid())
        t.danger = dark ? QColor(0xF8, 0x51, 0x49) : QColor(0xCF, 0x22, 0x2E);
    // 圆角是结构 token，不随手工主题变化
    t.radiusSm = 6;
    t.radius = 8;
    t.radiusLg = 12;
    if (t.menuBackground == QColor()) t.menuBackground = t.panel;
    if (t.menuText == QColor()) t.menuText = t.windowText;
    if (t.buttonBackground == QColor()) t.buttonBackground = t.panelRaised;
    if (t.buttonText == QColor()) t.buttonText = t.windowText;
    if (t.toolTipBase == QColor()) t.toolTipBase = t.panelRaised;
    if (t.toolTipText == QColor()) t.toolTipText = t.windowText;
}

void ThemeManager::initAppThemes()
{
    AppTheme theme;
    
    // 1. Default Light
    theme = AppTheme();
    theme.id = "default-light";
    theme.name = tr("Default Light");
    theme.windowBackground = QColor(246, 247, 249);
    theme.windowText = QColor(31, 35, 40);
    theme.buttonBackground = QColor(238, 240, 243);
    theme.buttonText = QColor(31, 35, 40);
    theme.highlight = QColor(9, 105, 218);
    theme.highlightText = QColor(255, 255, 255);
    theme.link = QColor(9, 105, 218);
    theme.menuBackground = QColor(246, 247, 249);
    theme.menuText = QColor(31, 35, 40);
    theme.toolTipBase = QColor(255, 255, 221);
    theme.toolTipText = QColor(31, 35, 40);
    m_appThemes[theme.id] = theme;

    // 2. Fresh Light —— 小清新：薄荷白底、墨绿灰文字、青绿强调。
    // 面板/描边仍由 deriveAppTokens 派生，层级与默认浅色一致，只是色相偏青绿。
    theme = AppTheme();
    theme.id = "fresh-light";
    theme.name = tr("Fresh Light");
    theme.windowBackground = QColor(0xED, 0xF8, 0xF2);   // 薄荷白
    theme.windowText = QColor(0x2A, 0x40, 0x38);         // 墨绿灰
    theme.buttonBackground = QColor(0xE1, 0xF0, 0xE9);
    theme.buttonText = QColor(0x2A, 0x40, 0x38);
    theme.highlight = QColor(0x3F, 0xB7, 0x9A);          // 青绿（选中/焦点）
    theme.highlightText = QColor(0x0F, 0x33, 0x2A);
    theme.link = QColor(0x1F, 0x8F, 0x7A);
    theme.menuBackground = QColor(0xE7, 0xF4, 0xED);
    theme.menuText = QColor(0x2A, 0x40, 0x38);
    theme.toolTipBase = QColor(0xED, 0xF8, 0xF2);
    theme.toolTipText = QColor(0x2A, 0x40, 0x38);
    m_appThemes[theme.id] = theme;

    // 3. Default Dark —— 设计稿基准主题（GUI_REDESIGN.md 色板）
    theme = AppTheme();
    theme.id = "default-dark";
    theme.name = tr("Default Dark");
    theme.windowBackground = QColor(0x17, 0x18, 0x1B);   // bg
    theme.windowText     = QColor(0xD7, 0xDB, 0xE0);     // text
    theme.panel          = QColor(0x1E, 0x20, 0x24);     // panel
    theme.panelHover     = QColor(0x26, 0x28, 0x2D);     // panel2
    theme.panelRaised    = QColor(0x26, 0x28, 0x2D);
    theme.border         = QColor(0x2E, 0x31, 0x38);
    theme.borderStrong   = QColor(0x3A, 0x3E, 0x46);
    theme.textDim        = QColor(0x8B, 0x91, 0x9A);
    theme.textFaint      = QColor(0x5C, 0x62, 0x6B);
    theme.accent         = QColor(0x4C, 0x8D, 0xFF);
    theme.accentHover    = QColor(0x5B, 0x99, 0xFF);
    theme.highlight      = QColor(0x4C, 0x8D, 0xFF);
    theme.highlightText  = QColor(0xFF, 0xFF, 0xFF);
    theme.link           = QColor(0x79, 0xC0, 0xFF);
    theme.menuBackground = QColor(0x1E, 0x20, 0x24);
    theme.menuText       = QColor(0xD7, 0xDB, 0xE0);
    theme.buttonBackground = QColor(0x26, 0x28, 0x2D);
    theme.buttonText     = QColor(0xD7, 0xDB, 0xE0);
    theme.toolTipBase    = QColor(0x2C, 0x2F, 0x35);
    theme.toolTipText    = QColor(0xD7, 0xDB, 0xE0);
    theme.success        = QColor(0x3F, 0xB9, 0x50);
    theme.warning        = QColor(0xD2, 0x99, 0x22);
    theme.danger         = QColor(0xF8, 0x51, 0x49);
    theme.radius = 6;
    theme.dark = true;
    m_appThemes[theme.id] = theme;
    
    // 4. Solarized Light
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
    
    // 5. Solarized Dark
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
    
    // 6. Dracula
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
    
    // 7. Nord
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

    // 统一派生/补齐扩展 tokens：手工精调过的主题保留自己的色板，
    // 只补它没有定义的项，保证任意主题都具备完整设计语言色板
    for (auto it = m_appThemes.begin(); it != m_appThemes.end(); ++it) {
        deriveAppTokens(it.value());
    }

    // 缺省外壳主题：小清新（薄荷绿）
    m_currentAppThemeId = "fresh-light";
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

void ThemeManager::setTerminalTheme(const QString &id, bool userInitiated)
{
    if (!userInitiated) {
        // 跟随外壳主题：仅在配色确实变化时通知
        if (m_terminalThemes.contains(id) && m_currentTerminalThemeId != id) {
            m_currentTerminalThemeId = id;
            emit terminalThemeChanged(m_terminalThemes[id]);
        }
        return;
    }
    if (m_terminalThemes.contains(id) && m_currentTerminalThemeId != id) {
        m_currentTerminalThemeId = id;
        m_terminalThemeAuto = false;   // 用户显式选择后不再自动配对
        emit terminalThemeChanged(m_terminalThemes[id]);
        saveSettings();
    }
}

QString ThemeManager::pairedTerminalThemeId(const AppTheme &appTheme) const
{
    // 同名配色（Nord / Dracula / Solarized …）优先，保证外壳与终端同一视觉语言
    if (m_terminalThemes.contains(appTheme.id)) {
        return appTheme.id;
    }
    return appTheme.dark ? QStringLiteral("vscode-dark")
                         : QStringLiteral("vscode-light");
}

void ThemeManager::setAppTheme(const QString &id)
{
    if (m_appThemes.contains(id) && m_currentAppThemeId != id) {
        m_currentAppThemeId = id;
        emit appThemeChanged(m_appThemes[id]);
        applyAppTheme(m_appThemes[id]);
        if (m_terminalThemeAuto) {
            setTerminalTheme(pairedTerminalThemeId(m_appThemes[id]), false);
        }
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
    settings.setValue("TerminalThemeAuto", m_terminalThemeAuto);
    settings.endGroup();
}

void ThemeManager::loadSettings()
{
    QSettings settings;
    settings.beginGroup("Theme");

    // 缺省主题：小清新外壳 + 浅底终端。用户显式切换后以保存值为准。
    QString terminalThemeId = settings.value("TerminalTheme", "vscode-light").toString();
    QString appThemeId = settings.value("AppTheme", "fresh-light").toString();
    m_terminalThemeAuto = settings.value("TerminalThemeAuto", true).toBool();

    if (m_appThemes.contains(appThemeId)) {
        m_currentAppThemeId = appThemeId;
        applyAppTheme(m_appThemes[appThemeId]);
    }

    if (m_terminalThemeAuto) {
        // 跟随模式：终端明暗与外壳保持一致（首次启动即深色/浅色配套）
        m_currentTerminalThemeId = pairedTerminalThemeId(m_appThemes.value(m_currentAppThemeId));
    } else if (m_terminalThemes.contains(terminalThemeId)) {
        m_currentTerminalThemeId = terminalThemeId;
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

    // 完整外观由 QSS 模板（resources/styles/app.qss）统一驱动
    applyStylesheet();
}

// 渲染 QSS 模板：占位符 -> 当前主题 tokens
void ThemeManager::applyStylesheet()
{
    const AppTheme theme = currentAppTheme();

    QFile qssFile(QStringLiteral(":/styles/app.qss"));
    if (!qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[ThemeManager] Cannot load :/styles/app.qss";
        return;
    }
    QString qss = QString::fromUtf8(qssFile.readAll());

    auto rgba = [](const QColor &c, qreal alpha) {
        return QString("rgba(%1,%2,%3,%4)")
                .arg(c.red()).arg(c.green()).arg(c.blue())
                .arg(alpha, 0, 'f', 2);
    };

    struct Token { const char *key; QString value; };
    const Token tokens[] = {
        { "@bg@",           theme.windowBackground.name() },
        { "@panel@",        theme.panel.name() },
        { "@panelHover@",   theme.panelHover.name() },
        { "@panelRaised@",  theme.panelRaised.name() },
        { "@surfaceAlt@",   theme.surfaceAlt.name() },
        { "@border@",       theme.border.name() },
        { "@borderStrong@", theme.borderStrong.name() },
        { "@hairline@",     theme.hairline.name() },
        { "@scrollbar@",    theme.scrollbar.name() },
        { "@text@",         theme.windowText.name() },
        { "@textDim@",      theme.textDim.name() },
        { "@textFaint@",    theme.textFaint.name() },
        { "@accent@",       theme.accent.name() },
        { "@accentHover@",  theme.accentHover.name() },
        { "@accentText@",   theme.accentText.name() },
        { "@onAccent@",     theme.highlightText.name() },
        { "@accentSoft@",   rgba(theme.accent, theme.dark ? 0.20 : 0.14) },
        { "@accentSoftStrong@", rgba(theme.accent, theme.dark ? 0.34 : 0.26) },
        { "@success@",      theme.success.name() },
        { "@successSoft@",  rgba(theme.success, 0.14) },
        { "@warning@",      theme.warning.name() },
        { "@warningSoft@",  rgba(theme.warning, 0.14) },
        { "@danger@",       theme.danger.name() },
        { "@dangerSoft@",   rgba(theme.danger, 0.12) },
        // 勾选图标按强调色明暗自动选白色/深色描边版本
        { "@checkIcon@",    theme.accent.lightness() < 150
                                ? QStringLiteral(":/icons/check.svg")
                                : QStringLiteral(":/icons/check-dark.svg") },
        { "@radiusSm@",     QString::number(theme.radiusSm) },
        { "@radius@",       QString::number(theme.radius) },
        { "@radiusLg@",     QString::number(theme.radiusLg) },
        { "@fontSize@",     "13" },
    };
    for (const Token &token : tokens) {
        qss.replace(QLatin1String(token.key), token.value);
    }

    qApp->setStyleSheet(qss);

    // 已存在的窗口立即跟随明暗（新窗口在 Show 事件里处理）
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows) {
        applyWindowChrome(window);
    }
}

QColor ThemeManager::iconColor() const
{
    const AppTheme theme = currentAppTheme();
    return theme.textDim.isValid() ? theme.textDim : theme.windowText;
}

void ThemeManager::applyWindowChrome(QWidget *window)
{
#ifdef Q_OS_WIN
    if (!window || !window->isWindow()) {
        return;
    }
    setImmersiveDarkTitleBar(reinterpret_cast<HWND>(window->winId()),
                             currentAppTheme().dark);
#else
    Q_UNUSED(window);
#endif
}

void ThemeManager::setWindowBorderColor(QWidget *window, const QColor &color)
{
#ifdef Q_OS_WIN
    if (!window || !window->isWindow() || !color.isValid()) {
        return;
    }
    const DwmSetWindowAttributeFn setAttribute = dwmSetWindowAttribute();
    if (!setAttribute) {
        return;
    }
    // DWMWA_BORDER_COLOR = 34（Windows 11 起支持）。旧系统调用失败时保持默认边框，
    // 不影响窗口功能，因此忽略返回值。
    const COLORREF border = RGB(color.red(), color.green(), color.blue());
    setAttribute(reinterpret_cast<HWND>(window->winId()), 34, &border, sizeof(border));
#else
    Q_UNUSED(window);
    Q_UNUSED(color);
#endif
}

bool ThemeManager::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Show) {
        if (QWidget *widget = qobject_cast<QWidget *>(watched)) {
            applyWindowChrome(widget);
        }
    }
    return QObject::eventFilter(watched, event);
}
