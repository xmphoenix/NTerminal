#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QString>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <QMutex>
#include <QSettings>

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
    QString formatTimestamp(const QDateTime &timestamp = QDateTime()) const;

    // Configuration methods
    void loadSettings();
    void saveSettings();

    void setLogPath(const QString &path);
    QString logPath() const;

    void setLogFileNamePattern(const QString &pattern);
    QString logFileNamePattern() const;

    void setOverwriteExisting(bool overwrite);
    bool overwriteExisting() const;

    void setStartLoggingOnConnect(bool start);
    bool startLoggingOnConnect() const;

    void setIncludeTerminalCodes(bool include);
    bool includeTerminalCodes() const;

    void setEncoding(const QString &encoding);
    QString encoding() const;

    void setEnableTimestamp(bool enabled);
    bool enableTimestamp() const;

    void setTimestampFormat(const QString &format);
    QString timestampFormat() const;

    void setLogLevel(LogLevel level);
    LogLevel logLevel() const;

    void setLogDataTraffic(bool enabled);
    bool logDataTraffic() const;

    void setMaxFileSize(qint64 size);
    qint64 maxFileSize() const;

    void setMaxBackupFiles(int count);
    int maxBackupFiles() const;

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

    // Configuration options
    QString m_logFileNamePattern;
    bool m_overwriteExisting;
    bool m_startLoggingOnConnect;
    bool m_includeTerminalCodes;
    QString m_encoding;
    bool m_enableTimestamp;
    QString m_timestampFormat;
    LogLevel m_logLevel;
    bool m_logDataTraffic;

    // Line buffering for proper terminal output
    QString m_lineBuffer;
    enum AnsiState {
        Ground,
        Escape,
        CSI,
        OSC,
        Charset
    };
    AnsiState m_ansiState;
    bool m_inAlternateScreen;  // 是否在备用屏幕模式（vim/less等全屏应用）
    QString m_csiParams;       // 用于收集 CSI 参数

    static const qint64 DEFAULT_MAX_FILE_SIZE = 10 * 1024 * 1024; // 10 MB
    static const int DEFAULT_MAX_BACKUP_FILES = 5;
};

#endif // LOGGER_H