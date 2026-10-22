#include "ui/scriptcontrolpanel.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QStyle>

ScriptControlPanel::ScriptControlPanel(QWidget *parent)
    : QWidget(parent)
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

void ScriptControlPanel::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    // 状态显示区域
    QGroupBox *statusGroup = new QGroupBox(tr("脚本状态"), this);
    QVBoxLayout *statusLayout = new QVBoxLayout(statusGroup);

    // 状态指示器
    m_statusLabel = new QLabel(tr("● 未运行"), this);
    m_statusLabel->setStyleSheet("QLabel { color: gray; font-weight: bold; }");
    statusLayout->addWidget(m_statusLabel);

    // 脚本名称
    m_scriptNameLabel = new QLabel(tr("无脚本"), this);
    m_scriptNameLabel->setStyleSheet("QLabel { color: #555; }");
    statusLayout->addWidget(m_scriptNameLabel);

    // 运行时间
    m_runningTimeLabel = new QLabel(tr("运行时间: --:--:--"), this);
    m_runningTimeLabel->setStyleSheet("QLabel { color: #555; font-size: 11px; }");
    statusLayout->addWidget(m_runningTimeLabel);

    mainLayout->addWidget(statusGroup);

    // 控制按钮区域
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    m_loadButton = new QPushButton(tr("加载脚本"), this);
    m_loadButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    connect(m_loadButton, &QPushButton::clicked,
            this, &ScriptControlPanel::loadScriptRequested);
    buttonLayout->addWidget(m_loadButton);

    m_stopButton = new QPushButton(tr("停止"), this);
    m_stopButton->setIcon(style()->standardIcon(QStyle::SP_BrowserStop));
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked,
            this, &ScriptControlPanel::stopScriptRequested);
    buttonLayout->addWidget(m_stopButton);

    m_logButton = new QPushButton(tr("查看日志"), this);
    m_logButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(m_logButton, &QPushButton::clicked,
            this, &ScriptControlPanel::viewLogRequested);
    buttonLayout->addWidget(m_logButton);

    mainLayout->addLayout(buttonLayout);
    mainLayout->addStretch();
}

void ScriptControlPanel::setScriptRunning(bool running)
{
    m_isRunning = running;

    if (running) {
        m_statusLabel->setText(tr("● 运行中"));
        m_statusLabel->setStyleSheet("QLabel { color: green; font-weight: bold; }");
        m_loadButton->setEnabled(false);
        m_stopButton->setEnabled(true);
        m_updateTimer->start(1000);  // 每秒更新一次
    } else {
        m_statusLabel->setText(tr("● 未运行"));
        m_statusLabel->setStyleSheet("QLabel { color: gray; font-weight: bold; }");
        m_loadButton->setEnabled(true);
        m_stopButton->setEnabled(false);
        m_updateTimer->stop();
        m_runningTimeLabel->setText(tr("运行时间: --:--:--"));
    }
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

