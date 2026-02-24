#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QString>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <QMutex>

class Logger : public QObject
{
    Q_OBJECT

public:
    enum LogLevel {
        Debug,
        Info,
        Warning,
        Error
    };

    explicit Logger(QObject *parent = nullptr);
    ~Logger();

    bool startLogging(const QString &sessionName, const QString &logDir = QString());
    void stopLogging();
    
    bool isLogging() const;
    QString getCurrentLogFile() const;
    
    void log(LogLevel level, const QString &message);
    void logData(const QByteArray &data, bool isReceived, const QString &source = QString());
    void logConnectionEvent(const QString &event, const QString &details = QString());
    
    static QString logLevelToString(LogLevel level);
    static QString formatTimestamp(const QDateTime &timestamp = QDateTime::currentDateTime());
    
signals:
    void logMessage(const QString &message);
    void loggingStarted(const QString &logFile);
    void loggingStopped();
    void errorOccurred(const QString &error);

public slots:
    void onDataReceived(const QByteArray &data);
    void onDataSent(const QByteArray &data);
    void onConnectionOpened();
    void onConnectionClosed();
    void onConnectionError(const QString &error);

private:
    bool openLogFile();
    void closeLogFile();
    void rotateLogFileIfNeeded();
    QString generateLogFileName(const QString &sessionName) const;
    
    QFile m_logFile;
    QTextStream m_logStream;
    mutable QMutex m_mutex;
    
    QString m_logDir;
    QString m_sessionName;
    qint64 m_maxFileSize;
    int m_maxBackupFiles;
    bool m_isLogging;
    
    static const qint64 DEFAULT_MAX_FILE_SIZE = 10 * 1024 * 1024; // 10 MB
    static const int DEFAULT_MAX_BACKUP_FILES = 5;
};

#endif // LOGGER_H