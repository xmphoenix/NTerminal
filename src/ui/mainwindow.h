#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <functional>
#include "common/connectionsettings.h"
#include "transfer/filetransfer.h"

class TerminalProxy;
class TerminalTabBar;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QTabWidget;
class QStackedWidget;
class QLineEdit;
class QComboBox;
class QLabel;
class QMenuBar;
class QMenu;
class QAction;
class QToolButton;
class QShowEvent;
class QResizeEvent;
class QEvent;
class SessionManager;
class SystemStatsWidget;

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
    // 窗口变窄时逐级收起状态栏的远端资源指标
    void resizeEvent(QResizeEvent *event) override;
    // 语言切换（QEvent::LanguageChange）时重设全部界面文案
    void changeEvent(QEvent *event) override;

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
    // 状态栏远端资源条（当前标签页为 SSH 时显示）
    void updateStatusStats();

private:
    void initMainWindowLayout();
    void initMainWindowMenu();
    // 注册一段"重设文案"回调：立即执行一次，语言切换时再执行
    void bindText(std::function<void()> apply);
    // 重新设置所有已注册文案（菜单、顶栏、侧栏、状态栏等）
    void retranslateUi();
    QWidget *buildTopBar();
    QWidget *buildSidebar();
    QWidget *buildTerminalArea();
    // 无标签页时的空状态引导页（选择会话 / 新建会话）
    QWidget *buildEmptyState();
    // 在空状态与标签页之间切换
    void updateEmptyState();
    void buildStatusBarWidgets();
    void applyMenuIcons();
    // 应用主题切换后：清空图标缓存并按属性 "iconName" 重建按钮图标
    void refreshIcons();
    void setupSessionManager();
    void saveCurrentSession();
    // 登记会话并落盘（新建会话/快速连接在打开前调用）
    void registerSession(const Common::ConnectionSettings &settings);
    void loadSession();
    void manageSessions();
    void populateSessionMenu();
    void populateSessionTree();
    void showThemeSettings();
    void applyTerminalTheme(class TerminalProxy *terminal);
    // 协议传输（kind + 方向）：选择文件/目录后把终端字节流交给协议引擎
    void startFileTransfer(FileTransfer::Kind kind, bool send);
    // 弹出连接对话框并建立新终端标签页（applyTheme: 是否立即应用当前主题/字体）
    // type: 预选协议（传输菜单可直达 SFTP/FTP）
    void openConnectionFromDialog(bool applyTheme,
                                 Common::ConnectionType type = Common::ConnectionType::Serial);
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
    QStackedWidget *m_mainStack = nullptr;
    QWidget *m_emptyState = nullptr;
    QLineEdit *m_commandLineEdit = nullptr;

    QMap<QString, TerminalProxy*> m_terminals;
    TerminalProxy *m_currentTerminal = nullptr;
    int m_nextTerminalId = 1;
    bool m_geometryAdjusted = false;   // showEvent 只校正一次窗口尺寸
    // 文案绑定：语言切换时统一重设，避免为每个控件保存成员指针
    QList<std::function<void()>> m_textBindings;

    // ---- 状态栏 ----
    QLabel *m_statusConnection = nullptr;
    QLabel *m_statusProtocol = nullptr;
    QLabel *m_statusEncoding = nullptr;
    QLabel *m_statusSize = nullptr;
    QLabel *m_statusLatency = nullptr;
    SystemStatsWidget *m_statusStats = nullptr;

    QMenuBar *m_menuBar = nullptr;
    QMenu *m_fileMenu = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_optMenu = nullptr;
    QMenu *m_transmitMenu = nullptr;
    QMenu *m_protocolMenu = nullptr;
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
    // 语言切换（设置 → 语言）
    QMenu *m_languageMenu = nullptr;
    QAction *m_chineseAction = nullptr;
    QAction *m_englishAction = nullptr;
    // 工具/测试动作（按指针重建图标，不受当前界面语言影响）
    QAction *m_terminalTestAction = nullptr;
    QAction *m_calculatorAction = nullptr;
    QAction *m_portScannerAction = nullptr;
    QAction *m_asciiTableAction = nullptr;
    QAction *m_networkServicesAction = nullptr;
    QAction *m_printAction = nullptr;
    QAction *m_aboutAction = nullptr;
    // 编辑 / 视图 / 传输 / 插件菜单动作
    QAction *m_copyAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_clearScreenAction = nullptr;
    QAction *m_toggleSidebarAction = nullptr;
    QAction *m_fullScreenAction = nullptr;
    QAction *m_sftpAction = nullptr;
    QAction *m_ftpAction = nullptr;
    QAction *m_pluginDirAction = nullptr;

    // 脚本相关 Action
    QAction *m_loadScriptAction = nullptr;
    QAction *m_stopScriptAction = nullptr;
    QAction *m_scriptLogAction = nullptr;
    QAction *m_scriptControlAction = nullptr;
};
#endif // MAINWINDOW_H
