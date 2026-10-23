#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QColor>
#include <QString>
#include <QVector>
#include <QMap>
#include "terminal/terminaltheme.h"

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
    QColor panelRaised;    // 输入框/浮层底
    QColor border;         // 1px 分隔线
    QColor borderStrong;   // 输入框/按钮描边
    QColor textDim;        // 次文字/未激活
    QColor textFaint;      // 占位符/弱提示
    QColor accent;         // 主强调（选中/焦点/主按钮）
    QColor accentHover;    // 主强调悬停
    QColor success;        // 已连接
    QColor warning;        // 串口/告警
    QColor danger;         // 错误/断开
    int radius = 6;        // 控件圆角（px）
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
    
    void setTerminalTheme(const QString &id);
    void setAppTheme(const QString &id);
    
    TerminalTheme currentTerminalTheme() const;
    AppTheme currentAppTheme() const;
    
    void saveSettings();
    void loadSettings();

    // 将当前应用主题渲染为 QSS（占位符替换）并应用到 qApp；主题切换时自动调用
    void applyStylesheet();
    // 图标基础色（由当前应用主题的次文字色决定）
    QColor iconColor() const;

signals:
    void terminalThemeChanged(const TerminalTheme &theme);
    void appThemeChanged(const AppTheme &theme);

private:
    ThemeManager(QObject *parent = nullptr);
    ~ThemeManager();
    
    void initTerminalThemes();
    void initAppThemes();
    void applyAppTheme(const AppTheme &theme);
    
    static ThemeManager *m_instance;
    
    QMap<QString, TerminalTheme> m_terminalThemes;
    QMap<QString, AppTheme> m_appThemes;
    
    QString m_currentTerminalThemeId;
    QString m_currentAppThemeId;
};

#endif // THEMEMANAGER_H
