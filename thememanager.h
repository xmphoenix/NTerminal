#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QColor>
#include <QString>
#include <QVector>
#include <QMap>

struct TerminalTheme {
    QString name;
    QString id;
    
    QColor defaultForeground;
    QColor defaultBackground;
    QColor cursorColor;
    QColor selectionBackground;
    QColor selectionForeground;
    
    QColor ansiColors[16];
    
    QColor ansi256Color(int index) const;
};

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
