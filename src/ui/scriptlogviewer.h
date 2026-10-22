#ifndef SCRIPTLOGVIEWER_H
#define SCRIPTLOGVIEWER_H

#include <QDialog>
#include <QString>
#include <QTextEdit>
#include <QPushButton>
#include <QList>
#include <QPair>

class ScriptLogViewer : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptLogViewer(QWidget *parent = nullptr);
    ~ScriptLogViewer();

    void addLogMessage(const QString &level, const QString &message);
    void clearLog();
    void setScriptName(const QString &name);

public slots:
    void exportLog();

private:
    void setupUI();
    QString formatLogMessage(const QString &level, const QString &message) const;
    QString getLevelColor(const QString &level) const;

    QTextEdit *m_logText;
    QPushButton *m_clearButton;
    QPushButton *m_exportButton;
    QPushButton *m_closeButton;

    QList<QPair<QString, QString>> m_logMessages;  // level, message
    QString m_scriptName;
};

#endif // SCRIPTLOGVIEWER_H
