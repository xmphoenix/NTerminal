#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "common/connectionsettings.h"

class TerminalProxy;
class QSplitter;
class QListWidget;
class QTabWidget;
class QTextEdit;
class QMenuBar;
class QMenu;
class QToolBar;
class QAction;
class SessionManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    // 命令输入区回车事件拦截：将命令发送到当前终端
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void openTerminalTester();
    void showLogSettings();
    void onTabContextMenu(const QPoint &pos);
    void duplicateSession();
    void showCalculator();
    void showPortScanner();
    void showNetworkServices();
    void showAsciiTable();

    // 脚本相关槽函数
    void loadScript();
    void stopScript();
    void showScriptLog();
    void showScriptControlPanel();

private:
    void initMainWindowLayout();
    void initMainWindowMenu();
    void setupSessionManager();
    void saveCurrentSession();
    void loadSession();
    void manageSessions();
    void populateSessionMenu();
    void showThemeSettings();
    void applyTerminalTheme(class TerminalProxy *terminal);
    // 弹出连接对话框并建立新终端标签页（applyTheme: 是否立即应用当前主题/字体）
    void openConnectionFromDialog(bool applyTheme);
    
    TerminalProxy* createNewTerminalTab(const QString &tabName = QString());
    void closeTerminalTab(int index);
    void closeTerminalTabById(const QString &terminalId);
    TerminalProxy* getCurrentTerminal() const;
    TerminalProxy* getTerminalById(const QString &terminalId) const;
    void updateCurrentTerminal(TerminalProxy *terminal);
    void onTabCloseRequested(int index);
    void onTabCurrentChanged(int index);

    QSplitter *m_verticalSplitter;
    QSplitter *m_horizontalSplitter;
    QListWidget *m_listWidget;
    QTabWidget *m_screenTabWidget;
    QTextEdit *m_commandTextEdit;
    
    QMap<QString, TerminalProxy*> m_terminals;
    TerminalProxy *m_currentTerminal;
    int m_nextTerminalId;

    QMenuBar *m_menuBar;
    QMenu *m_fileMenu;
    QMenu *m_editMenu;
    QMenu *m_viewMenu;
    QMenu *m_optMenu;
    QMenu *m_transmitMenu;
    QMenu *m_scriptMenu;
    QMenu *m_toolsMenu;
    QMenu *m_pluginMenu;
    QMenu *m_helpMenu;
    QAction *m_sessionAction;
    QAction *m_exitAction;
    QToolBar *m_toolBar;
    QAction *m_toolbarSessionAction;
    QAction *m_debugAction;
    
    SessionManager *m_sessionManager;
    QAction *m_saveSessionAction;
    QAction *m_loadSessionAction;
    QAction *m_manageSessionsAction;
    QAction *m_themeAction;
    QAction *m_logSettingsAction;
    // 持久化"Load Session"子菜单（populateSessionMenu 只重建其内容，避免累积泄漏）
    QMenu *m_loadSessionMenu;

    // 脚本相关 Action
    QAction *m_loadScriptAction;
    QAction *m_stopScriptAction;
    QAction *m_scriptLogAction;
    QAction *m_scriptControlAction;
};
#endif // MAINWINDOW_H
