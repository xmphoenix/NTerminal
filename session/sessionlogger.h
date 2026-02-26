#ifndef SESSIONLOGGER_H
#define SESSIONLOGGER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <QString>

class SessionLogger : public QObject
{
    Q_OBJECT

public:
    explicit SessionLogger(const QString &sessionId, QObject *parent = nullptr);
    ~SessionLogger();
    
    bool startLogging(const QString &sessionName, const QString &logDir = QString());
    void stopLogging();
    bool isLogging() const;
    QString currentLogFile() const;
    
    void logData(const QByteArray &data, bool isReceived);
    void logConnectionEvent(const QString &event, const QString &details = QString());
    void logMessage(const QString &level, const QString &message);

private:
    bool openLogFile();
    void closeLogFile();
    QString generateLogFileName() const;
    
    QString m_sessionId;
    QString m_sessionName;
    QString m_logDir;
    QFile m_logFile;
    QTextStream m_logStream;
    mutable QMutex m_mutex;
    bool m_isLogging;
};

#endif // SESSIONLOGGER_H
