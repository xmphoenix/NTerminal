#include "mainwindow.h"
#include "terminalwidget.h"
#include "connectiondialog.h"
#include "sessionmanager.h"
#include "logger.h"
#include <QSplitter>
#include <QListWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QMenuBar>
#include <QTimer>
#include <QMenu>
#include <QToolBar>
#include <QAction>
#include <QDebug>

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
    , m_logger(nullptr)
    , m_startLoggingAction(nullptr)
    , m_stopLoggingAction(nullptr)
    , m_toggleLoggingAction(nullptr)
{
    initMainWindowMenu();
    initMainWindowLayout();
    setupSessionManager();
    setupLogger();
}

void MainWindow::saveCurrentSession()
{
    TerminalWidget *terminal = getCurrentTerminal();
    if (!terminal || !terminal->isConnected()) {
        qDebug() << "没有活动的连接，无法保存会话";
        return;
    }
    
    ConnectionDialog::ConnectionSettings settings = terminal->getConnectionSettings();
    if (settings.sessionName.isEmpty()) {
        settings.sessionName = "未命名会话";
    }
    
    m_sessionManager->addSession(settings.sessionName, settings);
    m_sessionManager->saveSessions();
    qDebug() << "已保存会话:" << settings.sessionName;
}

void MainWindow::setupSessionManager()
{
    m_sessionManager = new SessionManager(this);
    if (!m_sessionManager->loadSessions()) {
        qWarning() << "无法加载会话配置";
    }
    
    connect(m_sessionManager, &SessionManager::sessionsLoaded,
            this, &MainWindow::populateSessionMenu);
    connect(m_sessionManager, &SessionManager::sessionAdded,
            this, &MainWindow::populateSessionMenu);
    connect(m_sessionManager, &SessionManager::sessionUpdated,
            this, &MainWindow::populateSessionMenu);
    connect(m_sessionManager, &SessionManager::sessionRemoved,
            this, &MainWindow::populateSessionMenu);
    
    // 初始加载后立即更新菜单
    QTimer::singleShot(0, this, &MainWindow::populateSessionMenu);
}

void MainWindow::setupLogger()
{
    m_logger = new Logger(this);
    
    // 连接日志信号
    connect(m_logger, &Logger::logMessage, this, [this](const QString &message) {
        qDebug() << "[LOG]" << message;
    });
    
    connect(m_logger, &Logger::errorOccurred, this, [this](const QString &error) {
        qWarning() << "日志错误:" << error;
    });
}

void MainWindow::startLogging()
{
    TerminalWidget *terminal = getCurrentTerminal();
    if (!terminal || !terminal->isConnected()) {
        qDebug() << "没有活动的连接，无法开始日志记录";
        return;
    }
    
    ConnectionDialog::ConnectionSettings settings = terminal->getConnectionSettings();
    QString sessionName = settings.sessionName.isEmpty() ? "未命名会话" : settings.sessionName;
    
    if (m_logger->startLogging(sessionName)) {
        // 连接终端信号到日志记录器
        connect(terminal, &TerminalWidget::dataReceived, m_logger, &Logger::onDataReceived);
        connect(terminal, &TerminalWidget::dataSent, m_logger, &Logger::onDataSent);
        connect(terminal, &TerminalWidget::connectionOpened, m_logger, &Logger::onConnectionOpened);
        connect(terminal, &TerminalWidget::connectionClosed, m_logger, &Logger::onConnectionClosed);
        connect(terminal, &TerminalWidget::connectionError, m_logger, &Logger::onConnectionError);
        
        qDebug() << "已开始记录日志:" << m_logger->getCurrentLogFile();
        if (m_toggleLoggingAction) m_toggleLoggingAction->setText("停止记录日志");
    }
}

void MainWindow::stopLogging()
{
    if (m_logger && m_logger->isLogging()) {
        // 断开所有终端信号
        for (TerminalWidget *terminal : m_terminals) {
            disconnect(terminal, &TerminalWidget::dataReceived, m_logger, &Logger::onDataReceived);
            disconnect(terminal, &TerminalWidget::dataSent, m_logger, &Logger::onDataSent);
            disconnect(terminal, &TerminalWidget::connectionOpened, m_logger, &Logger::onConnectionOpened);
            disconnect(terminal, &TerminalWidget::connectionClosed, m_logger, &Logger::onConnectionClosed);
            disconnect(terminal, &TerminalWidget::connectionError, m_logger, &Logger::onConnectionError);
        }
        
        m_logger->stopLogging();
        qDebug() << "已停止记录日志";
        if (m_toggleLoggingAction) m_toggleLoggingAction->setText("开始记录日志");
    }
}

void MainWindow::loadSession()
{
    // 实现会话加载功能
    qDebug() << "loadSession() - 待实现";
}

void MainWindow::manageSessions()
{
    // 实现会话管理对话框
    qDebug() << "manageSessions() - 待实现";
}

void MainWindow::populateSessionMenu()
{
    // 清除现有的会话菜单项（如果有）
    if (m_fileMenu) {
        // 查找并移除现有的"加载会话"子菜单
        QList<QAction*> actions = m_fileMenu->actions();
        for (QAction* action : actions) {
            if (action->text() == "加载会话") {
                m_fileMenu->removeAction(action);
                break;
            }
        }
    }
    
    if (!m_sessionManager || !m_fileMenu) {
        return;
    }
    
    // 获取会话列表
    QList<SessionManager::Session> sessions = m_sessionManager->getSessions();
    
    // 创建"加载会话"子菜单
    QMenu* loadSessionMenu = new QMenu("加载会话", m_fileMenu);
    
    // 添加会话项
    for (const SessionManager::Session& session : sessions) {
        QString displayName = QString("%1 (%2)").arg(session.name).arg(session.settings.sessionName);
        QAction* sessionAction = new QAction(displayName, this);
        sessionAction->setData(session.name);
        
        connect(sessionAction, &QAction::triggered, this, [this, session]() {
            // 加载会话设置
            ConnectionDialog::ConnectionSettings settings = session.settings;
            
            qDebug() << "加载会话:" << session.name;
            
            // 创建新的终端标签页
            QString tabName = session.name.isEmpty() ? 
                QString("终端 %1").arg(m_terminals.size() + 1) : 
                session.name;
            
            TerminalWidget *terminal = createNewTerminalTab(tabName);
            if (terminal) {
                terminal->setConnectionSettings(settings);
                if (terminal->openConnection()) {
                    qDebug() << "会话连接已打开";
                } else {
                    qDebug() << "会话连接打开失败";
                }
            }
        });
        
        loadSessionMenu->addAction(sessionAction);
    }
    
    // 如果没有会话，添加禁用项
    if (sessions.isEmpty()) {
        QAction* noSessionsAction = new QAction("无可用会话", this);
        noSessionsAction->setEnabled(false);
        loadSessionMenu->addAction(noSessionsAction);
    }
    
    // 将"加载会话"菜单添加到"文件"菜单中
    // 找到适当的位置插入（在"新建连接"之后）
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

void MainWindow::toggleLogging()
{
    if (m_logger && m_logger->isLogging()) {
        stopLogging();
    } else {
        startLogging();
    }
}

// 多终端管理方法实现

TerminalWidget* MainWindow::createNewTerminalTab(const QString &tabName)
{
    QString terminalId = QString("terminal_%1").arg(m_nextTerminalId++);
    QString displayName = tabName.isEmpty() ? QString("终端 %1").arg(m_terminals.size() + 1) : tabName;
    
    // 创建新的终端控件
    TerminalWidget *terminal = new TerminalWidget();
    
    // 添加到标签页
    int tabIndex = m_screenTabWidget->addTab(terminal, displayName);
    
    // 存储到映射中
    m_terminals.insert(terminalId, terminal);
    
    // 设置标签页数据以便查找
    m_screenTabWidget->tabBar()->setTabData(tabIndex, terminalId);
    
    // 设置为当前标签页
    m_screenTabWidget->setCurrentIndex(tabIndex);
    
    // 更新当前终端
    updateCurrentTerminal(terminal);
    
    qDebug() << "创建新终端标签页:" << displayName << "ID:" << terminalId;
    
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
    TerminalWidget *terminal = m_terminals.value(terminalId, nullptr);
    if (!terminal) {
        return;
    }
    
    // 找到对应的标签页索引
    int tabIndex = -1;
    for (int i = 0; i < m_screenTabWidget->count(); ++i) {
        QString tabTerminalId = m_screenTabWidget->tabBar()->tabData(i).toString();
        if (tabTerminalId == terminalId) {
            tabIndex = i;
            break;
        }
    }
    
    // 关闭连接（如果已连接）
    if (terminal->isConnected()) {
        terminal->closeConnection();
    }
    
    // 从映射中移除
    m_terminals.remove(terminalId);
    
    // 从标签页控件中移除（如果找到了索引）
    if (tabIndex >= 0) {
        m_screenTabWidget->removeTab(tabIndex);
    }
    
    // 删除控件
    terminal->deleteLater();
    
    qDebug() << "关闭终端标签页 ID:" << terminalId;
    
    // 如果所有标签页都关闭了，创建一个新的
    if (m_screenTabWidget->count() == 0) {
        createNewTerminalTab("终端 1");
    }
}

TerminalWidget* MainWindow::getCurrentTerminal() const
{
    return m_currentTerminal;
}

TerminalWidget* MainWindow::getTerminalById(const QString &terminalId) const
{
    return m_terminals.value(terminalId, nullptr);
}

void MainWindow::updateCurrentTerminal(TerminalWidget *terminal)
{
    if (m_currentTerminal != terminal) {
        m_currentTerminal = terminal;
        qDebug() << "当前活动终端已更新";
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
        TerminalWidget *terminal = getTerminalById(terminalId);
        if (terminal) {
            updateCurrentTerminal(terminal);
        }
    }
}

MainWindow::~MainWindow() 
{
    // 清理所有终端实例
    for (TerminalWidget *terminal : m_terminals) {
        if (terminal) {
            terminal->closeConnection();
            terminal->deleteLater();
        }
    }
    m_terminals.clear();
}

void MainWindow::initMainWindowLayout()
{
    m_verticalSplitter = new QSplitter(Qt::Vertical);
    m_horizontalSplitter = new QSplitter(Qt::Horizontal, m_verticalSplitter);

    m_listWidget = new QListWidget(m_horizontalSplitter);
    m_screenTabWidget = new QTabWidget(m_horizontalSplitter);
    m_horizontalSplitter->setStretchFactor(0, 1);
    m_horizontalSplitter->setStretchFactor(1, 180);

    // 设置标签页可关闭
    m_screenTabWidget->setTabsClosable(true);
    m_screenTabWidget->setMovable(true);
    
    // 连接标签页信号
    connect(m_screenTabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);
    connect(m_screenTabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::onTabCurrentChanged);

    // 创建第一个终端标签页
    createNewTerminalTab("终端 1");

    m_commandTextEdit = new QTextEdit(QObject::tr("手动输入命令区域"), m_verticalSplitter);

    m_verticalSplitter->setStretchFactor(0, 5);
    m_verticalSplitter->setStretchFactor(1, 1);

    setCentralWidget(m_verticalSplitter);
    resize(1200, 600);

    setWindowTitle("深海的小鱼儿开发--NTerminal");
}

void MainWindow::initMainWindowMenu()
{
    m_menuBar = new QMenuBar();

    m_fileMenu = m_menuBar->addMenu("文件");
    m_editMenu = m_menuBar->addMenu("编辑");
    m_viewMenu = m_menuBar->addMenu("视图");
    m_optMenu = m_menuBar->addMenu("设置");
    m_transmitMenu = m_menuBar->addMenu("发送");
    
    // 日志记录动作
    m_toggleLoggingAction = new QAction("开始记录日志", this);
    connect(m_toggleLoggingAction, &QAction::triggered, this, &MainWindow::toggleLogging);
    m_transmitMenu->addAction(m_toggleLoggingAction);
    
    m_scriptMenu = m_menuBar->addMenu("脚本");
    m_helpMenu = m_menuBar->addMenu("帮助");

    m_sessionAction = new QAction("新建连接", this);
    m_fileMenu->addAction(m_sessionAction);
    
    m_saveSessionAction = new QAction("保存当前会话", this);
    connect(m_saveSessionAction, &QAction::triggered, this, &MainWindow::saveCurrentSession);
    m_fileMenu->addAction(m_saveSessionAction);
    
    m_manageSessionsAction = new QAction("管理会话", this);
    connect(m_manageSessionsAction, &QAction::triggered, this, &MainWindow::manageSessions);
    m_fileMenu->addAction(m_manageSessionsAction);
    
    m_fileMenu->addSeparator();
    m_fileMenu->addAction("打印");
    m_fileMenu->addSeparator();
    m_exitAction = new QAction("退出", this);
    m_fileMenu->addAction(m_exitAction);

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
            ConnectionDialog::ConnectionSettings settings = dialog.getSettings();
            qDebug() << "新建连接: 会话名称 =" << settings.sessionName
                     << "类型 =" << settings.type;
            
            // 创建新的终端标签页
            QString tabName = settings.sessionName.isEmpty() ? 
                QString("终端 %1").arg(m_terminals.size() + 1) : 
                settings.sessionName;
            
            TerminalWidget *terminal = createNewTerminalTab(tabName);
            if (terminal) {
                terminal->setConnectionSettings(settings);
                if (terminal->openConnection()) {
                    qDebug() << "连接已成功打开";
                } else {
                    qDebug() << "连接打开失败";
                }
            }
        }
    });

    m_toolBar = new QToolBar(this);
    m_toolbarSessionAction = new QAction("新建连接", this);
    m_toolBar->addAction(m_toolbarSessionAction);
    m_toolBar->addAction("打印");
    m_debugAction = m_toolBar->addAction("Debug");
    addToolBar(Qt::TopToolBarArea, m_toolBar);
    
    connect(m_toolbarSessionAction, &QAction::triggered, this, [this]() {
        ConnectionDialog dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            ConnectionDialog::ConnectionSettings settings = dialog.getSettings();
            qDebug() << "工具栏新建连接: 会话名称 =" << settings.sessionName
                     << "类型 =" << settings.type;
            
            // 创建新的终端标签页
            QString tabName = settings.sessionName.isEmpty() ? 
                QString("终端 %1").arg(m_terminals.size() + 1) : 
                settings.sessionName;
            
            TerminalWidget *terminal = createNewTerminalTab(tabName);
            if (terminal) {
                terminal->setConnectionSettings(settings);
                if (terminal->openConnection()) {
                    qDebug() << "连接已成功打开";
                } else {
                    qDebug() << "连接打开失败";
                }
            }
        }
    });
}
