#include "sessionlogger.h"
#include <QDir>
#include <QRegularExpression>
#include <QStringConverter>

SessionLogger::SessionLogger(const QString &sessionId, QObject *parent)
    : QObject(parent)
    , m_sessionId(sessionId)
    , m_isLogging(false)
    , m_logFileNamePattern("%c_%n_%Y-%m-%d_%t.log")
    , m_overwriteExisting(false)
    , m_encoding("utf-8")
    , m_enableTimestamp(true)
    , m_timestampFormat("[%Y-%m-%d %h:%M:%s]")
{
    loadSettings();

    m_logDir = QDir::homePath() + "/.nterminal/logs";
    QDir().mkpath(m_logDir);
}

SessionLogger::~SessionLogger()
{
    stopLogging();
}

bool SessionLogger::startLogging(const QString &sessionName, const QString &logDir,
                                   const QString &connectionType, const QString &host)
{
    QMutexLocker locker(&m_mutex);

    if (m_isLogging) {
        stopLogging();
    }

    m_sessionName = sessionName;
    m_connectionType = connectionType;
    m_host = host;

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
    QString timestamp = formatTimestamp();

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

    QString timestamp = formatTimestamp();
    m_logStream << QString("[%1] [%2] %3\n").arg(timestamp, level, message);
    m_logStream.flush();
}

QString SessionLogger::formatTimestamp() const
{
    if (!m_enableTimestamp) {
        return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    }

    QString format = m_timestampFormat;
    format.replace("[", "").replace("]", "");

    QDateTime now = QDateTime::currentDateTime();
    return now.toString(format
        .replace("%Y", "yyyy")
        .replace("%m", "MM")
        .replace("%d", "dd")
        .replace("%h", "HH")
        .replace("%M", "mm")
        .replace("%s", "ss")
        .replace("%N", "zzz")
        .replace("%a", "ddd"));
}

bool SessionLogger::openLogFile()
{
    QString fileName = generateLogFileName();

    if (m_overwriteExisting && QFile::exists(fileName)) {
        QFile::remove(fileName);
    }

    m_logFile.setFileName(fileName);

    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "[SessionLogger] Cannot open log file:" << fileName;
        return false;
    }

    m_logStream.setDevice(&m_logFile);

    auto encoding = QStringConverter::encodingForName(m_encoding.toUtf8());
    if (encoding.has_value()) {
        m_logStream.setEncoding(encoding.value());
    } else {
        m_logStream.setEncoding(QStringConverter::Utf8);
    }

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
    if (safeName.isEmpty()) {
        safeName = "Session";
    }

    QString fileName = m_logFileNamePattern;
    if (fileName.isEmpty()) {
        fileName = "%c_%n_%Y-%m-%d_%t.log";
    }

    QDateTime now = QDateTime::currentDateTime();

    QString connectionType = m_connectionType;
    if (connectionType.isEmpty()) {
        connectionType = "ssh";
    } else {
        connectionType = connectionType.toLower();
    }

    QString timePart = QString("%1_%2_%3")
        .arg(now.toString("HH"))
        .arg(now.toString("mm"))
        .arg(now.toString("ss"));

    fileName.replace("%t", timePart);
    fileName.replace("%c", connectionType);
    fileName.replace("%n", safeName);
    fileName.replace("%Y", now.toString("yyyy"));
    fileName.replace("%m", now.toString("MM"));
    fileName.replace("%d", now.toString("dd"));

    return QDir(m_logDir).filePath(fileName);
}

void SessionLogger::loadSettings()
{
    QSettings settings("NTerminal", "NTerminal");

    m_logFileNamePattern = settings.value("log/fileNamePattern", "%c_%n_%Y-%m-%d_%t.log").toString();
    m_overwriteExisting = settings.value("log/overwrite", false).toBool();
    m_encoding = settings.value("log/encoding", "utf-8").toString();
    m_enableTimestamp = settings.value("log/enableTimestamp", true).toBool();
    m_timestampFormat = settings.value("log/timestampFormat", "[%Y-%m-%d %h:%M:%s]").toString();
}
