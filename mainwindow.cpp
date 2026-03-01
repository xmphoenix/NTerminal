#include "mainwindow.h"
#include "terminalproxy.h"
#include "connectiondialog.h"
#include "sessionmanager.h"
#include "thememanager.h"
#include "fontmanager.h"
#include "themesettingsdialog.h"
#include "testwindow.h"
#include <QSplitter>
#include <QListWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QMenuBar>
#include <QTimer>
#include <QMenu>
#include <QToolBar>
#include <QAction>
#include <QFrame>
#include <QVBoxLayout>
#include <QDebug>

namespace {
ConnectionDialog::ConnectionSettings toDialogSettings(const Common::ConnectionSettings &s) {
    ConnectionDialog::ConnectionSettings d;
    d.sessionName = s.sessionName;
    d.type = static_cast<ConnectionDialog::ConnectionType>(s.type);
    d.portName = s.portName;
    d.baudRate = s.baudRate;
    d.dataBits = s.dataBits;
    d.parity = s.parity;
    d.stopBits = s.stopBits;
    d.flowControl = s.flowControl;
    d.host = s.host;
    d.port = s.port;
    d.username = s.username;
    d.password = s.password;
    d.useSshKey = s.useSshKey;
    d.sshKeyPath = s.sshKeyPath;
    return d;
}

Common::ConnectionSettings toCommonSettings(const ConnectionDialog::ConnectionSettings &d) {
    Common::ConnectionSettings s;
    s.sessionName = d.sessionName;
    s.type = static_cast<Common::ConnectionType>(d.type);
    s.portName = d.portName;
    s.baudRate = d.baudRate;
    s.dataBits = d.dataBits;
    s.parity = d.parity;
    s.stopBits = d.stopBits;
    s.flowControl = d.flowControl;
    s.host = d.host;
    s.port = d.port;
    s.username = d.username;
    s.password = d.password;
    s.useSshKey = d.useSshKey;
    s.sshKeyPath = d.sshKeyPath;
    return s;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_verticalSplitter(nullptr)
    , m_horizontalSplitter(nullptr)
    , m_listWidget(nullptr)
    , m_screenTabWidget(nullptr)
    , m_commandTextEdit(nullptr)
    , m_currentTerminal(nullptr)
    , m_nextTerminalId(1)
    , m_menuBar(nullptr)
    , m_fileMenu(nullptr)
    , m_editMenu(nullptr)
    , m_viewMenu(nullptr)
    , m_optMenu(nullptr)
    , m_transmitMenu(nullptr)
    , m_scriptMenu(nullptr)
    , m_helpMenu(nullptr)
    , m_sessionAction(nullptr)
    , m_exitAction(nullptr)
    , m_toolBar(nullptr)
    , m_toolbarSessionAction(nullptr)
    , m_debugAction(nullptr)
    , m_sessionManager(nullptr)
    , m_saveSessionAction(nullptr)
    , m_loadSessionAction(nullptr)
    , m_manageSessionsAction(nullptr)
{
    initMainWindowMenu();
    initMainWindowLayout();
    setupSessionManager();
}

MainWindow::~MainWindow()
{
    for (TerminalProxy *terminal : m_terminals) {
        if (terminal) {
            terminal->closeConnection();
            terminal->deleteLater();
        }
    }
    m_terminals.clear();
}

void MainWindow::saveCurrentSession()
{
    TerminalProxy *terminal = getCurrentTerminal();
    if (!terminal || !terminal->isConnected()) {
        qDebug() << "No active connection to save";
        return;
    }
    
    Common::ConnectionSettings settings = terminal->getConnectionSettings();
    if (settings.sessionName.isEmpty()) {
        settings.sessionName = "Unnamed Session";
    }
    
    m_sessionManager->addSession(settings.sessionName, toDialogSettings(settings));
    m_sessionManager->saveSessions();
    qDebug() << "Session saved:" << settings.sessionName;
}

void MainWindow::setupSessionManager()
{
    m_sessionManager = new SessionManager(this);
    if (!m_sessionManager->loadSessions()) {
        qWarning() << "Failed to load session configuration";
    }
    
    connect(m_sessionManager, &SessionManager::sessionsLoaded,
            this, &MainWindow::populateSessionMenu);
    connect(m_sessionManager, &SessionManager::sessionAdded,
            this, &MainWindow::populateSessionMenu);
    connect(m_sessionManager, &SessionManager::sessionUpdated,
            this, &MainWindow::populateSessionMenu);
    connect(m_sessionManager, &SessionManager::sessionRemoved,
            this, &MainWindow::populateSessionMenu);
    
    QTimer::singleShot(0, this, &MainWindow::populateSessionMenu);
}

void MainWindow::loadSession()
{
    qDebug() << "loadSession() - To be implemented";
}

void MainWindow::manageSessions()
{
    qDebug() << "manageSessions() - To be implemented";
}

void MainWindow::populateSessionMenu()
{
    if (m_fileMenu) {
        QList<QAction*> actions = m_fileMenu->actions();
        for (QAction* action : actions) {
            if (action->text() == "Load Session") {
                m_fileMenu->removeAction(action);
                break;
            }
        }
    }
    
    if (!m_sessionManager || !m_fileMenu) {
        return;
    }
    
    QList<SessionManager::Session> sessions = m_sessionManager->getSessions();
    
    QMenu* loadSessionMenu = new QMenu("Load Session", m_fileMenu);
    
    for (const SessionManager::Session& session : sessions) {
        QString displayName = QString("%1 (%2)").arg(session.name).arg(session.settings.sessionName);
        QAction* sessionAction = new QAction(displayName, this);
        sessionAction->setData(session.name);
        
        connect(sessionAction, &QAction::triggered, this, [this, session]() {
            Common::ConnectionSettings settings = toCommonSettings(session.settings);
            
            qDebug() << "Loading session:" << session.name;
            
            QString tabName = session.name.isEmpty() ? 
                QString("Terminal %1").arg(m_terminals.size() + 1) : 
                session.name;
            
            TerminalProxy *terminal = createNewTerminalTab(tabName);
            if (terminal) {
                terminal->setConnectionSettings(settings);
                if (terminal->openConnection()) {
                    qDebug() << "Session connection opened";
                } else {
                    qDebug() << "Session connection failed";
                }
            }
        });
        
        loadSessionMenu->addAction(sessionAction);
    }
    
    if (sessions.isEmpty()) {
        QAction* noSessionsAction = new QAction("No sessions available", this);
        noSessionsAction->setEnabled(false);
        loadSessionMenu->addAction(noSessionsAction);
    }
    
    QList<QAction*> fileActions = m_fileMenu->actions();
    bool inserted = false;
    
    for (int i = 0; i < fileActions.size(); ++i) {
        if (fileActions[i] == m_sessionAction && i + 1 < fileActions.size()) {
            m_fileMenu->insertMenu(fileActions[i + 1], loadSessionMenu);
            inserted = true;
            break;
        }
    }
    
    if (!inserted) {
        m_fileMenu->addMenu(loadSessionMenu);
    }
}

TerminalProxy* MainWindow::createNewTerminalTab(const QString &tabName)
{
    QString terminalId = QString("terminal_%1").arg(m_nextTerminalId++);
    QString displayName = tabName.isEmpty() ? QString("Terminal %1").arg(m_terminals.size() + 1) : tabName;
    
    TerminalProxy *terminal = new TerminalProxy();
    
    int tabIndex = m_screenTabWidget->addTab(terminal, displayName);
    
    m_terminals.insert(terminalId, terminal);
    
    m_screenTabWidget->tabBar()->setTabData(tabIndex, terminalId);
    
    m_screenTabWidget->setCurrentIndex(tabIndex);
    
    updateCurrentTerminal(terminal);
    
    qDebug() << "Created new terminal tab:" << displayName << "ID:" << terminalId;
    
    return terminal;
}

void MainWindow::closeTerminalTab(int index)
{
    if (index < 0 || index >= m_screenTabWidget->count()) {
        return;
    }
    
    QString terminalId = m_screenTabWidget->tabBar()->tabData(index).toString();
    
    if (!terminalId.isEmpty()) {
        closeTerminalTabById(terminalId);
    }
}

void MainWindow::closeTerminalTabById(const QString &terminalId)
{
    TerminalProxy *terminal = m_terminals.value(terminalId, nullptr);
    if (!terminal) {
        return;
    }
    
    int tabIndex = -1;
    for (int i = 0; i < m_screenTabWidget->count(); ++i) {
        QString tabTerminalId = m_screenTabWidget->tabBar()->tabData(i).toString();
        if (tabTerminalId == terminalId) {
            tabIndex = i;
            break;
        }
    }
    
    if (terminal->isConnected()) {
        terminal->closeConnection();
    }
    
    m_terminals.remove(terminalId);
    
    if (tabIndex >= 0) {
        m_screenTabWidget->removeTab(tabIndex);
    }
    
    terminal->deleteLater();
    
    qDebug() << "Closed terminal tab ID:" << terminalId;
    
    if (m_currentTerminal == terminal) {
        m_currentTerminal = nullptr;
        qDebug() << "Current terminal closed";
    }
}

TerminalProxy* MainWindow::getCurrentTerminal() const
{
    return m_currentTerminal;
}

TerminalProxy* MainWindow::getTerminalById(const QString &terminalId) const
{
    return m_terminals.value(terminalId, nullptr);
}

void MainWindow::updateCurrentTerminal(TerminalProxy *terminal)
{
    if (m_currentTerminal != terminal) {
        m_currentTerminal = terminal;
        qDebug() << "Current terminal updated";
    }
}

void MainWindow::onTabCloseRequested(int index)
{
    closeTerminalTab(index);
}

void MainWindow::onTabCurrentChanged(int index)
{
    if (index >= 0 && index < m_screenTabWidget->count()) {
        QString terminalId = m_screenTabWidget->tabBar()->tabData(index).toString();
        TerminalProxy *terminal = getTerminalById(terminalId);
        if (terminal) {
            updateCurrentTerminal(terminal);
        }
    }
}

void MainWindow::initMainWindowLayout()
{
    m_verticalSplitter = new QSplitter(Qt::Vertical);
    m_horizontalSplitter = new QSplitter(Qt::Horizontal, m_verticalSplitter);

    m_listWidget = new QListWidget(m_horizontalSplitter);
    m_listWidget->setMaximumWidth(400); // 限制最大宽度为 1/3 (1200/3=400)

    // 创建容器 widget 用于显示顶部蓝色边框
    QWidget* terminalContainer = new QWidget(m_horizontalSplitter);
    terminalContainer->setStyleSheet("QWidget { border-top: 1px solid #0078D7; }");
    QVBoxLayout* containerLayout = new QVBoxLayout(terminalContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);
    
    m_screenTabWidget = new QTabWidget(terminalContainer);
    m_screenTabWidget->setDocumentMode(true); // 使用文档模式，无边框
    containerLayout->addWidget(m_screenTabWidget);
    
    // 设置默认宽度比例：左侧 1/5，右侧 4/5 (1:4)
    m_horizontalSplitter->setStretchFactor(0, 1);
    m_horizontalSplitter->setStretchFactor(1, 4);
    // 设置默认宽度：1200 * 1/5 = 240
    m_horizontalSplitter->setSizes({240, 960});

    m_screenTabWidget->setTabsClosable(true);
    m_screenTabWidget->setMovable(true);

    connect(m_screenTabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);
    connect(m_screenTabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::onTabCurrentChanged);

    m_commandTextEdit = new QTextEdit(QObject::tr("Manual Command Input Area"), m_verticalSplitter);

    m_verticalSplitter->setStretchFactor(0, 5);
    m_verticalSplitter->setStretchFactor(1, 1);

    // 移除所有边距和间隙
    m_verticalSplitter->setContentsMargins(0, 0, 0, 0);
    m_horizontalSplitter->setContentsMargins(0, 0, 0, 0);
    m_listWidget->setContentsMargins(0, 0, 0, 0);
    m_screenTabWidget->setContentsMargins(0, 0, 0, 0);

    // 设置 QSplitter 的 handle 为蓝色分隔线
    m_horizontalSplitter->setHandleWidth(1);
    m_horizontalSplitter->setStyleSheet("QSplitter::handle { background-color: #0078D7; }");
    m_verticalSplitter->setHandleWidth(1);
    m_verticalSplitter->setStyleSheet("QSplitter::handle { background-color: #0078D7; }");

    setCentralWidget(m_verticalSplitter);
    resize(1200, 600);

    setWindowTitle("NTerminal");
}

void MainWindow::initMainWindowMenu()
{
    m_menuBar = new QMenuBar();

    m_fileMenu = m_menuBar->addMenu("File");
    m_editMenu = m_menuBar->addMenu("Edit");
    m_viewMenu = m_menuBar->addMenu("View");
    m_optMenu = m_menuBar->addMenu("Settings");
    m_transmitMenu = m_menuBar->addMenu("Transmit");
    m_scriptMenu = m_menuBar->addMenu("Script");
    m_helpMenu = m_menuBar->addMenu("Help");

    m_sessionAction = new QAction("Connect", this);
    m_fileMenu->addAction(m_sessionAction);
    
    m_saveSessionAction = new QAction("Save Current Session", this);
    connect(m_saveSessionAction, &QAction::triggered, this, &MainWindow::saveCurrentSession);
    m_fileMenu->addAction(m_saveSessionAction);
    
    m_manageSessionsAction = new QAction("Manage Sessions", this);
    connect(m_manageSessionsAction, &QAction::triggered, this, &MainWindow::manageSessions);
    m_fileMenu->addAction(m_manageSessionsAction);
    
    m_fileMenu->addSeparator();
    m_fileMenu->addAction("Print");
    m_fileMenu->addSeparator();
    m_exitAction = new QAction("Exit", this);
    m_fileMenu->addAction(m_exitAction);
    
    m_themeAction = new QAction("Theme Settings", this);
    connect(m_themeAction, &QAction::triggered, this, &MainWindow::showThemeSettings);
    m_optMenu->addAction(m_themeAction);

    QAction *terminalTestAction = new QAction("Terminal Compatibility Test", this);
    connect(terminalTestAction, &QAction::triggered, this, &MainWindow::openTerminalTester);
    m_optMenu->addAction(terminalTestAction);

    m_menuBar->addMenu(m_fileMenu);
    m_menuBar->addMenu(m_editMenu);
    m_menuBar->addMenu(m_viewMenu);
    m_menuBar->addMenu(m_optMenu);
    m_menuBar->addMenu(m_transmitMenu);
    m_menuBar->addMenu(m_scriptMenu);
    m_menuBar->addMenu(m_helpMenu);

    setMenuBar(m_menuBar);

    connect(m_sessionAction, &QAction::triggered, this, [this]() {
        ConnectionDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            ConnectionDialog::ConnectionSettings dialogSettings = dialog.getSettings();
            Common::ConnectionSettings settings = toCommonSettings(dialogSettings);
            
            QString tabName = settings.sessionName.isEmpty() ? 
                QString("Terminal %1").arg(m_terminals.size() + 1) : 
                settings.sessionName;
            
            TerminalProxy *terminal = createNewTerminalTab(tabName);
            if (terminal) {
                terminal->setConnectionSettings(settings);
                if (terminal->openConnection()) {
                    qDebug() << "Connection opened successfully";
                } else {
                    qDebug() << "Connection failed to open";
                }
            }
        }
    });

    m_toolBar = new QToolBar(this);
    m_toolbarSessionAction = new QAction("Connect", this);
    m_toolBar->addAction(m_toolbarSessionAction);
    m_toolBar->addAction("Print");
    m_debugAction = m_toolBar->addAction("Debug");
    addToolBar(Qt::TopToolBarArea, m_toolBar);
    
    connect(m_toolbarSessionAction, &QAction::triggered, this, [this]() {
        ConnectionDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            ConnectionDialog::ConnectionSettings dialogSettings = dialog.getSettings();
            Common::ConnectionSettings settings = toCommonSettings(dialogSettings);
            
            QString tabName = settings.sessionName.isEmpty() ? 
                QString("Terminal %1").arg(m_terminals.size() + 1) : 
                settings.sessionName;
            
            TerminalProxy *terminal = createNewTerminalTab(tabName);
            if (terminal) {
                terminal->setConnectionSettings(settings);
                applyTerminalTheme(terminal);
                if (terminal->openConnection()) {
                    qDebug() << "Connection opened successfully";
                } else {
                    qDebug() << "Connection failed to open";
                }
            }
        }
    });
}

void MainWindow::showThemeSettings()
{
    ThemeSettingsDialog dialog(this);
    dialog.exec();
    
    for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it) {
        applyTerminalTheme(it.value());
    }
}

void MainWindow::applyTerminalTheme(TerminalProxy *terminal)
{
    if (terminal) {
        TerminalTheme theme = ThemeManager::instance()->currentTerminalTheme();
        terminal->setTheme(theme);

        QFont font = FontManager::instance()->currentFont();
        terminal->setFont(font);
    }
}

void MainWindow::openTerminalTester()
{
    TestWindow *testWindow = new TestWindow(this);
    testWindow->setAttribute(Qt::WA_DeleteOnClose);

    // 传递当前终端给测试窗口
    TerminalProxy *currentTerminal = getCurrentTerminal();
    if (currentTerminal) {
        testWindow->setTerminal(currentTerminal);
    }

    testWindow->show();
    testWindow->raise();
    testWindow->activateWindow();
}
