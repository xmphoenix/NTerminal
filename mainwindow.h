#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class TerminalWidget;
class QSplitter;
class QListWidget;
class QTabWidget;
class QTextEdit;
class QMenuBar;
class QMenu;
class QToolBar;
class QAction;
class SessionManager;
class Logger;

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
    void setupLogger();
    void startLogging();
    void stopLogging();
    void toggleLogging();

    QSplitter *m_verticalSplitter;
    QSplitter *m_horizontalSplitter;
    QListWidget *m_listWidget;
    QTabWidget *m_screenTabWidget;
    QTextEdit *m_commandTextEdit;
    TerminalWidget *m_terminal;

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
    
    Logger *m_logger;
    QAction *m_startLoggingAction;
    QAction *m_stopLoggingAction;
    QAction *m_toggleLoggingAction;
};
#endif // MAINWINDOW_H
