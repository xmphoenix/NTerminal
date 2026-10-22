#include "sessionlogger.h"
#include <QDir>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>

SessionLogger::SessionLogger(const QString &sessionId, QObject *parent)
    : QObject(parent)
    , m_sessionId(sessionId)
    , m_isLogging(false)
    , m_logFileNamePattern("%c_%n_%Y-%m-%d_%t.log")
    , m_overwriteExisting(false)
    , m_encoding("utf-8")
    , m_enableTimestamp(true)
    , m_timestampFormat("[%Y-%m-%d %h:%M:%s]")
    , m_ansiState(Ground)
    , m_inAlternateScreen(false)
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

    // 刷新行缓冲区中的剩余内容
    if (!m_lineBuffer.isEmpty()) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        QString line = m_lineBuffer.trimmed();
        if (!line.isEmpty()) {
            m_logStream << "[" << timestamp << "] " << line << "\n";
            m_logStream.flush();
        }
        m_lineBuffer.clear();
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
    // 只记录接收到的数据，不记录发送的数据
    if (!isReceived || !m_isLogging || data.isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_mutex);

    // 逐字符处理数据，使用行缓冲
    for (unsigned char ch : data) {
        switch (m_ansiState) {
        case Ground:
            if (ch == 0x1B) {  // ESC
                m_ansiState = Escape;
                m_csiParams.clear();
            } else if (!m_inAlternateScreen) {
                // 只在非备用屏幕模式下记录内容
                if (ch == '\n') {
                    // 遇到换行符，输出当前行（如果缓冲区不为空）
                    if (!m_lineBuffer.isEmpty()) {
                        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                        QString line = m_lineBuffer.trimmed();
                        if (!line.isEmpty()) {
                            m_logStream << "[" << timestamp << "] " << line << "\n";
                            m_logStream.flush();
                        }
                        m_lineBuffer.clear();
                    }
                } else if (ch == '\r') {
                    // \r 回车到行首
                    // 如果缓冲区不为空，输出当前行（top等程序用\r来更新显示）
                    if (!m_lineBuffer.isEmpty()) {
                        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                        QString line = m_lineBuffer.trimmed();
                        if (!line.isEmpty()) {
                            m_logStream << "[" << timestamp << "] " << line << "\n";
                        }
                        m_logStream.flush();
                        m_lineBuffer.clear();
                    }
                } else if (ch == '\t') {
                    m_lineBuffer.append('\t');
                } else if (ch == 0x07) {  // BEL
                    // 忽略响铃
                } else if (ch == 0x08) {  // Backspace
                    // 处理退格键
                    if (!m_lineBuffer.isEmpty()) {
                        m_lineBuffer.chop(1);
                    }
                } else if (ch >= 32 && ch <= 126) {
                    // 可打印字符
                    m_lineBuffer.append(QChar(ch));
                } else if (ch >= 128) {
                    // UTF-8 多字节字符
                    m_lineBuffer.append(QChar(ch));
                }
            }
            // 其他控制字符忽略
            break;

        case Escape:
            if (ch == '[') {
                m_ansiState = CSI;  // CSI 序列
                m_csiParams.clear();
            } else if (ch == ']') {
                m_ansiState = OSC;  // OSC 序列
            } else if (ch == '(' || ch == ')') {
                m_ansiState = Charset;  // 字符集选择
            } else if (ch >= 0x40 && ch <= 0x5F) {
                // Fe 序列（如 ESC D, ESC M 等），直接结束
                m_ansiState = Ground;
            } else {
                // 其他情况，返回 Ground
                m_ansiState = Ground;
            }
            break;

        case CSI:
            // CSI 序列：ESC [ 参数 中间字符 终止字符
            if (ch >= 0x40 && ch <= 0x7E) {
                // 终止字符，CSI 序列结束
                // 检查是否是备用屏幕切换命令
                if (ch == 'h' || ch == 'l') {
                    // 检查参数是否包含 ?1049 或 ?47（备用屏幕）
                    if (m_csiParams.contains("?1049") || m_csiParams.contains("?47")) {
                        m_inAlternateScreen = (ch == 'h');  // h=进入, l=退出
                        if (m_inAlternateScreen) {
                            // 进入备用屏幕前，先输出当前行缓冲区
                            if (!m_lineBuffer.isEmpty()) {
                                QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                                QString line = m_lineBuffer.trimmed();
                                if (!line.isEmpty()) {
                                    m_logStream << "[" << timestamp << "] " << line << "\n";
                                    m_logStream.flush();
                                }
                                m_lineBuffer.clear();
                            }
                        }
                    }
                }
                m_ansiState = Ground;
                m_csiParams.clear();
            } else if (ch >= 0x20 && ch <= 0x3F) {
                // 参数或中间字符，收集参数
                m_csiParams.append(QChar(ch));
            } else {
                // 非法字符，退出 CSI
                m_ansiState = Ground;
                m_csiParams.clear();
            }
            break;

        case OSC:
            // OSC 序列：ESC ] ... BEL 或 ESC ] ... ST
            if (ch == 0x07) {  // BEL
                m_ansiState = Ground;
            } else if (ch == 0x1B) {  // ESC（可能是 ST 结束）
                m_ansiState = Escape;
            }
            // 其他字符继续在 OSC 中
            break;

        case Charset:
            // 字符集选择序列：ESC ( X 或 ESC ) X
            // 直接忽略下一个字符并返回 Ground
            m_ansiState = Ground;
            break;
        }
    }
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
