#ifndef SCRIPTSTATUSWIDGET_H
#define SCRIPTSTATUSWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QDateTime>
#include <QTimer>

class ScriptStatusWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScriptStatusWidget(QWidget *parent = nullptr);
    ~ScriptStatusWidget();

    void setScriptRunning(bool running);
    void setScriptName(const QString &name);
    void setScriptStartTime(const QDateTime &startTime);
    void updateStatus();

signals:
    void clicked();  // 点击状态栏时发出信号

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private slots:
    void updateRunningTime();

private:
    void setupUI();
    QString formatDuration(qint64 seconds) const;

    QLabel *m_statusIcon;
    QLabel *m_scriptNameLabel;
    QLabel *m_runningTimeLabel;

    QTimer *m_updateTimer;
    QDateTime m_startTime;
    bool m_isRunning;
};

#endif // SCRIPTSTATUSWIDGET_H
