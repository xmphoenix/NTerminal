#include "ui/mainwindow.h"
#include "terminal/terminalproxy.h"
#include "ui/connectiondialog.h"
#include "ui/iconprovider.h"
#include "ui/sessionitemdelegate.h"
#include "ui/terminaltabbar.h"
#include "services/sessionmanager.h"
#include "theme/thememanager.h"
#include "theme/fontmanager.h"
#include "common/languagemanager.h"
#include "ui/themesettingsdialog.h"
#include "ui/logconfigdialog.h"
#include "ui/testwindow.h"
#include "ui/calculatordialog.h"
#include "ui/asciitabledialog.h"
#include "ui/portscannerdialog.h"
#include "ui/scriptdialog.h"
#include "ui/scriptlogviewer.h"
#include "ui/scriptcontrolpanel.h"
#include <QSplitter>
#include <QTreeWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QStatusBar>
#include <QMenuBar>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QShowEvent>
#include <QStyle>
#include <QActionGroup>
#include <QCoreApplication>
#include <QEvent>
#include <QDebug>
#include <QMessageBox>
#include <QClipboard>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_verticalSplitter(nullptr)
    , m_horizontalSplitter(nullptr)
    , m_topBar(nullptr)
    , m_sidebar(nullptr)
    , m_sessionTree(nullptr)
    , m_sessionSearch(nullptr)
    , m_quickProtocolCombo(nullptr)
    , m_quickConnectEdit(nullptr)
    , m_terminalTabBar(nullptr)
    , m_screenTabWidget(nullptr)
    , m_commandLineEdit(nullptr)
    , m_currentTerminal(nullptr)
    , m_nextTerminalId(1)
    , m_statusConnection(nullptr)
    , m_statusProtocol(nullptr)
    , m_statusEncoding(nullptr)
    , m_statusSize(nullptr)
    , m_statusLatency(nullptr)
    , m_menuBar(nullptr)
    , m_fileMenu(nullptr)
    , m_editMenu(nullptr)
    , m_viewMenu(nullptr)
    , m_optMenu(nullptr)
    , m_transmitMenu(nullptr)
    , m_scriptMenu(nullptr)
    , m_toolsMenu(nullptr)
    , m_pluginMenu(nullptr)
    , m_helpMenu(nullptr)
    , m_sessionAction(nullptr)
    , m_exitAction(nullptr)
    , m_sessionManager(nullptr)
    , m_saveSessionAction(nullptr)
    , m_loadSessionAction(nullptr)
    , m_manageSessionsAction(nullptr)
    , m_loadScriptAction(nullptr)
    , m_stopScriptAction(nullptr)
    , m_scriptLogAction(nullptr)
    , m_scriptControlAction(nullptr)
    , m_loadSessionMenu(nullptr)
{
    initMainWindowMenu();
    initMainWindowLayout();
    setupSessionManager();

    // 应用主题切换：刷新图标着色与状态栏颜色（QSS 由 ThemeManager 统一更新）
    connect(ThemeManager::instance(), &ThemeManager::appThemeChanged,
            this, [this](const AppTheme &) {
        refreshIcons();
        updateStatusBar();
        updateTabStates();
    });
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

    m_sessionManager->addSession(settings.sessionName, settings);
    m_sessionManager->saveSessions();
    qDebug() << "Session saved:" << settings.sessionName;
}

void MainWindow::registerSession(const Common::ConnectionSettings &settings)
{
    // 新建会话/快速连接不经过“保存会话”菜单，若不在打开前登记，
    // 连接后侧栏会话管理器和“会话”菜单里都不会出现这条会话。
    if (settings.sessionName.isEmpty()) {
        return;
    }

    m_sessionManager->addSession(settings.sessionName, settings);
    m_sessionManager->saveSessions();
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

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    Q_UNUSED(event);
    // 命令发送由 QLineEdit::returnPressed 处理（见 buildTerminalArea）
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::populateSessionMenu()
{
    if (!m_loadSessionMenu || !m_sessionManager) {
        return;
    }

    // clear() 会删除菜单内所有 action（含其子对象），重建内容而不累积泄漏
    m_loadSessionMenu->clear();

    QList<SessionManager::Session> sessions = m_sessionManager->getSessions();

    if (sessions.isEmpty()) {
        QAction *noSessionsAction = m_loadSessionMenu->addAction(tr("暂无已保存会话"));
        noSessionsAction->setEnabled(false);
        populateSessionTree();
        return;
    }

    for (const SessionManager::Session& session : sessions) {
        QString displayName = QString("%1 (%2)").arg(session.name).arg(session.settings.sessionName);
        QAction* sessionAction = new QAction(displayName, m_loadSessionMenu);
        sessionAction->setData(session.name);

        connect(sessionAction, &QAction::triggered, this, [this, session]() {
            Common::ConnectionSettings settings = session.settings;

            qDebug() << "Loading session:" << session.name;

            QString tabName = session.name.isEmpty() ?
                tr("终端 %1").arg(m_terminals.size() + 1) :
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

        m_loadSessionMenu->addAction(sessionAction);
    }

    // 同步侧栏会话树
    populateSessionTree();
}

TerminalProxy* MainWindow::createNewTerminalTab(const QString &tabName)
{
    QString terminalId = QString("terminal_%1").arg(m_nextTerminalId++);
    QString displayName = tabName.isEmpty() ? tr("终端 %1").arg(m_terminals.size() + 1) : tabName;
    
    TerminalProxy *terminal = new TerminalProxy();

    // 注入当前主题/字体（TerminalProxy 构造不访问单例，由创建方负责注入）
    applyTerminalTheme(terminal);

    // 连接状态变化 -> 标签状态点 + 状态栏
    connect(terminal, &TerminalProxy::connectionOpened, this, [this]() {
        updateTabStates();
        updateStatusBar();
    });
    connect(terminal, &TerminalProxy::connectionClosed, this, [this]() {
        updateTabStates();
        updateStatusBar();
    });
    connect(terminal, &TerminalProxy::connectionError, this, [this](const QString &error) {
        Q_UNUSED(error);
        updateTabStates();
        updateStatusBar();
    });

    int tabIndex = m_screenTabWidget->addTab(terminal, displayName);
    
    m_terminals.insert(terminalId, terminal);
    
    m_screenTabWidget->tabBar()->setTabData(tabIndex, terminalId);
    
    m_screenTabWidget->setCurrentIndex(tabIndex);
    updateEmptyState();

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
        updateEmptyState();
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
    updateStatusBar();
    updateTabStates();
}

// ============================================================================
// GUI 重设计：侧栏会话树 / 快速连接 / 状态栏
// ============================================================================

namespace {

// QTabWidget::setTabBar 为 protected，用最小子类暴露以安装自绘标签栏
class TerminalTabWidget : public QTabWidget
{
public:
    using QTabWidget::QTabWidget;
    void installTabBar(QTabBar *bar) { setTabBar(bar); }
};

// 普通 QWidget 默认不绘制样式表背景，必须开启 WA_StyledBackground，
// 否则 QSS 中的 #topBar/#sidebar/#commandBar 背景色不会生效。
QWidget *styledPanel(const QString &objectName)
{
    QWidget *w = new QWidget;
    w->setObjectName(objectName);
    w->setAttribute(Qt::WA_StyledBackground, true);
    return w;
}

QString protocolName(Common::ConnectionType type)
{
    switch (type) {
    case Common::ConnectionType::Serial: return MainWindow::tr("串口");
    case Common::ConnectionType::Telnet: return QStringLiteral("Telnet");
    case Common::ConnectionType::SSH:    return QStringLiteral("SSH");
    case Common::ConnectionType::Rlogin: return QStringLiteral("Rlogin");
    case Common::ConnectionType::SFTP:   return QStringLiteral("SFTP");
    case Common::ConnectionType::FTP:    return QStringLiteral("FTP");
    case Common::ConnectionType::TFTP:   return QStringLiteral("TFTP");
    }
    return MainWindow::tr("未知");
}

// 会话副文本：网络类 "user@host:port"，串口 "COMx · baud 8N1"
QString sessionSubtitle(const Common::ConnectionSettings &s)
{
    if (s.type == Common::ConnectionType::Serial) {
        QString frame = QString::number(s.baudRate);
        switch (s.dataBits) {
        case QSerialPort::Data5: frame += QStringLiteral(" 5"); break;
        case QSerialPort::Data6: frame += QStringLiteral(" 6"); break;
        case QSerialPort::Data7: frame += QStringLiteral(" 7"); break;
        default: frame += QStringLiteral(" 8"); break;
        }
        switch (s.parity) {
        case QSerialPort::EvenParity: frame += QLatin1Char('E'); break;
        case QSerialPort::OddParity: frame += QLatin1Char('O'); break;
        case QSerialPort::MarkParity: frame += QLatin1Char('M'); break;
        case QSerialPort::SpaceParity: frame += QLatin1Char('S'); break;
        default: frame += QLatin1Char('N'); break;
        }
        frame += s.stopBits == QSerialPort::TwoStop ? QStringLiteral("2")
                                                    : QStringLiteral("1");
        return QStringLiteral("%1 · %2").arg(s.portName.isEmpty() ? QStringLiteral("--")
                                                                  : s.portName, frame);
    }
    const QString user = s.username.isEmpty() ? QStringLiteral("--") : s.username;
    return QStringLiteral("%1@%2:%3").arg(user, s.host).arg(s.port);
}

int defaultPort(Common::ConnectionType type)
{
    switch (type) {
    case Common::ConnectionType::Telnet: return 23;
    case Common::ConnectionType::SSH:    return 22;
    case Common::ConnectionType::Rlogin: return 513;
    case Common::ConnectionType::SFTP:   return 22;
    case Common::ConnectionType::FTP:    return 21;
    case Common::ConnectionType::TFTP:   return 69;
    default: return 23;
    }
}

} // namespace

void MainWindow::populateSessionTree()
{
    if (!m_sessionTree || !m_sessionManager) {
        return;
    }

    m_sessionTree->clear();

    const QList<SessionManager::Session> sessions = m_sessionManager->getSessions();
    if (sessions.isEmpty()) {
        QTreeWidgetItem *empty = new QTreeWidgetItem(m_sessionTree);
        empty->setText(0, tr("暂无已保存会话"));
        empty->setData(0, SessionItemDelegate::SessionGroupRole, true);
        empty->setFlags(Qt::NoItemFlags);
        return;
    }

    // 按协议分组（固定顺序，空组不显示）
    static const Common::ConnectionType kGroupOrder[] = {
        Common::ConnectionType::SSH, Common::ConnectionType::SFTP,
        Common::ConnectionType::Telnet, Common::ConnectionType::Rlogin,
        Common::ConnectionType::Serial, Common::ConnectionType::FTP,
        Common::ConnectionType::TFTP,
    };

    QMap<int, QTreeWidgetItem *> groups;
    QMap<int, int> counts;

    for (const SessionManager::Session &session : sessions) {
        const int typeKey = static_cast<int>(session.settings.type);
        QTreeWidgetItem *group = groups.value(typeKey, nullptr);
        if (!group) {
            // 先创建游离 item（不挂到树上）：末尾 clear() 会释放已挂载的 item，
            // 若此处直接以 m_sessionTree 为父，ordered 里将留下悬空指针。
            group = new QTreeWidgetItem();
            group->setText(0, protocolName(session.settings.type));
            group->setData(0, SessionItemDelegate::SessionGroupRole, true);
            group->setFlags(Qt::ItemIsEnabled);
            groups.insert(typeKey, group);
        }
        counts[typeKey] = counts.value(typeKey) + 1;

        const QString displayName = session.name.isEmpty() ? session.settings.sessionName
                                                           : session.name;
        QTreeWidgetItem *item = new QTreeWidgetItem(group);
        item->setText(0, displayName);
        item->setData(0, SessionItemDelegate::SessionNameRole, session.name);
        item->setData(0, SessionItemDelegate::SessionTypeRole, typeKey);
        item->setData(0, SessionItemDelegate::SessionHostRole,
                      sessionSubtitle(session.settings));
        item->setData(0, SessionItemDelegate::SessionActiveRole, false);
        item->setToolTip(0, QStringLiteral("%1 · %2").arg(session.name,
                                                          sessionSubtitle(session.settings)));
    }

    // 按固定顺序重排分组 + 写入数量
    QList<QTreeWidgetItem *> ordered;
    for (Common::ConnectionType type : kGroupOrder) {
        const int key = static_cast<int>(type);
        if (QTreeWidgetItem *group = groups.value(key, nullptr)) {
            group->setData(0, Qt::UserRole + 10, counts.value(key));
            ordered.append(group);
            groups.remove(key);
        }
    }
    // 其余类型（理论上不会出现）
    for (QTreeWidgetItem *group : groups) {
        ordered.append(group);
    }
    m_sessionTree->clear();
    m_sessionTree->addTopLevelItems(ordered);
    m_sessionTree->expandAll();
}

void MainWindow::onSessionTreeActivated(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item || item->data(0, SessionItemDelegate::SessionGroupRole).toBool()) {
        return;
    }
    const QString sessionName = item->data(0, SessionItemDelegate::SessionNameRole).toString();
    if (sessionName.isEmpty()) {
        return;
    }
    for (const SessionManager::Session &session : m_sessionManager->getSessions()) {
        if (session.name == sessionName) {
            openSession(session.settings, session.name);
            return;
        }
    }
}

void MainWindow::onQuickConnect()
{
    const QString input = m_quickConnectEdit->text().trimmed();
    if (input.isEmpty()) {
        return;
    }

    static const Common::ConnectionType kQuickTypes[] = {
        Common::ConnectionType::SSH, Common::ConnectionType::Telnet,
        Common::ConnectionType::Rlogin, Common::ConnectionType::Serial,
        Common::ConnectionType::SFTP, Common::ConnectionType::FTP,
        Common::ConnectionType::TFTP,
    };
    const int index = qBound(0, m_quickProtocolCombo->currentIndex(), 6);

    Common::ConnectionSettings settings;
    settings.type = kQuickTypes[index];

    if (settings.type == Common::ConnectionType::Serial) {
        settings.portName = input;
        settings.sessionName = input;
    } else {
        QString rest = input;
        QString user;
        const int at = rest.indexOf(QLatin1Char('@'));
        if (at > 0) {
            user = rest.left(at);
            rest = rest.mid(at + 1);
        }
        QString host = rest;
        int port = 0;
        const int colon = rest.lastIndexOf(QLatin1Char(':'));
        if (colon > 0) {
            port = rest.mid(colon + 1).toInt();
            host = rest.left(colon);
        }
        settings.host = host;
        settings.username = user;
        settings.port = port > 0 ? port : defaultPort(settings.type);
        settings.sessionName = host.isEmpty() ? tr("快速连接") : host;
    }

    // 快速连接同样登记到会话管理器，避免“连上了但在侧栏里找不到”的情况
    registerSession(settings);
    openSession(settings, settings.sessionName);
    m_quickConnectEdit->clear();
}

void MainWindow::filterSessionTree(const QString &text)
{
    if (!m_sessionTree) {
        return;
    }
    const QString needle = text.trimmed();
    for (int i = 0; i < m_sessionTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_sessionTree->topLevelItem(i);
        if (group->data(0, SessionItemDelegate::SessionGroupRole).toBool()) {
            int visible = 0;
            for (int j = 0; j < group->childCount(); ++j) {
                QTreeWidgetItem *child = group->child(j);
                const bool match = needle.isEmpty()
                    || child->text(0).contains(needle, Qt::CaseInsensitive)
                    || child->data(0, SessionItemDelegate::SessionHostRole).toString()
                           .contains(needle, Qt::CaseInsensitive);
                child->setHidden(!match);
                if (match) {
                    ++visible;
                }
            }
            group->setHidden(visible == 0 && !needle.isEmpty());
            if (!needle.isEmpty() && visible > 0) {
                group->setExpanded(true);
            }
        }
    }
}

void MainWindow::openSession(const Common::ConnectionSettings &settings, const QString &name)
{
    const QString tabName = name.isEmpty() ? tr("Terminal %1").arg(m_terminals.size() + 1)
                                           : name;
    TerminalProxy *terminal = createNewTerminalTab(tabName);
    if (!terminal) {
        return;
    }
    terminal->setConnectionSettings(settings);
    applyTerminalTheme(terminal);
    if (terminal->openConnection()) {
        qDebug() << "Session connection opened:" << tabName;
    } else {
        qDebug() << "Session connection failed:" << tabName;
    }
    updateStatusBar();
}

void MainWindow::updateTabStates()
{
    if (m_terminalTabBar) {
        m_terminalTabBar->refreshStates();
    }
}

void MainWindow::updateStatusBar()
{
    if (!m_statusConnection) {
        return;
    }
    const AppTheme theme = ThemeManager::instance()->currentAppTheme();
    TerminalProxy *terminal = getCurrentTerminal();
    const bool connected = terminal && terminal->isConnected();

    m_statusConnection->setText(connected ? tr("● 已连接") : tr("○ 未连接"));
    // 用调色板而非 setStyleSheet：避免运行时样式重算（QSS 已统一控制外观）
    QPalette connPalette = m_statusConnection->palette();
    connPalette.setColor(QPalette::WindowText,
                         connected ? theme.success : theme.textFaint);
    m_statusConnection->setPalette(connPalette);

    if (terminal) {
        const Common::ConnectionSettings settings = terminal->getConnectionSettings();
        m_statusProtocol->setText(QStringLiteral("%1 · %2")
                                      .arg(protocolName(settings.type),
                                           sessionSubtitle(settings)));
        m_statusSize->setText(QStringLiteral("%1 × %2")
                                  .arg(terminal->columns())
                                  .arg(terminal->rows()));
    } else {
        m_statusProtocol->clear();
        m_statusSize->clear();
    }
    m_statusEncoding->setText(QStringLiteral("UTF-8"));
    m_statusLatency->setText(connected ? QStringLiteral("-- ms")
                                       : QStringLiteral("-- ms"));

    // 空的状态项直接隐藏：QSS 给状态栏标签画了分隔线，
    // 留着空标签会出现没有内容的分隔框。
    m_statusProtocol->setVisible(!m_statusProtocol->text().isEmpty());
    m_statusSize->setVisible(!m_statusSize->text().isEmpty());
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

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_geometryAdjusted) {
        return;
    }
    m_geometryAdjusted = true;

    // showEvent 触发时 Windows 还没把真实标题栏应用到窗口上，frameMargins() 为 0。
    // 延后一个事件循环：窗口已映射，再按"工作区 - 窗口边框"算出客户区尺寸并居中。
    // 这样 720p / 150% 缩放下整窗也不会超出屏幕，状态栏不会被任务栏遮住。
    QTimer::singleShot(0, this, [this] {
        const QRect available = screen()->availableGeometry();
        const QMargins margins = windowHandle() ? windowHandle()->frameMargins()
                                                : QMargins();
        resize(qMin(1280, available.width() - margins.left() - margins.right()),
               qMin(800, available.height() - margins.top() - margins.bottom()));
        // QWidget::move() 定位的是含标题栏的整窗左上角，直接按窗口外框居中；
        // 整窗恰好铺满工作区时居中会算出 -1 偏移，夹到工作区左上角避免压住任务栏
        const QRect frame = frameGeometry();
        QPoint pos = available.center() - QPoint(frame.width() / 2, frame.height() / 2);
        pos.setX(qMax(pos.x(), available.left()));
        pos.setY(qMax(pos.y(), available.top()));
        move(pos);
    });
}

void MainWindow::bindText(std::function<void()> apply)
{
    m_textBindings.append(std::move(apply));
    m_textBindings.last()();
}

void MainWindow::retranslateUi()
{
    for (const auto &apply : m_textBindings) {
        apply();
    }
    updateStatusBar();
    // 会话树分组名、空树提示由 populateSessionTree() 生成，重建以套用新语言
    populateSessionTree();
    if (m_sessionSearch && !m_sessionSearch->text().isEmpty()) {
        filterSessionTree(m_sessionSearch->text());
    }
    populateSessionMenu();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::initMainWindowLayout()
{
    // 中央布局：顶栏（工具栏）+ 主体（侧栏 | 终端区）
    QWidget *central = new QWidget(this);
    QVBoxLayout *outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_topBar = buildTopBar();
    outer->addWidget(m_topBar);

    m_horizontalSplitter = new QSplitter(Qt::Horizontal, central);
    // 分隔条留出抓取宽度，并绘制 1px 分界线（QSS 中 #QSplitter::handle）
    m_horizontalSplitter->setHandleWidth(6);
    m_horizontalSplitter->setChildrenCollapsible(false);
    m_horizontalSplitter->addWidget(buildSidebar());
    m_horizontalSplitter->addWidget(buildTerminalArea());
    m_horizontalSplitter->setStretchFactor(0, 0);
    m_horizontalSplitter->setStretchFactor(1, 1);
    m_horizontalSplitter->setSizes({248, 1000});
    outer->addWidget(m_horizontalSplitter, 1);

    setCentralWidget(central);
    buildStatusBarWidgets();

    // 初始尺寸按可用屏幕区域自适应：期望 1280×800，但不超过工作区。
    // 构造期拿不到真实标题栏尺寸，这里用样式里的标题栏高度估算；
    // 精确校正放在 showEvent()，避免首帧闪过一个超出屏幕的大窗口。
    const QRect available = screen()->availableGeometry();
    const int titleBar = style()->pixelMetric(QStyle::PM_TitleBarHeight, nullptr, this);
    const QSize preferred(qMin(1280, available.width()),
                          qMin(800, available.height() - titleBar));
    setMinimumSize(qMin(960, preferred.width()), qMin(600, preferred.height()));
    resize(preferred);
    move(available.center()
         - QPoint(preferred.width() / 2, (preferred.height() + titleBar) / 2));
    setWindowTitle("NTerminal");
}

QWidget *MainWindow::buildTopBar()
{
    QWidget *bar = styledPanel("topBar");
    QHBoxLayout *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(12, 8, 10, 8);
    lay->setSpacing(8);

    // 主操作：新建会话（实心强调色按钮）
    QPushButton *newBtn = new QPushButton;
    bindText([newBtn] { newBtn->setText(tr("新建会话")); });
    newBtn->setProperty("class", "primary");
    newBtn->setProperty("iconName", "new-session");
    newBtn->setProperty("iconOnAccent", true);
    newBtn->setIcon(IconProvider::icon("new-session",
                                       ThemeManager::instance()->currentAppTheme().highlightText));
    newBtn->setCursor(Qt::PointingHandCursor);
    newBtn->setMinimumHeight(34);
    connect(newBtn, &QPushButton::clicked, this, [this]() {
        openConnectionFromDialog(true);
    });
    lay->addWidget(newBtn);

    // 快速连接：协议 + 地址 + 连接，合并成一个圆角输入条（Google 搜索框式）
    QFrame *quickBar = new QFrame;
    quickBar->setObjectName("quickConnectBar");
    quickBar->setAttribute(Qt::WA_StyledBackground, true);
    quickBar->setMaximumWidth(520);
    QHBoxLayout *quickLay = new QHBoxLayout(quickBar);
    quickLay->setContentsMargins(4, 2, 4, 2);
    quickLay->setSpacing(2);

    m_quickProtocolCombo = new QComboBox;
    // 协议名按语言重设（选项按索引取协议，重建时保留当前索引）
    bindText([this] {
        const int index = m_quickProtocolCombo->currentIndex();
        m_quickProtocolCombo->clear();
        m_quickProtocolCombo->addItems({ QStringLiteral("SSH"), QStringLiteral("Telnet"),
                                         QStringLiteral("Rlogin"), tr("串口"),
                                         QStringLiteral("SFTP"), QStringLiteral("FTP"),
                                         QStringLiteral("TFTP") });
        m_quickProtocolCombo->setCurrentIndex(index < 0 ? 0 : index);
    });
    m_quickProtocolCombo->setFixedWidth(88);
    m_quickProtocolCombo->setCursor(Qt::PointingHandCursor);

    m_quickConnectEdit = new QLineEdit;
    bindText([this] { m_quickConnectEdit->setPlaceholderText(tr("user@host:port 快速连接")); });
    m_quickConnectEdit->setMinimumWidth(190);

    QPushButton *goBtn = new QPushButton;
    bindText([goBtn] { goBtn->setText(tr("连接")); });
    goBtn->setProperty("class", "tonal");
    goBtn->setCursor(Qt::PointingHandCursor);
    goBtn->setFixedHeight(30);
    connect(goBtn, &QPushButton::clicked, this, &MainWindow::onQuickConnect);
    connect(m_quickConnectEdit, &QLineEdit::returnPressed, this, &MainWindow::onQuickConnect);

    quickLay->addWidget(m_quickProtocolCombo);
    quickLay->addWidget(m_quickConnectEdit, 1);
    quickLay->addWidget(goBtn);
    lay->addWidget(quickBar);

    auto addSeparator = [&lay]() {
        QFrame *line = new QFrame;
        line->setFrameShape(QFrame::VLine);
        line->setObjectName("toolbarSep");
        line->setFixedWidth(1);
        lay->addSpacing(2);
        lay->addWidget(line);
        lay->addSpacing(2);
    };

    addSeparator();

    // 会话操作图标组（提示文案延迟到语言切换时重新求值）
    auto addIconButton = [&](const QString &icon, std::function<QString()> tip,
                             const std::function<void()> &handler) {
        QToolButton *button = new QToolButton;
        button->setProperty("iconName", icon);
        button->setIcon(IconProvider::themedIcon(icon));
        bindText([button, tip] { button->setToolTip(tip()); });
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumSize(32, 32);
        connect(button, &QToolButton::clicked, this, handler);
        lay->addWidget(button);
        return button;
    };

    addIconButton("disconnect", [] { return tr("断开当前会话"); }, [this]() {
        if (TerminalProxy *t = getCurrentTerminal()) {
            t->closeConnection();
        }
    });
    addIconButton("reconnect", [] { return tr("重新连接"); }, [this]() {
        if (TerminalProxy *t = getCurrentTerminal()) {
            t->closeConnection();
            t->openConnection();
        }
    });
    addIconButton("duplicate", [] { return tr("复制会话"); }, [this]() {
        duplicateSession();
    });

    addSeparator();

    // 工具入口
    addIconButton("script", [] { return tr("脚本"); }, [this]() { showScriptControlPanel(); });
    addIconButton("tools", [] { return tr("工具"); }, [this]() { showPortScanner(); });

    lay->addStretch(1);

    addIconButton("theme", [] { return tr("主题设置"); }, [this]() { showThemeSettings(); });
    addIconButton("settings", [] { return tr("日志设置"); }, [this]() { showLogSettings(); });

    return bar;
}

QWidget *MainWindow::buildSidebar()
{
    m_sidebar = styledPanel("sidebar");
    m_sidebar->setMinimumWidth(216);
    QVBoxLayout *lay = new QVBoxLayout(m_sidebar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // 标题行
    QWidget *head = styledPanel("sidebarHead");
    QHBoxLayout *headLay = new QHBoxLayout(head);
    headLay->setContentsMargins(14, 8, 8, 7);
    headLay->setSpacing(6);
    QLabel *title = new QLabel;
    title->setObjectName("sidebarTitle");
    bindText([title] { title->setText(tr("会话管理器")); });
    QToolButton *headAdd = new QToolButton;
    headAdd->setIcon(IconProvider::themedIcon("new-session"));
    bindText([headAdd] { headAdd->setToolTip(tr("新建连接")); });
    headAdd->setAutoRaise(true);
    headAdd->setCursor(Qt::PointingHandCursor);
    headAdd->setMinimumSize(28, 28);
    connect(headAdd, &QToolButton::clicked, this, [this]() {
        openConnectionFromDialog(true);
    });
    headLay->addWidget(title);
    headLay->addStretch(1);
    headLay->addWidget(headAdd);
    lay->addWidget(head);

    // 搜索框（前置放大镜）
    QWidget *searchWrap = new QWidget;
    searchWrap->setObjectName("sidebarSearchWrap");
    QHBoxLayout *searchLay = new QHBoxLayout(searchWrap);
    searchLay->setContentsMargins(12, 4, 12, 10);
    m_sessionSearch = new QLineEdit;
    m_sessionSearch->setObjectName("sidebarSearch");
    bindText([this] { m_sessionSearch->setPlaceholderText(tr("搜索会话")); });
    m_sessionSearch->setClearButtonEnabled(true);
    m_sessionSearch->addAction(IconProvider::themedIcon("search"), QLineEdit::LeadingPosition);
    connect(m_sessionSearch, &QLineEdit::textChanged, this, &MainWindow::filterSessionTree);
    searchLay->addWidget(m_sessionSearch);
    lay->addWidget(searchWrap);

    // 会话树
    m_sessionTree = new QTreeWidget;
    m_sessionTree->setHeaderHidden(true);
    m_sessionTree->setIndentation(0);
    m_sessionTree->setRootIsDecorated(false);
    m_sessionTree->setUniformRowHeights(false);
    m_sessionTree->setFrameShape(QFrame::NoFrame);
    m_sessionTree->setMouseTracking(true);
    m_sessionTree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_sessionTree->setItemDelegate(new SessionItemDelegate(m_sessionTree));
    // activated 同时覆盖鼠标双击与键盘回车；再连 itemDoubleClicked 会一次打开两个标签
    connect(m_sessionTree, &QTreeWidget::itemActivated, this, &MainWindow::onSessionTreeActivated);
    lay->addWidget(m_sessionTree, 1);

    // 底部：新建连接（柔和色调，与顶栏主按钮区分层级）
    QWidget *foot = styledPanel("sidebarFoot");
    QVBoxLayout *footLay = new QVBoxLayout(foot);
    footLay->setContentsMargins(12, 8, 12, 10);
    QPushButton *newBtn = new QPushButton;
    bindText([newBtn] { newBtn->setText(tr("新建连接")); });
    newBtn->setProperty("class", "tonal");
    newBtn->setProperty("iconName", "new-session");
    newBtn->setIcon(IconProvider::themedIcon("new-session"));
    newBtn->setCursor(Qt::PointingHandCursor);
    newBtn->setMinimumHeight(34);
    connect(newBtn, &QPushButton::clicked, this, [this]() {
        openConnectionFromDialog(true);
    });
    footLay->addWidget(newBtn);
    lay->addWidget(foot);

    return m_sidebar;
}

QWidget *MainWindow::buildTerminalArea()
{
    QWidget *area = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(area);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *tabWidget = new TerminalTabWidget;
    // 标签条右侧的空白也由 QSS 底色铺满（普通 QWidget 需显式开启样式背景）
    tabWidget->setAttribute(Qt::WA_StyledBackground, true);
    m_screenTabWidget = tabWidget;
    m_screenTabWidget->setDocumentMode(true);
    m_screenTabWidget->setTabsClosable(true);
    m_screenTabWidget->setMovable(true);

    m_terminalTabBar = new TerminalTabBar(m_screenTabWidget);
    m_terminalTabBar->setStateProvider([this](const QString &tabId) {
        TerminalProxy *terminal = m_terminals.value(tabId, nullptr);
        return terminal && terminal->isConnected();
    });
    tabWidget->installTabBar(m_terminalTabBar);

    connect(m_screenTabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);
    connect(m_screenTabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::onTabCurrentChanged);
    connect(m_terminalTabBar, &QTabBar::customContextMenuRequested,
            this, &MainWindow::onTabContextMenu);

    m_mainStack = new QStackedWidget;
    m_emptyState = buildEmptyState();
    m_mainStack->addWidget(m_emptyState);
    m_mainStack->addWidget(tabWidget);
    m_mainStack->setCurrentIndex(0);
    lay->addWidget(m_mainStack, 1);

    // 命令输入栏（悬浮圆角输入框 + 发送按钮）
    QWidget *cmdBar = styledPanel("commandBar");
    QHBoxLayout *cmdLay = new QHBoxLayout(cmdBar);
    cmdLay->setContentsMargins(12, 8, 12, 10);
    cmdLay->setSpacing(8);

    QLabel *prompt = new QLabel(QStringLiteral(">"));
    prompt->setObjectName("commandPrompt");
    cmdLay->addWidget(prompt);

    m_commandLineEdit = new QLineEdit;
    m_commandLineEdit->setObjectName("commandLine");
    bindText([this] { m_commandLineEdit->setPlaceholderText(tr("输入命令，Enter 发送")); });
    connect(m_commandLineEdit, &QLineEdit::returnPressed, this, [this]() {
        const QString command = m_commandLineEdit->text().trimmed();
        if (command.isEmpty()) {
            return;
        }
        TerminalProxy *terminal = getCurrentTerminal();
        if (terminal) {
            terminal->sendText(command + QStringLiteral("\n"));
        } else {
            QMessageBox::information(this, tr("提示"), tr("请先打开一个终端连接"));
        }
        m_commandLineEdit->clear();
    });
    cmdLay->addWidget(m_commandLineEdit, 1);

    QToolButton *sendBtn = new QToolButton;
    sendBtn->setProperty("iconName", "send");
    sendBtn->setIcon(IconProvider::themedIcon("send"));
    bindText([sendBtn] { sendBtn->setToolTip(tr("发送")); });
    sendBtn->setAutoRaise(true);
    sendBtn->setCursor(Qt::PointingHandCursor);
    sendBtn->setMinimumSize(34, 34);
    connect(sendBtn, &QToolButton::clicked, this, [this]() {
        emit m_commandLineEdit->returnPressed();
    });
    cmdLay->addWidget(sendBtn);

    lay->addWidget(cmdBar);

    return area;
}

QWidget *MainWindow::buildEmptyState()
{
    QWidget *page = new QWidget;
    page->setObjectName("emptyState");

    QVBoxLayout *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 24, 24, 24);
    outer->addStretch();

    // 居中卡片：图标 + 标题 + 说明 + 两个入口按钮
    QWidget *card = new QWidget;
    card->setObjectName("emptyStateCard");
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setMaximumWidth(420);

    QVBoxLayout *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(32, 32, 32, 28);
    cardLay->setSpacing(12);

    QLabel *iconChip = new QLabel;
    iconChip->setObjectName("emptyStateIcon");
    iconChip->setAlignment(Qt::AlignCenter);
    iconChip->setFixedSize(64, 64);
    iconChip->setPixmap(IconProvider::pixmap("terminal",
                                             ThemeManager::instance()->currentAppTheme().accentText,
                                             30, devicePixelRatioF()));
    cardLay->addWidget(iconChip, 0, Qt::AlignHCenter);

    QLabel *title = new QLabel;
    title->setObjectName("emptyStateTitle");
    title->setAlignment(Qt::AlignCenter);
    bindText([title] { title->setText(tr("还没有打开的会话")); });
    cardLay->addWidget(title);

    QLabel *text = new QLabel;
    text->setObjectName("emptyStateText");
    text->setAlignment(Qt::AlignCenter);
    text->setWordWrap(true);
    bindText([text] { text->setText(tr("在左侧选择一个已保存的会话，\n或新建一个连接开始工作。")); });
    cardLay->addWidget(text);

    QHBoxLayout *actions = new QHBoxLayout;
    actions->setSpacing(8);
    actions->addStretch();

    QPushButton *newBtn = new QPushButton;
    newBtn->setProperty("class", "primary");
    newBtn->setProperty("iconName", "new-session");
    newBtn->setProperty("iconOnAccent", true);
    newBtn->setIcon(IconProvider::icon("new-session",
                                       ThemeManager::instance()->currentAppTheme().highlightText));
    newBtn->setCursor(Qt::PointingHandCursor);
    newBtn->setMinimumHeight(34);
    bindText([newBtn] { newBtn->setText(tr("新建会话")); });
    connect(newBtn, &QPushButton::clicked, this, [this]() { openConnectionFromDialog(true); });
    actions->addWidget(newBtn);

    QPushButton *quickBtn = new QPushButton;
    quickBtn->setProperty("class", "tonal");
    quickBtn->setCursor(Qt::PointingHandCursor);
    quickBtn->setMinimumHeight(34);
    bindText([quickBtn] { quickBtn->setText(tr("快速连接")); });
    connect(quickBtn, &QPushButton::clicked, this, [this]() {
        if (m_quickConnectEdit) {
            m_quickConnectEdit->setFocus();
            m_quickConnectEdit->selectAll();
        }
    });
    actions->addWidget(quickBtn);

    actions->addStretch();
    cardLay->addSpacing(4);
    cardLay->addLayout(actions);

    outer->addWidget(card, 0, Qt::AlignHCenter);
    outer->addStretch();

    return page;
}

void MainWindow::updateEmptyState()
{
    if (!m_mainStack || !m_screenTabWidget) {
        return;
    }
    const bool hasTabs = m_screenTabWidget->count() > 0;
    m_mainStack->setCurrentIndex(hasTabs ? 1 : 0);

    // 没有终端时命令栏不可用，避免输入后无处发送
    if (QWidget *cmdBar = findChild<QWidget *>(QStringLiteral("commandBar"))) {
        cmdBar->setEnabled(hasTabs);
    }
}

void MainWindow::buildStatusBarWidgets()
{
    QStatusBar *bar = statusBar();
    bar->setSizeGripEnabled(false);

    m_statusConnection = new QLabel;
    m_statusConnection->setObjectName("statusConnection");
    m_statusProtocol = new QLabel;
    m_statusEncoding = new QLabel(QStringLiteral("UTF-8"));
    m_statusSize = new QLabel;
    m_statusLatency = new QLabel;
    m_statusLatency->setObjectName("statusLatency");

    bar->addWidget(m_statusConnection);
    bar->addWidget(m_statusProtocol);
    bar->addWidget(m_statusEncoding);
    bar->addWidget(m_statusSize);
    bar->addPermanentWidget(m_statusLatency);

    updateStatusBar();
}

void MainWindow::initMainWindowMenu()
{
    // 使用 QMainWindow 内置 menuBar，避免 new QMenuBar() 泄漏 Qt 预创建的默认菜单栏
    m_menuBar = this->menuBar();

    // 菜单标题先留空，统一在 bindText 中按当前语言填写
    m_fileMenu = m_menuBar->addMenu(QString());
    m_editMenu = m_menuBar->addMenu(QString());
    m_viewMenu = m_menuBar->addMenu(QString());
    m_optMenu = m_menuBar->addMenu(QString());
    m_transmitMenu = m_menuBar->addMenu(QString());
    m_scriptMenu = m_menuBar->addMenu(QString());
    m_toolsMenu = m_menuBar->addMenu(QString());
    m_pluginMenu = m_menuBar->addMenu(QString());
    m_helpMenu = m_menuBar->addMenu(QString());

    bindText([this] {
        m_fileMenu->setTitle(tr("文件(&F)"));
        m_editMenu->setTitle(tr("编辑(&E)"));
        m_viewMenu->setTitle(tr("视图(&V)"));
        m_optMenu->setTitle(tr("设置(&S)"));
        m_transmitMenu->setTitle(tr("传输(&T)"));
        m_scriptMenu->setTitle(tr("脚本(&P)"));
        m_toolsMenu->setTitle(tr("工具(&O)"));
        m_pluginMenu->setTitle(tr("插件(&L)"));
        m_helpMenu->setTitle(tr("帮助(&H)"));
    });

    m_sessionAction = new QAction(this);
    bindText([this] { m_sessionAction->setText(tr("连接...")); });
    m_fileMenu->addAction(m_sessionAction);

    // 持久化"加载会话"子菜单：位置与历史版本一致（紧跟"连接"），
    // 内容由 populateSessionMenu() 用 clear() 重建，不再反复 new QMenu
    m_loadSessionMenu = new QMenu(QString(), m_fileMenu);
    bindText([this] { m_loadSessionMenu->setTitle(tr("加载会话")); });
    m_fileMenu->addMenu(m_loadSessionMenu);

    m_saveSessionAction = new QAction(this);
    bindText([this] { m_saveSessionAction->setText(tr("保存当前会话")); });
    connect(m_saveSessionAction, &QAction::triggered, this, &MainWindow::saveCurrentSession);
    m_fileMenu->addAction(m_saveSessionAction);

    m_manageSessionsAction = new QAction(this);
    bindText([this] { m_manageSessionsAction->setText(tr("管理会话")); });
    connect(m_manageSessionsAction, &QAction::triggered, this, &MainWindow::manageSessions);
    m_fileMenu->addAction(m_manageSessionsAction);

    m_fileMenu->addSeparator();
    m_printAction = new QAction(this);
    bindText([this] { m_printAction->setText(tr("打印")); });
    m_fileMenu->addAction(m_printAction);
    m_fileMenu->addSeparator();

    m_exitAction = new QAction(this);
    bindText([this] { m_exitAction->setText(tr("退出")); });
    connect(m_exitAction, &QAction::triggered, this, &QMainWindow::close);
    m_fileMenu->addAction(m_exitAction);

    m_themeAction = new QAction(this);
    bindText([this] { m_themeAction->setText(tr("主题设置")); });
    connect(m_themeAction, &QAction::triggered, this, &MainWindow::showThemeSettings);
    m_optMenu->addAction(m_themeAction);

    m_logSettingsAction = new QAction(this);
    bindText([this] { m_logSettingsAction->setText(tr("日志设置")); });
    connect(m_logSettingsAction, &QAction::triggered, this, &MainWindow::showLogSettings);
    m_optMenu->addAction(m_logSettingsAction);

    // 语言切换：中文（缺省）/ English，立即生效并写入配置
    m_languageMenu = m_optMenu->addMenu(QString());
    bindText([this] { m_languageMenu->setTitle(tr("语言")); });

    auto *languageGroup = new QActionGroup(this);
    languageGroup->setExclusive(true);
    m_chineseAction = new QAction(LanguageManager::languageDisplayName(
                                      LanguageManager::Language::Chinese), this);
    m_englishAction = new QAction(LanguageManager::languageDisplayName(
                                      LanguageManager::Language::English), this);
    for (QAction *action : { m_chineseAction, m_englishAction }) {
        action->setCheckable(true);
        languageGroup->addAction(action);
        m_languageMenu->addAction(action);
    }
    const bool english = LanguageManager::instance()->current()
                         == LanguageManager::Language::English;
    (english ? m_englishAction : m_chineseAction)->setChecked(true);
    connect(m_chineseAction, &QAction::triggered, this, [] {
        LanguageManager::instance()->setLanguage(LanguageManager::Language::Chinese);
    });
    connect(m_englishAction, &QAction::triggered, this, [] {
        LanguageManager::instance()->setLanguage(LanguageManager::Language::English);
    });
    connect(LanguageManager::instance(), &LanguageManager::languageChanged, this,
            [this](LanguageManager::Language language) {
                m_chineseAction->setChecked(language == LanguageManager::Language::Chinese);
                m_englishAction->setChecked(language == LanguageManager::Language::English);
            });

    m_terminalTestAction = new QAction(this);
    bindText([this] { m_terminalTestAction->setText(tr("终端兼容性测试")); });
    connect(m_terminalTestAction, &QAction::triggered, this, &MainWindow::openTerminalTester);
    m_optMenu->addAction(m_terminalTestAction);

    // 工具菜单
    m_calculatorAction = new QAction(this);
    bindText([this] { m_calculatorAction->setText(tr("计算器")); });
    connect(m_calculatorAction, &QAction::triggered, this, &MainWindow::showCalculator);
    m_toolsMenu->addAction(m_calculatorAction);

    m_portScannerAction = new QAction(this);
    bindText([this] { m_portScannerAction->setText(tr("端口扫描")); });
    connect(m_portScannerAction, &QAction::triggered, this, &MainWindow::showPortScanner);
    m_toolsMenu->addAction(m_portScannerAction);

    QAction *networkServicesAction = new QAction(this);
    bindText([networkServicesAction] { networkServicesAction->setText(tr("网络服务")); });
    connect(networkServicesAction, &QAction::triggered, this, &MainWindow::showNetworkServices);
    m_toolsMenu->addAction(networkServicesAction);
    m_networkServicesAction = networkServicesAction;

    m_asciiTableAction = new QAction(this);
    bindText([this] { m_asciiTableAction->setText(tr("ASCII 码表")); });
    connect(m_asciiTableAction, &QAction::triggered, this, &MainWindow::showAsciiTable);
    m_toolsMenu->addAction(m_asciiTableAction);

    // 脚本菜单
    m_loadScriptAction = new QAction(this);
    bindText([this] { m_loadScriptAction->setText(tr("加载脚本...")); });
    connect(m_loadScriptAction, &QAction::triggered, this, &MainWindow::loadScript);
    m_scriptMenu->addAction(m_loadScriptAction);

    m_stopScriptAction = new QAction(this);
    bindText([this] { m_stopScriptAction->setText(tr("停止脚本")); });
    m_stopScriptAction->setEnabled(false);
    connect(m_stopScriptAction, &QAction::triggered, this, &MainWindow::stopScript);
    m_scriptMenu->addAction(m_stopScriptAction);

    m_scriptMenu->addSeparator();

    m_scriptLogAction = new QAction(this);
    bindText([this] { m_scriptLogAction->setText(tr("查看脚本日志")); });
    connect(m_scriptLogAction, &QAction::triggered, this, &MainWindow::showScriptLog);
    m_scriptMenu->addAction(m_scriptLogAction);

    m_scriptControlAction = new QAction(this);
    bindText([this] { m_scriptControlAction->setText(tr("脚本控制面板")); });
    connect(m_scriptControlAction, &QAction::triggered, this, &MainWindow::showScriptControlPanel);
    m_scriptMenu->addAction(m_scriptControlAction);

    // 帮助菜单
    m_aboutAction = new QAction(this);
    bindText([this] { m_aboutAction->setText(tr("关于 NTerminal")); });
    connect(m_aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("关于 NTerminal"),
                           tr("NTerminal 多协议终端工具\n版本 %1")
                               .arg(QCoreApplication::applicationVersion()));
    });
    m_helpMenu->addAction(m_aboutAction);

    // ---- 编辑菜单：复制/粘贴/清屏（快捷键沿用 xterm 惯例，避开终端里的 Ctrl+C） ----
    m_copyAction = new QAction(this);
    bindText([this] { m_copyAction->setText(tr("复制")); });
    m_copyAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    connect(m_copyAction, &QAction::triggered, this, [this] {
        if (TerminalProxy *terminal = getCurrentTerminal()) {
            terminal->copyToClipboard();
        }
    });
    m_editMenu->addAction(m_copyAction);

    m_pasteAction = new QAction(this);
    bindText([this] { m_pasteAction->setText(tr("粘贴")); });
    m_pasteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    connect(m_pasteAction, &QAction::triggered, this, [this] {
        if (TerminalProxy *terminal = getCurrentTerminal()) {
            const QString text = QGuiApplication::clipboard()->text();
            if (!text.isEmpty()) {
                terminal->sendText(text);
            }
        }
    });
    m_editMenu->addAction(m_pasteAction);

    m_editMenu->addSeparator();
    m_clearScreenAction = new QAction(this);
    bindText([this] { m_clearScreenAction->setText(tr("清屏")); });
    m_clearScreenAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    connect(m_clearScreenAction, &QAction::triggered, this, [this] {
        if (TerminalProxy *terminal = getCurrentTerminal()) {
            terminal->processTerminalData(QByteArrayLiteral("\x1b[2J\x1b[H"));
        }
    });
    m_editMenu->addAction(m_clearScreenAction);

    // ---- 视图菜单：侧栏与全屏 ----
    m_toggleSidebarAction = new QAction(this);
    bindText([this] { m_toggleSidebarAction->setText(tr("会话侧栏")); });
    m_toggleSidebarAction->setCheckable(true);
    m_toggleSidebarAction->setChecked(true);
    m_toggleSidebarAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    connect(m_toggleSidebarAction, &QAction::toggled, this, [this](bool visible) {
        if (m_sidebar) {
            m_sidebar->setVisible(visible);
        }
    });
    m_viewMenu->addAction(m_toggleSidebarAction);

    m_fullScreenAction = new QAction(this);
    bindText([this] { m_fullScreenAction->setText(tr("全屏")); });
    m_fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    connect(m_fullScreenAction, &QAction::triggered, this, [this] {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    });
    m_viewMenu->addAction(m_fullScreenAction);

    // ---- 传输菜单：直达 SFTP / FTP 连接对话框 ----
    m_sftpAction = new QAction(this);
    bindText([this] { m_sftpAction->setText(tr("SFTP 连接...")); });
    connect(m_sftpAction, &QAction::triggered, this, [this] {
        openConnectionFromDialog(true, Common::ConnectionType::SFTP);
    });
    m_transmitMenu->addAction(m_sftpAction);

    m_ftpAction = new QAction(this);
    bindText([this] { m_ftpAction->setText(tr("FTP 连接...")); });
    connect(m_ftpAction, &QAction::triggered, this, [this] {
        openConnectionFromDialog(true, Common::ConnectionType::FTP);
    });
    m_transmitMenu->addAction(m_ftpAction);

    // ---- 插件菜单：当前仅提供插件目录入口 ----
    m_pluginDirAction = new QAction(this);
    bindText([this] { m_pluginDirAction->setText(tr("打开插件目录...")); });
    connect(m_pluginDirAction, &QAction::triggered, this, [] {
        QDir dir(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"));
        if (!dir.exists()) {
            dir.mkpath(QStringLiteral("."));
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
    });
    m_pluginMenu->addAction(m_pluginDirAction);

    // 各菜单已通过 menuBar()->addMenu(QString) 创建并挂载，无需再次添加

    connect(m_sessionAction, &QAction::triggered, this, [this]() {
        openConnectionFromDialog(false);
    });

    applyMenuIcons();
}

// 菜单/动作图标（IconProvider 按主题着色；无图标名则保持文字）
void MainWindow::refreshIcons()
{
    IconProvider::clearCache();
    applyMenuIcons();

    for (QToolButton *button : findChildren<QToolButton *>()) {
        const QString name = button->property("iconName").toString();
        if (!name.isEmpty()) {
            button->setIcon(IconProvider::themedIcon(name));
        }
    }
    for (QPushButton *button : findChildren<QPushButton *>()) {
        const QString name = button->property("iconName").toString();
        if (name.isEmpty()) {
            continue;
        }
        // 强调色实心按钮上的图标用"强调色文字色"，深浅主题下都保持对比度
        button->setIcon(button->property("iconOnAccent").toBool()
                            ? IconProvider::icon(name,
                                  ThemeManager::instance()->currentAppTheme().highlightText)
                            : IconProvider::themedIcon(name));
    }
    if (m_emptyState) {
        if (QLabel *chip = m_emptyState->findChild<QLabel *>(QStringLiteral("emptyStateIcon"))) {
            chip->setPixmap(IconProvider::pixmap("terminal",
                                                 ThemeManager::instance()->currentAppTheme().accentText,
                                                 30, devicePixelRatioF()));
        }
    }
    if (m_terminalTabBar) {
        m_terminalTabBar->update();
    }
}

void MainWindow::applyMenuIcons()
{
    if (m_sessionAction) m_sessionAction->setIcon(IconProvider::themedIcon("new-session"));
    if (m_themeAction) m_themeAction->setIcon(IconProvider::themedIcon("theme"));
    if (m_logSettingsAction) m_logSettingsAction->setIcon(IconProvider::themedIcon("log"));
    if (m_exitAction) m_exitAction->setIcon(IconProvider::themedIcon("close"));
    if (m_loadScriptAction) m_loadScriptAction->setIcon(IconProvider::themedIcon("script"));
    if (m_stopScriptAction) m_stopScriptAction->setIcon(IconProvider::themedIcon("stop"));
    if (m_scriptLogAction) m_scriptLogAction->setIcon(IconProvider::themedIcon("log"));
    if (m_scriptControlAction) m_scriptControlAction->setIcon(IconProvider::themedIcon("settings"));
    // 文件菜单
    if (m_loadSessionMenu) m_loadSessionMenu->menuAction()->setIcon(IconProvider::themedIcon("folder"));
    if (m_saveSessionAction) m_saveSessionAction->setIcon(IconProvider::themedIcon("save"));
    if (m_manageSessionsAction) m_manageSessionsAction->setIcon(IconProvider::themedIcon("settings"));
    if (m_printAction) m_printAction->setIcon(IconProvider::themedIcon("print"));
    // 编辑 / 视图 / 传输 / 插件
    if (m_copyAction) m_copyAction->setIcon(IconProvider::themedIcon("copy"));
    if (m_pasteAction) m_pasteAction->setIcon(IconProvider::themedIcon("paste"));
    if (m_clearScreenAction) m_clearScreenAction->setIcon(IconProvider::themedIcon("clear"));
    if (m_toggleSidebarAction) m_toggleSidebarAction->setIcon(IconProvider::themedIcon("split"));
    if (m_fullScreenAction) m_fullScreenAction->setIcon(IconProvider::themedIcon("fullscreen"));
    if (m_sftpAction) m_sftpAction->setIcon(IconProvider::themedIcon("sftp"));
    if (m_ftpAction) m_ftpAction->setIcon(IconProvider::themedIcon("transfer"));
    if (m_pluginDirAction) m_pluginDirAction->setIcon(IconProvider::themedIcon("grid"));
    if (m_languageMenu) m_languageMenu->menuAction()->setIcon(IconProvider::themedIcon("globe"));
    // 工具/测试动作按成员指针设置图标：菜单文案会随语言切换，不能按文字匹配
    if (m_terminalTestAction) m_terminalTestAction->setIcon(IconProvider::themedIcon("terminal"));
    if (m_calculatorAction) m_calculatorAction->setIcon(IconProvider::themedIcon("calculator"));
    if (m_portScannerAction) m_portScannerAction->setIcon(IconProvider::themedIcon("search"));
    if (m_asciiTableAction) m_asciiTableAction->setIcon(IconProvider::themedIcon("log"));
    if (m_networkServicesAction) m_networkServicesAction->setIcon(IconProvider::themedIcon("server"));
    if (m_aboutAction) m_aboutAction->setIcon(IconProvider::themedIcon("terminal"));
}

void MainWindow::openConnectionFromDialog(bool applyTheme, Common::ConnectionType type)
{
    ConnectionDialog dialog(this);
    dialog.setPreferredType(type);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    Q_UNUSED(applyTheme);   // openSession 内统一注入当前主题/字体
    const Common::ConnectionSettings settings = dialog.getSettings();
    // 对话框产出的是一份完整的会话定义：先登记并落盘，再打开标签页，
    // 这样连接后会话管理器/“会话”菜单里立刻能看到它。
    registerSession(settings);
    openSession(settings, settings.sessionName);
}

void MainWindow::showThemeSettings()
{
    ThemeSettingsDialog dialog(this);
    dialog.exec();

    for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it) {
        applyTerminalTheme(it.value());
    }
}

void MainWindow::showLogSettings()
{
    LogConfigDialog dialog(this);
    dialog.exec();
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

void MainWindow::onTabContextMenu(const QPoint &pos)
{
    int tabIndex = m_screenTabWidget->tabBar()->tabAt(pos);
    if (tabIndex < 0) {
        return;
    }

    // 获取该tab对应的terminal
    QString terminalId = m_screenTabWidget->tabBar()->tabData(tabIndex).toString();
    TerminalProxy *terminal = getTerminalById(terminalId);
    if (!terminal) {
        return;
    }

    // 获取连接设置
    Common::ConnectionSettings settings = terminal->getConnectionSettings();

    // 创建上下文菜单
    QMenu contextMenu(this);

    // 只有非串口会话才显示"复制会话"选项
    if (settings.type != Common::ConnectionType::Serial) {
        QAction *duplicateAction = contextMenu.addAction(tr("复制会话"));
        connect(duplicateAction, &QAction::triggered, this, [this, settings]() {
            // 创建新的tab，使用相同的连接设置
            QString tabName = settings.sessionName.isEmpty() ?
                tr("终端 %1").arg(m_terminals.size() + 1) :
                tr("%1 (副本)").arg(settings.sessionName);

            TerminalProxy *newTerminal = createNewTerminalTab(tabName);
            if (newTerminal) {
                newTerminal->setConnectionSettings(settings);
                applyTerminalTheme(newTerminal);
                if (newTerminal->openConnection()) {
                    qDebug() << "Duplicated session opened successfully";
                } else {
                    qDebug() << "Duplicated session failed to open";
                }
            }
        });
    }

    contextMenu.addSeparator();
    QAction *closeAction = contextMenu.addAction(tr("关闭"));
    connect(closeAction, &QAction::triggered, this, [this, tabIndex]() {
        closeTerminalTab(tabIndex);
    });

    // 显示菜单
    contextMenu.exec(m_screenTabWidget->tabBar()->mapToGlobal(pos));
}

void MainWindow::duplicateSession()
{
    // This function is kept for potential future use
    // The actual duplication is handled in onTabContextMenu
}

void MainWindow::showCalculator()
{
    CalculatorDialog *dialog = new CalculatorDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::showPortScanner()
{
    PortScannerDialog *dialog = new PortScannerDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::showNetworkServices()
{
    qDebug() << "Network Services - To be implemented";
    // TODO: 实现网络服务对话框
}

void MainWindow::showAsciiTable()
{
    AsciiTableDialog *dialog = new AsciiTableDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::loadScript()
{
    TerminalProxy *terminal = getCurrentTerminal();
    if (!terminal) {
        QMessageBox::warning(this, tr("警告"), tr("请先打开一个终端连接"));
        return;
    }

    ScriptDialog dialog(this);
    // TODO: 设置最近使用的脚本列表
    // dialog.setRecentScripts(recentScripts);

    if (dialog.exec() == QDialog::Accepted) {
        QString scriptPath = dialog.selectedScriptPath();
        if (!scriptPath.isEmpty()) {
            if (terminal->loadScript(scriptPath)) {
                m_stopScriptAction->setEnabled(true);
                qDebug() << "Script loaded:" << scriptPath;
            } else {
                QMessageBox::critical(this, tr("错误"),
                    tr("无法加载脚本: %1").arg(scriptPath));
            }
        }
    }
}

void MainWindow::stopScript()
{
    TerminalProxy *terminal = getCurrentTerminal();
    if (!terminal) {
        return;
    }

    terminal->stopScript();
    m_stopScriptAction->setEnabled(false);
    qDebug() << "Script stopped";
}

void MainWindow::showScriptLog()
{
    ScriptLogViewer *viewer = new ScriptLogViewer(this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->show();
}

void MainWindow::showScriptControlPanel()
{
    TerminalProxy *terminal = getCurrentTerminal();
    if (!terminal) {
        QMessageBox::warning(this, tr("警告"), tr("请先打开一个终端连接"));
        return;
    }

    ScriptControlPanel *panel = new ScriptControlPanel(this);
    panel->setAttribute(Qt::WA_DeleteOnClose);

    // 设置当前脚本状态
    panel->setScriptRunning(terminal->isScriptRunning());
    panel->setScriptName(terminal->currentScriptName());
    if (terminal->isScriptRunning()) {
        panel->setScriptStartTime(terminal->scriptStartTime());
    }

    // 连接信号
    connect(panel, &ScriptControlPanel::loadScriptRequested,
            this, &MainWindow::loadScript);
    connect(panel, &ScriptControlPanel::stopScriptRequested,
            this, &MainWindow::stopScript);
    connect(panel, &ScriptControlPanel::viewLogRequested,
            this, &MainWindow::showScriptLog);

    panel->show();
    // 悬浮在主窗口中部，避免遮挡侧栏与终端内容
    panel->adjustSize();
    panel->move(frameGeometry().center() - panel->rect().center());
    panel->raise();
    panel->activateWindow();
}
