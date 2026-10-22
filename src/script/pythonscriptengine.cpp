#include "script/pythonscriptengine.h"
#include <QDebug>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

PythonScriptEngine::PythonScriptEngine(QObject *parent)
    : QObject(parent)
    , m_process(nullptr)
{
}

PythonScriptEngine::~PythonScriptEngine()
{
    stopScript();
}

bool PythonScriptEngine::loadScript(const QString &scriptPath)
{
    // 检查脚本文件是否存在
    QFileInfo fileInfo(scriptPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        emit scriptError(QString("Script file not found: %1").arg(scriptPath));
        return false;
    }

    // 停止当前运行的脚本
    if (isRunning()) {
        stopScript();
    }

    m_scriptPath = scriptPath;

    // 查找 Python 可执行文件
    QString pythonExe = findPythonExecutable();
    if (pythonExe.isEmpty()) {
        emit scriptError("Python executable not found. Please install Python 3.x");
        return false;
    }

    // 创建进程
    m_process = new QProcess(this);
    m_process->setProgram(pythonExe);
    m_process->setArguments({"-u", scriptPath});  // -u: unbuffered output

    // 连接信号
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &PythonScriptEngine::onProcessReadyRead);
    connect(m_process, &QProcess::finished,
            this, &PythonScriptEngine::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &PythonScriptEngine::onProcessError);
    connect(m_process, &QProcess::started,
            this, &PythonScriptEngine::onProcessStarted);

    // 启动进程
    qDebug() << "[PythonScriptEngine] Starting script:" << scriptPath;
    qDebug() << "[PythonScriptEngine] Python executable:" << pythonExe;
    m_process->start();

    if (!m_process->waitForStarted(5000)) {
        emit scriptError("Failed to start Python process");
        delete m_process;
        m_process = nullptr;
        return false;
    }

    return true;
}

void PythonScriptEngine::stopScript()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        qDebug() << "[PythonScriptEngine] Stopping script";

        // 发送退出消息
        sendMessage({{"type", "exit"}});

        // 等待进程结束
        if (!m_process->waitForFinished(3000)) {
            qDebug() << "[PythonScriptEngine] Force terminating script";
            m_process->terminate();
            if (!m_process->waitForFinished(1000)) {
                m_process->kill();
            }
        }
    }

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_buffer.clear();
    m_scriptPath.clear();
}

bool PythonScriptEngine::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

void PythonScriptEngine::sendLine(const QString &line)
{
    if (!isRunning()) {
        return;
    }

    QJsonObject message;
    message["type"] = "line";
    message["data"] = line;
    message["timestamp"] = QDateTime::currentSecsSinceEpoch();

    sendMessage(message);
}

void PythonScriptEngine::sendEvent(const QString &eventType, const QJsonObject &data)
{
    if (!isRunning()) {
        return;
    }

    QJsonObject message;
    message["type"] = eventType;

    // 合并额外数据
    for (auto it = data.begin(); it != data.end(); ++it) {
        message[it.key()] = it.value();
    }

    sendMessage(message);
}

void PythonScriptEngine::onProcessReadyRead()
{
    if (!m_process) {
        return;
    }

    // 读取标准输出
    QByteArray data = m_process->readAllStandardOutput();
    m_buffer.append(data);

    // 处理完整的 JSON 行
    int newlineIndex;
    while ((newlineIndex = m_buffer.indexOf('\n')) != -1) {
        QByteArray line = m_buffer.left(newlineIndex);
        m_buffer.remove(0, newlineIndex + 1);

        // 解析 JSON
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(line, &error);

        if (error.error != QJsonParseError::NoError) {
            qDebug() << "[PythonScriptEngine] JSON parse error:" << error.errorString();
            qDebug() << "[PythonScriptEngine] Invalid JSON:" << line;
            continue;
        }

        if (!doc.isObject()) {
            qDebug() << "[PythonScriptEngine] Invalid message format (not an object)";
            continue;
        }

        processMessage(doc.object());
    }

    // 读取标准错误（用于调试）
    QByteArray errorData = m_process->readAllStandardError();
    if (!errorData.isEmpty()) {
        qDebug() << "[PythonScriptEngine] Script stderr:" << errorData;
    }
}

void PythonScriptEngine::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "[PythonScriptEngine] Script finished with exit code:" << exitCode;

    if (exitStatus == QProcess::CrashExit) {
        emit scriptError("Script crashed");
    }

    emit scriptStopped(exitCode);

    // 清理
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_buffer.clear();
}

void PythonScriptEngine::onProcessError(QProcess::ProcessError error)
{
    QString errorString;

    switch (error) {
        case QProcess::FailedToStart:
            errorString = "Failed to start Python process";
            break;
        case QProcess::Crashed:
            errorString = "Python process crashed";
            break;
        case QProcess::Timedout:
            errorString = "Python process timed out";
            break;
        case QProcess::WriteError:
            errorString = "Write error to Python process";
            break;
        case QProcess::ReadError:
            errorString = "Read error from Python process";
            break;
        default:
            errorString = "Unknown error";
            break;
    }

    qDebug() << "[PythonScriptEngine] Process error:" << errorString;
    emit scriptError(errorString);
}

void PythonScriptEngine::onProcessStarted()
{
    qDebug() << "[PythonScriptEngine] Script started successfully";
    emit scriptStarted();
}

void PythonScriptEngine::sendMessage(const QJsonObject &message)
{
    if (!isRunning()) {
        return;
    }

    QJsonDocument doc(message);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    data.append('\n');

    qint64 written = m_process->write(data);
    if (written != data.size()) {
        qDebug() << "[PythonScriptEngine] Failed to write complete message";
    }

    m_process->waitForBytesWritten(1000);
}

void PythonScriptEngine::processMessage(const QJsonObject &message)
{
    QString type = message["type"].toString();

    if (type == "send") {
        // 脚本请求发送命令到终端
        QString data = message["data"].toString();
        emit sendCommand(data);
    }
    else if (type == "log") {
        // 脚本记录日志
        QString level = message["level"].toString();
        QString msg = message["message"].toString();
        emit logMessage(level, msg);
    }
    else if (type == "exit") {
        // 脚本请求退出
        qDebug() << "[PythonScriptEngine] Script requested exit";
        stopScript();
    }
    else {
        qDebug() << "[PythonScriptEngine] Unknown message type:" << type;
    }
}

QString PythonScriptEngine::findPythonExecutable()
{
    // 尝试常见的 Python 可执行文件名
    QStringList pythonNames = {"python3", "python", "python.exe", "python3.exe"};

    // 首先在 PATH 中查找
    for (const QString &name : pythonNames) {
        QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty()) {
            qDebug() << "[PythonScriptEngine] Found Python at:" << path;
            return path;
        }
    }

    // Windows 常见安装路径
#ifdef Q_OS_WIN
    QStringList windowsPaths = {
        "C:/Python312/python.exe",
        "C:/Python311/python.exe",
        "C:/Python310/python.exe",
        "C:/Python39/python.exe",
        "C:/Python38/python.exe",
        "C:/Program Files/Python312/python.exe",
        "C:/Program Files/Python311/python.exe",
        "C:/Program Files/Python310/python.exe",
    };

    for (const QString &path : windowsPaths) {
        if (QFile::exists(path)) {
            qDebug() << "[PythonScriptEngine] Found Python at:" << path;
            return path;
        }
    }
#endif

    return QString();
}
