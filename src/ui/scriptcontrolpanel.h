#ifndef SCRIPTCONTROLPANEL_H
#define SCRIPTCONTROLPANEL_H

#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QDateTime>

class ScriptControlPanel : public QDialog
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

protected:
    // 原生标题栏只保留关闭按钮（Windows 会自行补上最小化/最大化）
    void showEvent(QShowEvent *event) override;

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
