#include "ui/scriptcontrolpanel.h"
#include "ui/iconprovider.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QStyle>
#include <QShowEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

ScriptControlPanel::ScriptControlPanel(QWidget *parent)
    : QDialog(parent)
    , m_loadButton(nullptr)
    , m_stopButton(nullptr)
    , m_logButton(nullptr)
    , m_statusLabel(nullptr)
    , m_scriptNameLabel(nullptr)
    , m_runningTimeLabel(nullptr)
    , m_updateTimer(nullptr)
    , m_isRunning(false)
{
    setupUI();

    // 创建定时器用于更新运行时间
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout,
            this, &ScriptControlPanel::updateRunningTime);
}

ScriptControlPanel::~ScriptControlPanel()
{
}

void ScriptControlPanel::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
#ifdef Q_OS_WIN
    // Qt 会在原生对话框标题栏自动补上最小化/最大化，这里按 macOS/Material 工具面板的习惯只留关闭
    HWND hwnd = reinterpret_cast<HWND>(winId());
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    const LONG desired = style & ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    if (desired != style) {
        SetWindowLong(hwnd, GWL_STYLE, desired);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif
}

void ScriptControlPanel::setupUI()
{
    setWindowTitle(tr("脚本控制面板"));
    // 非模态工具面板：固定尺寸、仅保留关闭按钮
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint
                   | Qt::MSWindowsFixedSizeDialogHint);
    setMinimumWidth(380);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 18, 20, 18);
    mainLayout->setSpacing(12);

    // 头部
    QLabel *titleLabel = new QLabel(tr("脚本控制面板"), this);
    titleLabel->setProperty("class", "sectionTitle");
    QLabel *hintLabel = new QLabel(tr("脚本将在当前终端会话中执行"), this);
    hintLabel->setProperty("class", "hint");
    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(hintLabel);

    // 状态显示区域
    QGroupBox *statusGroup = new QGroupBox(tr("脚本状态"), this);
    QVBoxLayout *statusLayout = new QVBoxLayout(statusGroup);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(6);

    // 状态指示器
    m_statusLabel = new QLabel(tr("● 未运行"), this);
    m_statusLabel->setObjectName("scriptStatus");
    m_statusLabel->setProperty("state", "idle");
    statusLayout->addWidget(m_statusLabel);

    // 脚本名称
    m_scriptNameLabel = new QLabel(tr("无脚本"), this);
    m_scriptNameLabel->setObjectName("scriptName");
    m_scriptNameLabel->setWordWrap(true);
    statusLayout->addWidget(m_scriptNameLabel);

    // 运行时间
    m_runningTimeLabel = new QLabel(tr("运行时间: --:--:--"), this);
    m_runningTimeLabel->setProperty("class", "hint");
    statusLayout->addWidget(m_runningTimeLabel);

    mainLayout->addWidget(statusGroup);

    // 控制按钮区域
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    m_loadButton = new QPushButton(tr("加载脚本"), this);
    m_loadButton->setProperty("class", "primary");
    m_loadButton->setIcon(IconProvider::themedIcon("folder-open"));
    connect(m_loadButton, &QPushButton::clicked,
            this, &ScriptControlPanel::loadScriptRequested);
    buttonLayout->addWidget(m_loadButton);

    m_stopButton = new QPushButton(tr("停止"), this);
    m_stopButton->setProperty("class", "danger");
    m_stopButton->setIcon(IconProvider::themedIcon("stop"));
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked,
            this, &ScriptControlPanel::stopScriptRequested);
    buttonLayout->addWidget(m_stopButton);

    m_logButton = new QPushButton(tr("查看日志"), this);
    m_logButton->setProperty("class", "tonal");
    m_logButton->setIcon(IconProvider::themedIcon("log"));
    connect(m_logButton, &QPushButton::clicked,
            this, &ScriptControlPanel::viewLogRequested);
    buttonLayout->addWidget(m_logButton);

    mainLayout->addLayout(buttonLayout);

    // 底部关闭
    QHBoxLayout *footerLayout = new QHBoxLayout();
    footerLayout->addStretch();
    QPushButton *closeButton = new QPushButton(tr("关闭"), this);
    closeButton->setProperty("class", "ghost");
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    footerLayout->addWidget(closeButton);
    mainLayout->addLayout(footerLayout);
}

void ScriptControlPanel::setScriptRunning(bool running)
{
    m_isRunning = running;

    if (running) {
        m_statusLabel->setText(tr("● 运行中"));
        m_statusLabel->setProperty("state", "running");
        m_loadButton->setEnabled(false);
        m_stopButton->setEnabled(true);
        m_updateTimer->start(1000);  // 每秒更新一次
    } else {
        m_statusLabel->setText(tr("● 未运行"));
        m_statusLabel->setProperty("state", "idle");
        m_loadButton->setEnabled(true);
        m_stopButton->setEnabled(false);
        m_updateTimer->stop();
        m_runningTimeLabel->setText(tr("运行时间: --:--:--"));
    }
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void ScriptControlPanel::setScriptName(const QString &name)
{
    if (name.isEmpty()) {
        m_scriptNameLabel->setText(tr("无脚本"));
    } else {
        m_scriptNameLabel->setText(tr("脚本: %1").arg(name));
    }
}

void ScriptControlPanel::setScriptStartTime(const QDateTime &startTime)
{
    m_startTime = startTime;
    updateRunningTime();
}

void ScriptControlPanel::setScriptStatus(const QString &status)
{
    if (!status.isEmpty()) {
        m_statusLabel->setText(tr("● %1").arg(status));
    }
}

void ScriptControlPanel::updateRunningTime()
{
    if (!m_isRunning || !m_startTime.isValid()) {
        return;
    }

    qint64 seconds = m_startTime.secsTo(QDateTime::currentDateTime());
    m_runningTimeLabel->setText(tr("运行时间: %1").arg(formatDuration(seconds)));
}

QString ScriptControlPanel::formatDuration(qint64 seconds) const
{
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;

    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}

