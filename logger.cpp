#include "logger.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QStringConverter>
#include <QRegularExpression>

Logger::Logger(QObject *parent)
    : QObject(parent)
    , m_maxFileSize(DEFAULT_MAX_FILE_SIZE)
    , m_maxBackupFiles(DEFAULT_MAX_BACKUP_FILES)
    , m_isLogging(false)
{
    // 默认日志目录：用户目录下的 .nterminal/logs
    m_logDir = QDir::homePath() + "/.nterminal/logs";
    QDir().mkpath(m_logDir);
}

Logger::~Logger()
{
    stopLogging();
}

bool Logger::startLogging(const QString &sessionName, const QString &logDir)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isLogging) {
        stopLogging();
    }
    
    m_sessionName = sessionName;
    if (!logDir.isEmpty()) {
        m_logDir = logDir;
    }
    
    // 确保日志目录存在
    QDir().mkpath(m_logDir);
    
    if (!openLogFile()) {
        emit errorOccurred("无法打开日志文件");
        return false;
    }
    
    m_isLogging = true;
    
    // 记录会话开始信息
    log(Info, QString("开始记录会话: %1").arg(sessionName));
    log(Info, QString("日志文件: %1").arg(m_logFile.fileName()));
    
    emit loggingStarted(m_logFile.fileName());
    return true;
}

void Logger::stopLogging()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_isLogging) {
        return;
    }
    
    log(Info, "停止记录会话");
    closeLogFile();
    
    m_isLogging = false;
    emit loggingStopped();
}

bool Logger::isLogging() const
{
    return m_isLogging;
}

QString Logger::getCurrentLogFile() const
{
    QMutexLocker locker(&m_mutex);
    return m_isLogging ? m_logFile.fileName() : QString();
}

void Logger::log(LogLevel level, const QString &message)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_isLogging || !m_logFile.isOpen()) {
        return;
    }
    
    QString timestamp = formatTimestamp();
    QString levelStr = logLevelToString(level);
    QString logEntry = QString("[%1] [%2] %3\n").arg(timestamp, levelStr, message);
    
    m_logStream << logEntry;
    m_logStream.flush();
    
    emit logMessage(logEntry.trimmed());
    
    // 检查是否需要轮转日志文件
    rotateLogFileIfNeeded();
}

void Logger::logData(const QByteArray &data, bool isReceived, const QString &source)
{
    if (data.isEmpty() || !m_isLogging) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    QString direction = isReceived ? "RECV" : "SEND";
    QString sourceInfo = source.isEmpty() ? QString() : QString(" [%1]").arg(source);
    
    // 记录数据基本信息
    log(Debug, QString("%1%2: %3 字节").arg(direction, sourceInfo).arg(data.size()));
    
    // 如果数据较小，同时记录十六进制和ASCII表示
    if (data.size() <= 256) {
        QString hexString;
        QString asciiString;
        
        for (int i = 0; i < data.size(); ++i) {
            unsigned char ch = data[i];
            
            // 十六进制部分
            hexString.append(QString("%1 ").arg(ch, 2, 16, QLatin1Char('0')).toUpper());
            
            // ASCII部分
            if (ch >= 32 && ch <= 126) {
                asciiString.append(QChar(ch));
            } else if (ch == '\r') {
                asciiString.append("\\r");
            } else if (ch == '\n') {
                asciiString.append("\\n");
            } else if (ch == '\t') {
                asciiString.append("\\t");
            } else if (ch == 0x1B) {
                asciiString.append("\\e");
            } else {
                asciiString.append('.');
            }
            
            // 每16字节换行
            if ((i + 1) % 16 == 0 || i == data.size() - 1) {
                log(Debug, QString("  %1 | %2").arg(hexString.leftJustified(48, ' '), asciiString));
                hexString.clear();
                asciiString.clear();
            }
        }
    }
}

void Logger::logConnectionEvent(const QString &event, const QString &details)
{
    QString message = QString("连接事件: %1").arg(event);
    if (!details.isEmpty()) {
        message.append(QString(" (%1)").arg(details));
    }
    log(Info, message);
}

QString Logger::logLevelToString(LogLevel level)
{
    switch (level) {
        case Debug: return "DEBUG";
        case Info: return "INFO";
        case Warning: return "WARN";
        case Error: return "ERROR";
        default: return "UNKNOWN";
    }
}

QString Logger::formatTimestamp(const QDateTime &timestamp)
{
    return timestamp.toString("yyyy-MM-dd HH:mm:ss.zzz");
}

void Logger::onDataReceived(const QByteArray &data)
{
    logData(data, true, "终端");
}

void Logger::onDataSent(const QByteArray &data)
{
    logData(data, false, "终端");
}

void Logger::onConnectionOpened()
{
    logConnectionEvent("连接已打开");
}

void Logger::onConnectionClosed()
{
    logConnectionEvent("连接已关闭");
}

void Logger::onConnectionError(const QString &error)
{
    logConnectionEvent("连接错误", error);
}

bool Logger::openLogFile()
{
    QString fileName = generateLogFileName(m_sessionName);
    m_logFile.setFileName(fileName);
    
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "无法打开日志文件:" << fileName << "错误:" << m_logFile.errorString();
        return false;
    }
    
    m_logStream.setDevice(&m_logFile);
    m_logStream.setEncoding(QStringConverter::Utf8);
    
    // 写入文件头
    QString header = QString("# NTerminal 会话日志\n")
                    .append(QString("# 会话名称: %1\n").arg(m_sessionName))
                    .append(QString("# 开始时间: %1\n").arg(formatTimestamp()))
                    .append(QString("# 日志格式: [时间戳] [级别] 消息\n"))
                    .append(QString("#\n"));
    
    m_logStream << header;
    m_logStream.flush();
    
    return true;
}

void Logger::closeLogFile()
{
    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }
}

void Logger::rotateLogFileIfNeeded()
{
    if (m_logFile.size() < m_maxFileSize) {
        return;
    }
    
    closeLogFile();
    
    QString currentFile = m_logFile.fileName();
    QFileInfo fileInfo(currentFile);
    QString baseName = fileInfo.completeBaseName();
    QString suffix = fileInfo.suffix();
    QString path = fileInfo.path();
    
    // 删除最旧的备份文件
    QString oldestBackup = QString("%1/%2.%3.%4").arg(path, baseName, QString::number(m_maxBackupFiles), suffix);
    if (QFile::exists(oldestBackup)) {
        QFile::remove(oldestBackup);
    }
    
    // 重命名现有备份文件
    for (int i = m_maxBackupFiles - 1; i >= 1; --i) {
        QString oldName = QString("%1/%2.%3.%4").arg(path, baseName, QString::number(i), suffix);
        QString newName = QString("%1/%2.%3.%4").arg(path, baseName, QString::number(i + 1), suffix);
        
        if (QFile::exists(oldName)) {
            QFile::rename(oldName, newName);
        }
    }
    
    // 重命名当前文件为备份1
    QString backup1 = QString("%1/%2.1.%3").arg(path, baseName, suffix);
    QFile::rename(currentFile, backup1);
    
    // 重新打开日志文件
    openLogFile();
    
    log(Info, QString("日志文件已轮转，新文件: %1").arg(m_logFile.fileName()));
}

QString Logger::generateLogFileName(const QString &sessionName) const
{
    // 清理会话名称中的非法字符
    QString safeName = sessionName;
    safeName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString fileName = QString("%1_%2.log").arg(safeName, timestamp);
    
    return QDir(m_logDir).filePath(fileName);
}