#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QColor>
#include <QString>
#include <QVector>
#include <QMap>
#include "terminal/terminaltheme.h"

class QWidget;
class QEvent;

// ============================================================================
// 主题管理器（单例）：TerminalTheme/AppTheme 的生产与持久化。
// TerminalTheme 数据结构定义于 src/terminal/terminaltheme.h。
// ============================================================================

struct AppTheme {
    QString name;
    QString id;

    QColor windowBackground;
    QColor windowText;
    QColor buttonBackground;
    QColor buttonText;
    QColor highlight;
    QColor highlightText;
    QColor link;
    QColor menuBackground;
    QColor menuText;
    QColor toolTipBase;
    QColor toolTipText;

    // ---- 外壳扩展色板（GUI 重设计：统一深色/浅色设计语言） ----
    QColor panel;          // 菜单/工具栏/侧栏/状态栏底色
    QColor panelHover;     // 悬停背景
    QColor panelRaised;    // 输入框/浮层/卡片底
    QColor surfaceAlt;     // 次级容器底（分组、列表交替行）
    QColor border;         // 1px 描边
    QColor borderStrong;   // 输入框/按钮描边
    QColor hairline;       // 发丝分隔线（低对比，仅用于划分区域）
    QColor scrollbar;      // 滚动条滑块
    QColor textDim;        // 次文字/未激活
    QColor textFaint;      // 占位符/弱提示
    QColor accent;         // 主强调（选中/焦点/主按钮）
    QColor accentHover;    // 主强调悬停
    QColor accentText;     // 强调色"作为文字"时的可读版本（浅色更深、深色更亮）
    QColor success;        // 已连接
    QColor warning;        // 串口/告警
    QColor danger;         // 错误/断开
    int radiusSm = 6;      // 小控件圆角（徽标/标签）
    int radius = 8;        // 控件圆角（按钮/输入框）
    int radiusLg = 12;     // 卡片/弹层圆角
    bool dark = true;      // 暗色主题（图标着色/派生用）
};

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    static ThemeManager* instance();

    void initialize();

    QVector<TerminalTheme> terminalThemes() const;
    QVector<AppTheme> appThemes() const;
    
    TerminalTheme terminalTheme(const QString &id) const;
    AppTheme appTheme(const QString &id) const;
    
    QString currentTerminalThemeId() const;
    QString currentAppThemeId() const;
    
    // userInitiated=false 用于"跟随应用主题"的自动切换，不会关闭自动配对
    void setTerminalTheme(const QString &id, bool userInitiated = true);
    void setAppTheme(const QString &id);
    
    TerminalTheme currentTerminalTheme() const;
    AppTheme currentAppTheme() const;
    
    void saveSettings();
    void loadSettings();

    // 与外壳主题明暗匹配的默认终端配色（同 id 优先）
    QString pairedTerminalThemeId(const AppTheme &appTheme) const;
    // 终端配色是否仍跟随外壳主题
    bool terminalThemeFollowsApp() const { return m_terminalThemeAuto; }

    // 将当前应用主题渲染为 QSS（占位符替换）并应用到 qApp；主题切换时自动调用
    void applyStylesheet();
    // 图标基础色（由当前应用主题的次文字色决定）
    QColor iconColor() const;

    // 设置窗口外边框颜色（Windows 11 起支持；其它平台/旧系统上为空操作）
    static void setWindowBorderColor(QWidget *window, const QColor &color);

protected:
    // 窗口显示时同步 Windows 标题栏明暗（深色主题下标题栏也跟随）
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void terminalThemeChanged(const TerminalTheme &theme);
    void appThemeChanged(const AppTheme &theme);

private:
    ThemeManager(QObject *parent = nullptr);
    ~ThemeManager();
    
    void initTerminalThemes();
    void initAppThemes();
    void applyAppTheme(const AppTheme &theme);
    // 让窗口（含标题栏）跟随当前主题明暗
    void applyWindowChrome(QWidget *window);
    
    static ThemeManager *m_instance;
    
    QMap<QString, TerminalTheme> m_terminalThemes;
    QMap<QString, AppTheme> m_appThemes;
    
    QString m_currentTerminalThemeId;
    QString m_currentAppThemeId;
    // 终端配色是否继续跟随外壳主题明暗（用户亲手选过终端配色后为 false）
    bool m_terminalThemeAuto = true;
};

#endif // THEMEMANAGER_H
