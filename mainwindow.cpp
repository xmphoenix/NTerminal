#include "mainwindow.h"
#include "terminalwidget.h"
#include "connectiondialog.h"
#include <QSplitter>
#include <QListWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QMenuBar>
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
    , m_terminal(nullptr)
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
    , m_debugAction(nullptr)
{
    initMainWindowMenu();
    initMainWindowLayout();
}

MainWindow::~MainWindow() {}

void MainWindow::initMainWindowLayout()
{
    m_verticalSplitter = new QSplitter(Qt::Vertical);
    m_horizontalSplitter = new QSplitter(Qt::Horizontal, m_verticalSplitter);

    m_listWidget = new QListWidget(m_horizontalSplitter);
    m_screenTabWidget = new QTabWidget(m_horizontalSplitter);
    m_horizontalSplitter->setStretchFactor(0, 1);
    m_horizontalSplitter->setStretchFactor(1, 180);

    m_terminal = new TerminalWidget();
    m_screenTabWidget->addTab(m_terminal, "Terminal");

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
    m_scriptMenu = m_menuBar->addMenu("脚本");
    m_helpMenu = m_menuBar->addMenu("帮助");

    m_sessionAction = new QAction("新建连接", this);
    m_fileMenu->addAction(m_sessionAction);
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
            
            // 设置连接参数并打开连接
            if (m_terminal) {
                m_terminal->setConnectionSettings(settings);
                if (m_terminal->openConnection()) {
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
            
            // 设置连接参数并打开连接
            if (m_terminal) {
                m_terminal->setConnectionSettings(settings);
                if (m_terminal->openConnection()) {
                    qDebug() << "连接已成功打开";
                } else {
                    qDebug() << "连接打开失败";
                }
            }
        }
    });
}
