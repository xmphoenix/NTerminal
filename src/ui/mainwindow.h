#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "common/connectionsettings.h"

class TerminalProxy;
class TerminalTabBar;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QTabWidget;
class QLineEdit;
class QComboBox;
class QLabel;
class QMenuBar;
class QMenu;
class QAction;
class QToolButton;
class QShowEvent;
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
    // 首帧把窗口夹到可用屏幕区域内（见实现）
    void showEvent(QShowEvent *event) override;

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

    // ---- GUI 重设计新增 ----
    void onSessionTreeActivated(QTreeWidgetItem *item, int column);
    void onQuickConnect();
    void filterSessionTree(const QString &text);
    void updateTabStates();
    void updateStatusBar();

private:
    void initMainWindowLayout();
    void initMainWindowMenu();
    QWidget *buildTopBar();
    QWidget *buildSidebar();
    QWidget *buildTerminalArea();
    void buildStatusBarWidgets();
    void applyMenuIcons();
    // 应用主题切换后：清空图标缓存并按属性 "iconName" 重建按钮图标
    void refreshIcons();
    void setupSessionManager();
    void saveCurrentSession();
    void loadSession();
    void manageSessions();
    void populateSessionMenu();
    void populateSessionTree();
    void showThemeSettings();
    void applyTerminalTheme(class TerminalProxy *terminal);
    // 弹出连接对话框并建立新终端标签页（applyTheme: 是否立即应用当前主题/字体）
    void openConnectionFromDialog(bool applyTheme);
    // 按已保存会话打开新标签页
    void openSession(const Common::ConnectionSettings &settings, const QString &name);
    
    TerminalProxy* createNewTerminalTab(const QString &tabName = QString());
    void closeTerminalTab(int index);
    void closeTerminalTabById(const QString &terminalId);
    TerminalProxy* getCurrentTerminal() const;
    TerminalProxy* getTerminalById(const QString &terminalId) const;
    void updateCurrentTerminal(TerminalProxy *terminal);
    void onTabCloseRequested(int index);
    void onTabCurrentChanged(int index);

    // ---- 布局 ----
    // 所有指针成员必须类内初始化为 nullptr：
    // 防御性判空（if (m_xxx)）遇到未初始化垃圾值会误判并解引用 → 间歇崩溃。
    QSplitter *m_verticalSplitter = nullptr;
    QSplitter *m_horizontalSplitter = nullptr;
    QWidget *m_topBar = nullptr;
    QWidget *m_sidebar = nullptr;
    QTreeWidget *m_sessionTree = nullptr;
    QLineEdit *m_sessionSearch = nullptr;
    QComboBox *m_quickProtocolCombo = nullptr;
    QLineEdit *m_quickConnectEdit = nullptr;
    TerminalTabBar *m_terminalTabBar = nullptr;
    QTabWidget *m_screenTabWidget = nullptr;
    QLineEdit *m_commandLineEdit = nullptr;

    QMap<QString, TerminalProxy*> m_terminals;
    TerminalProxy *m_currentTerminal = nullptr;
    int m_nextTerminalId = 1;
    bool m_geometryAdjusted = false;   // showEvent 只校正一次窗口尺寸

    // ---- 状态栏 ----
    QLabel *m_statusConnection = nullptr;
    QLabel *m_statusProtocol = nullptr;
    QLabel *m_statusEncoding = nullptr;
    QLabel *m_statusSize = nullptr;
    QLabel *m_statusLatency = nullptr;

    QMenuBar *m_menuBar = nullptr;
    QMenu *m_fileMenu = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_optMenu = nullptr;
    QMenu *m_transmitMenu = nullptr;
    QMenu *m_scriptMenu = nullptr;
    QMenu *m_toolsMenu = nullptr;
    QMenu *m_pluginMenu = nullptr;
    QMenu *m_helpMenu = nullptr;
    QAction *m_sessionAction = nullptr;
    QAction *m_exitAction = nullptr;

    SessionManager *m_sessionManager = nullptr;
    QAction *m_saveSessionAction = nullptr;
    QAction *m_loadSessionAction = nullptr;
    QAction *m_manageSessionsAction = nullptr;
    QAction *m_themeAction = nullptr;
    QAction *m_logSettingsAction = nullptr;
    // 持久化"Load Session"子菜单（populateSessionMenu 只重建其内容，避免累积泄漏）
    QMenu *m_loadSessionMenu = nullptr;

    // 脚本相关 Action
    QAction *m_loadScriptAction = nullptr;
    QAction *m_stopScriptAction = nullptr;
    QAction *m_scriptLogAction = nullptr;
    QAction *m_scriptControlAction = nullptr;
};
#endif // MAINWINDOW_H
