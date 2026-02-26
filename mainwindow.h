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
};
#endif // MAINWINDOW_H
