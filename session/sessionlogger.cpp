#include "sessionlogger.h"
#include <QDir>
#include <QDebug>
#include <QRegularExpression>

SessionLogger::SessionLogger(const QString &sessionId, QObject *parent)
    : QObject(parent)
    , m_sessionId(sessionId)
    , m_isLogging(false)
{
    m_logDir = QDir::homePath() + "/.nterminal/logs";
    QDir().mkpath(m_logDir);
}

SessionLogger::~SessionLogger()
{
    stopLogging();
}

bool SessionLogger::startLogging(const QString &sessionName, const QString &logDir)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_isLogging) {
        stopLogging();
    }
    
    m_sessionName = sessionName;
    if (!logDir.isEmpty()) {
        m_logDir = logDir;
        QDir().mkpath(m_logDir);
    }
    
    if (!openLogFile()) {
        qWarning() << "[SessionLogger] Failed to open log file";
        return false;
    }
    
    m_isLogging = true;
    logMessage("INFO", QString("Session started: %1").arg(sessionName));
    qDebug() << "[SessionLogger] Logging started for session:" << sessionName;
    return true;
}

void SessionLogger::stopLogging()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_isLogging) {
        return;
    }
    
    logMessage("INFO", "Session ended");
    closeLogFile();
    m_isLogging = false;
    qDebug() << "[SessionLogger] Logging stopped";
}

bool SessionLogger::isLogging() const
{
    return m_isLogging;
}

QString SessionLogger::currentLogFile() const
{
    QMutexLocker locker(&m_mutex);
    return m_logFile.fileName();
}

void SessionLogger::logData(const QByteArray &data, bool isReceived)
{
    if (!m_isLogging || data.isEmpty()) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    QString direction = isReceived ? "RECV" : "SEND";
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    
    m_logStream << QString("[%1] [%2] %3 bytes\n").arg(timestamp, direction).arg(data.size());
    
    if (data.size() <= 256) {
        QString hexLine;
        QString asciiLine;
        
        for (int i = 0; i < data.size(); ++i) {
            unsigned char ch = static_cast<unsigned char>(data[i]);
            hexLine += QString("%1 ").arg(ch, 2, 16, QChar('0')).toUpper();
            
            if (ch >= 32 && ch <= 126) {
                asciiLine += QChar(ch);
            } else if (ch == '\r') {
                asciiLine += "\\r";
            } else if (ch == '\n') {
                asciiLine += "\\n";
            } else if (ch == '\t') {
                asciiLine += "\\t";
            } else if (ch == 0x1B) {
                asciiLine += "\\e";
            } else {
                asciiLine += '.';
            }
            
            if ((i + 1) % 16 == 0 || i == data.size() - 1) {
                m_logStream << QString("  %1 | %2\n").arg(hexLine.leftJustified(48, ' '), asciiLine);
                hexLine.clear();
                asciiLine.clear();
            }
        }
    }
    
    m_logStream.flush();
}

void SessionLogger::logConnectionEvent(const QString &event, const QString &details)
{
    QString message = QString("Connection: %1").arg(event);
    if (!details.isEmpty()) {
        message += QString(" (%1)").arg(details);
    }
    logMessage("INFO", message);
}

void SessionLogger::logMessage(const QString &level, const QString &message)
{
    if (!m_isLogging || !m_logFile.isOpen()) {
        return;
    }
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    m_logStream << QString("[%1] [%2] %3\n").arg(timestamp, level, message);
    m_logStream.flush();
}

bool SessionLogger::openLogFile()
{
    QString fileName = generateLogFileName();
    m_logFile.setFileName(fileName);
    
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "[SessionLogger] Cannot open log file:" << fileName;
        return false;
    }
    
    m_logStream.setDevice(&m_logFile);
    m_logStream.setEncoding(QStringConverter::Utf8);
    
    m_logStream << "# NTerminal Session Log\n";
    m_logStream << QString("# Session: %1\n").arg(m_sessionName);
    m_logStream << QString("# Started: %1\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    m_logStream << "#\n";
    m_logStream.flush();
    
    return true;
}

void SessionLogger::closeLogFile()
{
    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }
}

QString SessionLogger::generateLogFileName() const
{
    QString safeName = m_sessionName;
    safeName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString fileName = QString("%1_%2_%3.log").arg(safeName, m_sessionId, timestamp);
    
    return QDir(m_logDir).filePath(fileName);
}
