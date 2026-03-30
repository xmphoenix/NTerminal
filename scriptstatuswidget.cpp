#include "scriptstatuswidget.h"
#include <QHBoxLayout>
#include <QMouseEvent>

ScriptStatusWidget::ScriptStatusWidget(QWidget *parent)
    : QWidget(parent)
    , m_statusIcon(nullptr)
    , m_scriptNameLabel(nullptr)
    , m_runningTimeLabel(nullptr)
    , m_updateTimer(nullptr)
    , m_isRunning(false)
{
    setupUI();

    // 创建定时器用于更新运行时间
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout,
            this, &ScriptStatusWidget::updateRunningTime);

    // 设置鼠标悬停效果
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

ScriptStatusWidget::~ScriptStatusWidget()
{
}

void ScriptStatusWidget::setupUI()
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(5, 2, 5, 2);
    layout->setSpacing(8);

    // 状态图标
    m_statusIcon = new QLabel(this);
    m_statusIcon->setText("●");
    m_statusIcon->setStyleSheet("QLabel { color: gray; font-size: 10px; }");
    layout->addWidget(m_statusIcon);

    // 脚本名称
    m_scriptNameLabel = new QLabel(tr("无脚本"), this);
    m_scriptNameLabel->setStyleSheet("QLabel { color: #555; font-size: 11px; }");
    layout->addWidget(m_scriptNameLabel);

    // 运行时间
    m_runningTimeLabel = new QLabel(this);
    m_runningTimeLabel->setStyleSheet("QLabel { color: #777; font-size: 10px; }");
    m_runningTimeLabel->setVisible(false);
    layout->addWidget(m_runningTimeLabel);

    layout->addStretch();

    // 设置整体样式
    setStyleSheet("ScriptStatusWidget { background-color: #f0f0f0; border-radius: 3px; }");
}

void ScriptStatusWidget::setScriptRunning(bool running)
{
    m_isRunning = running;

    if (running) {
        m_statusIcon->setStyleSheet("QLabel { color: #4CAF50; font-size: 10px; }");
        m_runningTimeLabel->setVisible(true);
        m_updateTimer->start(1000);  // 每秒更新一次
    } else {
        m_statusIcon->setStyleSheet("QLabel { color: gray; font-size: 10px; }");
        m_runningTimeLabel->setVisible(false);
        m_updateTimer->stop();
    }
}

void ScriptStatusWidget::setScriptName(const QString &name)
{
    if (name.isEmpty()) {
        m_scriptNameLabel->setText(tr("无脚本"));
    } else {
        m_scriptNameLabel->setText(name);
    }
}

void ScriptStatusWidget::setScriptStartTime(const QDateTime &startTime)
{
    m_startTime = startTime;
    updateRunningTime();
}

void ScriptStatusWidget::updateStatus()
{
    updateRunningTime();
}

void ScriptStatusWidget::updateRunningTime()
{
    if (!m_isRunning || !m_startTime.isValid()) {
        return;
    }

    qint64 seconds = m_startTime.secsTo(QDateTime::currentDateTime());
    m_runningTimeLabel->setText(formatDuration(seconds));
}

QString ScriptStatusWidget::formatDuration(qint64 seconds) const
{
    if (seconds < 60) {
        return tr("%1秒").arg(seconds);
    } else if (seconds < 3600) {
        int minutes = seconds / 60;
        int secs = seconds % 60;
        return tr("%1分%2秒").arg(minutes).arg(secs);
    } else {
        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        return tr("%1时%2分").arg(hours).arg(minutes);
    }
}

void ScriptStatusWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QWidget::mousePressEvent(event);
}

void ScriptStatusWidget::enterEvent(QEnterEvent *event)
{
    setStyleSheet("ScriptStatusWidget { background-color: #e0e0e0; border-radius: 3px; }");
    QWidget::enterEvent(event);
}

void ScriptStatusWidget::leaveEvent(QEvent *event)
{
    setStyleSheet("ScriptStatusWidget { background-color: #f0f0f0; border-radius: 3px; }");
    QWidget::leaveEvent(event);
}

