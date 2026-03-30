#ifndef SCRIPTCONTROLPANEL_H
#define SCRIPTCONTROLPANEL_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QDateTime>

class ScriptControlPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScriptControlPanel(QWidget *parent = nullptr);
    ~ScriptControlPanel();

    void setScriptRunning(bool running);
    void setScriptName(const QString &name);
    void setScriptStartTime(const QDateTime &startTime);
    void setScriptStatus(const QString &status);

signals:
    void loadScriptRequested();
    void stopScriptRequested();
    void viewLogRequested();

private slots:
    void updateRunningTime();

private:
    void setupUI();
    QString formatDuration(qint64 seconds) const;

    QPushButton *m_loadButton;
    QPushButton *m_stopButton;
    QPushButton *m_logButton;
    QLabel *m_statusLabel;
    QLabel *m_scriptNameLabel;
    QLabel *m_runningTimeLabel;

    QTimer *m_updateTimer;
    QDateTime m_startTime;
    bool m_isRunning;
};

#endif // SCRIPTCONTROLPANEL_H
