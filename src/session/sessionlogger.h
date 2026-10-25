#ifndef SESSIONLOGGER_H
#define SESSIONLOGGER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QSettings>

class SessionLogger : public QObject
{
    Q_OBJECT

public:
    // 日志选项：默认构造从 QSettings 读取（应用路径），
    // 显式传入时不再访问全局设置（便于测试/嵌入使用）。
    struct LogOptions {
        QString fileNamePattern = QStringLiteral("%c_%n_%Y-%m-%d_%t.log");
        bool overwriteExisting = false;
        QString encoding = QStringLiteral("utf-8");
        bool enableTimestamp = true;
        QString timestampFormat = QStringLiteral("[%Y-%m-%d %h:%M:%s]");
    };

    explicit SessionLogger(const QString &sessionId, QObject *parent = nullptr);
    SessionLogger(const QString &sessionId, const LogOptions &options,
                  QObject *parent = nullptr);
    ~SessionLogger();

    bool startLogging(const QString &sessionName, const QString &logDir = QString(), 
                      const QString &connectionType = QString(), const QString &host = QString());
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
    QString formatTimestamp() const;
    void loadSettings();

    // 以下方法要求调用方已经持有 m_mutex，避免非递归互斥锁自锁
    void stopLoggingLocked();
    void logMessageLocked(const QString &level, const QString &message);
    void flushLineLocked();

    QString m_sessionId;
    QString m_sessionName;
    QString m_logDir;
    QFile m_logFile;
    QTextStream m_logStream;
    mutable QMutex m_mutex;
    bool m_isLogging;

    // Configuration options
    QString m_logFileNamePattern;
    bool m_overwriteExisting;
    QString m_encoding;
    bool m_enableTimestamp;
    QString m_timestampFormat;
    QString m_connectionType;
    QString m_host;

    // Line buffering for proper terminal output（保存原始字节，输出时按 UTF-8 解码）
    QByteArray m_lineBuffer;
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
};

#endif // SESSIONLOGGER_H
