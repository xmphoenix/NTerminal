#include "logger.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QStringConverter>
#include <QRegularExpression>
#include <QStringList>

Logger::Logger(QObject *parent)
    : QObject(parent)
    , m_maxFileSize(DEFAULT_MAX_FILE_SIZE)
    , m_maxBackupFiles(DEFAULT_MAX_BACKUP_FILES)
    , m_isLogging(false)
    , m_logFileNamePattern("%c_%n_%Y-%m-%d_%t.log")
    , m_overwriteExisting(false)
    , m_startLoggingOnConnect(true)
    , m_includeTerminalCodes(false)
    , m_encoding("utf-8")
    , m_enableTimestamp(true)
    , m_timestampFormat("[%Y-%m-%d %h:%M:%s]")
    , m_logLevel(Info)
    , m_logDataTraffic(true)
{
    loadSettings();

    if (m_logDir.isEmpty()) {
        m_logDir = QDir::homePath() + "/.nterminal/logs";
    }
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

    QDir().mkpath(m_logDir);

    if (!openLogFile()) {
        emit errorOccurred("无法打开日志文件");
        return false;
    }

    m_isLogging = true;
    log(Info, QString("开始记录会话：%1").arg(sessionName));
    log(Info, QString("日志文件：%1").arg(m_logFile.fileName()));

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

    if (level < m_logLevel) {
        return;
    }

    QString logEntry;
    if (m_enableTimestamp) {
        QString timestamp = formatTimestamp();
        QString levelStr = logLevelToString(level);
        logEntry = QString("[%1] [%2] %3\n").arg(timestamp, levelStr, message);
    } else {
        QString levelStr = logLevelToString(level);
        logEntry = QString("[%1] %2\n").arg(levelStr, message);
    }

    m_logStream << logEntry;
    m_logStream.flush();

    emit logMessage(logEntry.trimmed());

    rotateLogFileIfNeeded();
}

void Logger::logData(const QByteArray &data, bool isReceived, const QString &source)
{
    // 只记录接收到的数据，不记录发送的数据
    if (!isReceived || data.isEmpty() || !m_isLogging || !m_logDataTraffic) {
        return;
    }

    QMutexLocker locker(&m_mutex);

    // 使用状态机处理 ANSI 转义序列
    enum State {
        Ground,           // 普通文本
        Escape,           // ESC 后
        CSI,              // ESC [ 后（CSI 序列）
        OSC,              // ESC ] 后（OSC 序列）
        Charset           // ESC ( 或 ESC ) 后（字符集选择）
    };

    static State state = Ground;
    QString text;

    for (unsigned char ch : data) {
        switch (state) {
        case Ground:
            if (ch == 0x1B) {  // ESC
                state = Escape;
            } else if (ch == '\n') {
                text.append('\n');
            } else if (ch == '\r') {
                // \r 只是回车到行首，不产生换行
                // 忽略 \r，只有 \n 才产生换行
                // 这样 \r\n 组合只产生一个换行（由 \n 产生）
            } else if (ch == '\t') {
                text.append('\t');
            } else if (ch == 0x07) {  // BEL
                // 忽略响铃
            } else if (ch >= 32 && ch <= 126) {
                text.append(QChar(ch));
            }
            // 其他控制字符忽略
            break;

        case Escape:
            if (ch == '[') {
                state = CSI;  // CSI 序列
            } else if (ch == ']') {
                state = OSC;  // OSC 序列
            } else if (ch == '(' || ch == ')') {
                state = Charset;  // 字符集选择
            } else if (ch >= 0x40 && ch <= 0x5F) {
                // Fe 序列（如 ESC D, ESC M 等），直接结束
                state = Ground;
            } else {
                // 其他情况，返回 Ground
                state = Ground;
            }
            break;

        case CSI:
            // CSI 序列：ESC [ 参数 中间字符 终止字符
            // 参数字符：0x30-0x3F (0-9 ; < = > ?)
            // 中间字符：0x20-0x2F (空格 ! " # $ % & ' ( ) * + , - . /)
            // 终止字符：0x40-0x7E (@ A-Z [ \ ] ^ _ ` a-z { | } ~)
            if (ch >= 0x40 && ch <= 0x7E) {
                // 终止字符，CSI 序列结束
                state = Ground;
            } else if (ch >= 0x20 && ch <= 0x3F) {
                // 参数或中间字符，继续
            } else {
                // 非法字符，退出 CSI
                state = Ground;
            }
            break;

        case OSC:
            // OSC 序列：ESC ] ... BEL 或 ESC ] ... ST
            if (ch == 0x07) {  // BEL
                state = Ground;
            } else if (ch == 0x1B) {  // ESC（可能是 ST 结束）
                state = Escape;
            }
            // 其他字符继续在 OSC 中
            break;

        case Charset:
            // 字符集选择序列：ESC ( X 或 ESC ) X
            // 直接忽略下一个字符并返回 Ground
            state = Ground;
            break;
        }
    }
    
    // 压缩连续的空行（最多保留一个空行）
    text.replace(QRegularExpression("\n{2,}"), "\n");
    
    // 移除每行末尾的空格
    QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        lines[i] = lines[i].trimmed();
    }
    text = lines.join('\n');
    

    // 只记录有实际内容的文本（包括只有换行符的情况）
    if (!text.isEmpty()) {
        m_logStream << text;
        m_logStream.flush();
    }
}

void Logger::logConnectionEvent(const QString &event, const QString &details)
{
    QString message = QString("连接事件：%1").arg(event);
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

QString Logger::formatTimestamp(const QDateTime &timestamp) const
{
    QDateTime dt = timestamp.isValid() ? timestamp : QDateTime::currentDateTime();

    if (!m_enableTimestamp) {
        return dt.toString("yyyy-MM-dd HH:mm:ss.zzz");
    }

    QString format = m_timestampFormat;
    format.replace("[", "").replace("]", "");

    QString qtFormat = format
        .replace("%Y", "yyyy")
        .replace("%m", "MM")
        .replace("%d", "dd")
        .replace("%h", "HH")
        .replace("%M", "mm")
        .replace("%s", "ss")
        .replace("%N", "zzz")
        .replace("%a", "ddd");

    return dt.toString(qtFormat);
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

    if (m_overwriteExisting && QFile::exists(fileName)) {
        QFile::remove(fileName);
    }

    m_logFile.setFileName(fileName);

    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "无法打开日志文件:" << fileName << "错误:" << m_logFile.errorString();
        return false;
    }

    m_logStream.setDevice(&m_logFile);

    auto encoding = QStringConverter::encodingForName(m_encoding.toUtf8());
    if (encoding.has_value()) {
        m_logStream.setEncoding(encoding.value());
    } else {
        m_logStream.setEncoding(QStringConverter::Utf8);
    }

    QString header = QString("# NTerminal 会话日志\n")
                    .append(QString("# 会话名称：%1\n").arg(m_sessionName))
                    .append(QString("# 开始时间：%1\n").arg(formatTimestamp()))
                    .append(QString("# 日志格式：[时间戳] [级别] 消息\n"))
                    .append("#\n");

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

    QString oldestBackup = QString("%1/%2.%3.%4").arg(path, baseName, QString::number(m_maxBackupFiles), suffix);
    if (QFile::exists(oldestBackup)) {
        QFile::remove(oldestBackup);
    }

    for (int i = m_maxBackupFiles - 1; i >= 1; --i) {
        QString oldName = QString("%1/%2.%3.%4").arg(path, baseName, QString::number(i), suffix);
        QString newName = QString("%1/%2.%3.%4").arg(path, baseName, QString::number(i + 1), suffix);

        if (QFile::exists(oldName)) {
            QFile::rename(oldName, newName);
        }
    }

    QString backup1 = QString("%1/%2.1.%3").arg(path, baseName, suffix);
    QFile::rename(currentFile, backup1);

    openLogFile();

    log(Info, QString("日志文件已轮转，新文件：%1").arg(m_logFile.fileName()));
}

QString Logger::generateLogFileName(const QString &sessionName) const
{
    QString safeName = sessionName;
    safeName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    QString fileName = m_logFileNamePattern;
    if (fileName.isEmpty()) {
        fileName = "%c_%n_%Y-%m-%d_%t.log";
    }

    QDateTime now = QDateTime::currentDateTime();

    QString connectionType = "ssh";
    QString timePart = QString("%1_%2_%3")
        .arg(now.toString("HH"))
        .arg(now.toString("mm"))
        .arg(now.toString("ss"));

    fileName.replace("%c", connectionType);
    fileName.replace("%n", safeName);
    fileName.replace("%Y", now.toString("yyyy"));
    fileName.replace("%m", now.toString("MM"));
    fileName.replace("%d", now.toString("dd"));
    fileName.replace("%t", timePart);

    return QDir(m_logDir).filePath(fileName);
}

void Logger::loadSettings()
{
    QSettings settings("NTerminal", "NTerminal");

    QString defaultPath = QDir::homePath() + "/.nterminal/logs";
    m_logDir = settings.value("log/path", defaultPath).toString();
    m_logFileNamePattern = settings.value("log/fileNamePattern", "%c_%n_%Y-%m-%d_%t.log").toString();
    m_overwriteExisting = settings.value("log/overwrite", false).toBool();
    m_startLoggingOnConnect = settings.value("log/startOnConnect", true).toBool();
    m_includeTerminalCodes = settings.value("log/includeTerminalCodes", false).toBool();
    m_encoding = settings.value("log/encoding", "utf-8").toString();
    m_enableTimestamp = settings.value("log/enableTimestamp", true).toBool();
    m_timestampFormat = settings.value("log/timestampFormat", "[%Y-%m-%d %h:%M:%s]").toString();
    m_logLevel = static_cast<LogLevel>(settings.value("log/level", Info).toInt());
    m_logDataTraffic = settings.value("log/logDataTraffic", true).toBool();
    m_maxFileSize = settings.value("log/maxFileSize", 10).toLongLong() * 1024 * 1024;
    m_maxBackupFiles = settings.value("log/maxBackupFiles", 5).toInt();
}

void Logger::saveSettings()
{
    QSettings settings("NTerminal", "NTerminal");

    settings.setValue("log/path", m_logDir);
    settings.setValue("log/fileNamePattern", m_logFileNamePattern);
    settings.setValue("log/overwrite", m_overwriteExisting);
    settings.setValue("log/startOnConnect", m_startLoggingOnConnect);
    settings.setValue("log/includeTerminalCodes", m_includeTerminalCodes);
    settings.setValue("log/encoding", m_encoding);
    settings.setValue("log/enableTimestamp", m_enableTimestamp);
    settings.setValue("log/timestampFormat", m_timestampFormat);
    settings.setValue("log/level", static_cast<int>(m_logLevel));
    settings.setValue("log/logDataTraffic", m_logDataTraffic);
    settings.setValue("log/maxFileSize", m_maxFileSize / 1024 / 1024);
    settings.setValue("log/maxBackupFiles", m_maxBackupFiles);
    settings.sync();
}

void Logger::setLogPath(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    m_logDir = path;
    QDir().mkpath(m_logDir);
}

QString Logger::logPath() const
{
    QMutexLocker locker(&m_mutex);
    return m_logDir;
}

void Logger::setLogFileNamePattern(const QString &pattern)
{
    QMutexLocker locker(&m_mutex);
    m_logFileNamePattern = pattern;
}

QString Logger::logFileNamePattern() const
{
    QMutexLocker locker(&m_mutex);
    return m_logFileNamePattern;
}

void Logger::setOverwriteExisting(bool overwrite)
{
    QMutexLocker locker(&m_mutex);
    m_overwriteExisting = overwrite;
}

bool Logger::overwriteExisting() const
{
    QMutexLocker locker(&m_mutex);
    return m_overwriteExisting;
}

void Logger::setStartLoggingOnConnect(bool start)
{
    QMutexLocker locker(&m_mutex);
    m_startLoggingOnConnect = start;
}

bool Logger::startLoggingOnConnect() const
{
    QMutexLocker locker(&m_mutex);
    return m_startLoggingOnConnect;
}

void Logger::setIncludeTerminalCodes(bool include)
{
    QMutexLocker locker(&m_mutex);
    m_includeTerminalCodes = include;
}

bool Logger::includeTerminalCodes() const
{
    QMutexLocker locker(&m_mutex);
    return m_includeTerminalCodes;
}

void Logger::setEncoding(const QString &encoding)
{
    QMutexLocker locker(&m_mutex);
    m_encoding = encoding;
}

QString Logger::encoding() const
{
    QMutexLocker locker(&m_mutex);
    return m_encoding;
}

void Logger::setEnableTimestamp(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_enableTimestamp = enabled;
}

bool Logger::enableTimestamp() const
{
    QMutexLocker locker(&m_mutex);
    return m_enableTimestamp;
}

void Logger::setTimestampFormat(const QString &format)
{
    QMutexLocker locker(&m_mutex);
    m_timestampFormat = format;
}

QString Logger::timestampFormat() const
{
    QMutexLocker locker(&m_mutex);
    return m_timestampFormat;
}

void Logger::setLogLevel(LogLevel level)
{
    QMutexLocker locker(&m_mutex);
    m_logLevel = level;
}

Logger::LogLevel Logger::logLevel() const
{
    QMutexLocker locker(&m_mutex);
    return m_logLevel;
}

void Logger::setLogDataTraffic(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_logDataTraffic = enabled;
}

bool Logger::logDataTraffic() const
{
    QMutexLocker locker(&m_mutex);
    return m_logDataTraffic;
}

void Logger::setMaxFileSize(qint64 size)
{
    QMutexLocker locker(&m_mutex);
    m_maxFileSize = size;
}

qint64 Logger::maxFileSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxFileSize;
}

void Logger::setMaxBackupFiles(int count)
{
    QMutexLocker locker(&m_mutex);
    m_maxBackupFiles = count;
}

int Logger::maxBackupFiles() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxBackupFiles;
}
