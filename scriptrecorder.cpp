#include "scriptrecorder.h"
#include <QTextStream>
#include <QDir>
#include <QDebug>
#include <QStringConverter>

const QString ScriptRecorder::SCRIPT_FILE_EXTENSION = "ntscript";

ScriptRecorder::ScriptRecorder(QObject *parent)
    : QObject(parent)
    , m_isRecording(false)
    , m_isPlaying(false)
{
    m_scriptDir = QDir::homePath() + "/.nterminal/scripts";
    QDir().mkpath(m_scriptDir);
}

ScriptRecorder::~ScriptRecorder()
{
    stopRecording();
    stopPlayback();
}

bool ScriptRecorder::startRecording(const QString &sessionName, const QString &scriptDir)
{
    if (m_isRecording || m_isPlaying) {
        return false;
    }
    
    m_sessionName = sessionName;
    if (!scriptDir.isEmpty()) {
        m_scriptDir = scriptDir;
    }
    
    QDir().mkpath(m_scriptDir);
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString fileName = QString("%1/%2_%3.%4").arg(m_scriptDir, sessionName, timestamp, SCRIPT_FILE_EXTENSION);
    
    m_scriptFile.setFileName(fileName);
    if (!m_scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "无法打开脚本文件:" << fileName;
        return false;
    }
    
    m_scriptEntries.clear();
    m_recordingTimer.start();
    m_isRecording = true;
    
    QTextStream stream(&m_scriptFile);
    stream.setEncoding(QStringConverter::Utf8);
    writeScriptHeader(stream);
    
    emit recordingStarted(fileName);
    qDebug() << "开始录制脚本:" << fileName;
    return true;
}

void ScriptRecorder::stopRecording()
{
    if (!m_isRecording) {
        return;
    }
    
    if (m_scriptFile.isOpen()) {
        m_scriptFile.close();
    }
    
    m_isRecording = false;
    emit recordingStopped();
    qDebug() << "停止录制脚本";
}

bool ScriptRecorder::isRecording() const
{
    return m_isRecording;
}

bool ScriptRecorder::isPlaying() const
{
    return m_isPlaying;
}

bool ScriptRecorder::saveScript(const QString &fileName)
{
    // 简化实现：将内存中的脚本保存到文件
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    
    writeScriptHeader(stream);
    for (const ScriptEntry &entry : m_scriptEntries) {
        writeScriptEntry(stream, entry);
    }
    
    file.close();
    return true;
}

bool ScriptRecorder::loadScript(const QString &fileName)
{
    // 简化实现：从文件加载脚本到内存
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    m_scriptEntries.clear();
    
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    
    // 跳过文件头
    QString line;
    while (!stream.atEnd()) {
        line = stream.readLine();
        if (line.startsWith("# DATA")) {
            break;
        }
    }
    
    // 读取脚本条目
    while (!stream.atEnd()) {
        ScriptEntry entry = readScriptEntry(stream);
        if (!entry.data.isEmpty()) {
            m_scriptEntries.append(entry);
        }
    }
    
    file.close();
    return true;
}

void ScriptRecorder::recordData(const QByteArray &data, bool isSend)
{
    if (!m_isRecording || data.isEmpty()) {
        return;
    }
    
    ScriptEntry entry;
    entry.data = data;
    entry.timestamp = m_recordingTimer.elapsed();
    entry.isSend = isSend;
    
    m_scriptEntries.append(entry);
    
    // 同时写入文件
    if (m_scriptFile.isOpen()) {
        QTextStream stream(&m_scriptFile);
        writeScriptEntry(stream, entry);
    }
}

bool ScriptRecorder::playScript()
{
    if (m_isRecording || m_isPlaying || m_scriptEntries.isEmpty()) {
        return false;
    }
    
    m_isPlaying = true;
    emit playbackStarted();
    
    qDebug() << "开始回放脚本，条目数:" << m_scriptEntries.size();
    
    // 简化实现：立即发送所有数据，忽略时间戳
    // 实际实现应该使用定时器来保持时间间隔
    for (const ScriptEntry &entry : m_scriptEntries) {
        if (entry.isSend) {
            emit dataToSend(entry.data);
        }
    }
    
    m_isPlaying = false;
    emit playbackFinished();
    return true;
}

void ScriptRecorder::stopPlayback()
{
    if (m_isPlaying) {
        m_isPlaying = false;
        qDebug() << "停止脚本回放";
    }
}

QString ScriptRecorder::getCurrentScriptFile() const
{
    return m_scriptFile.fileName();
}

bool ScriptRecorder::writeScriptHeader(QTextStream &stream)
{
    stream << "# NTerminal 脚本文件\n";
    stream << "# 会话名称: " << m_sessionName << "\n";
    stream << "# 创建时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
    stream << "# 格式: 时间戳(ms) 方向(S/R) 数据长度 数据(十六进制)\n";
    stream << "# S: 发送, R: 接收\n";
    stream << "# DATA\n";
    return true;
}

bool ScriptRecorder::writeScriptEntry(QTextStream &stream, const ScriptEntry &entry)
{
    QString direction = entry.isSend ? "S" : "R";
    QString hexData = entry.data.toHex();
    
    stream << entry.timestamp << " " << direction << " " << entry.data.size() << " " << hexData << "\n";
    return true;
}

ScriptRecorder::ScriptEntry ScriptRecorder::readScriptEntry(QTextStream &stream)
{
    ScriptEntry entry;
    QString line = stream.readLine();
    
    if (line.isEmpty() || line.startsWith("#")) {
        return entry;
    }
    
    QStringList parts = line.split(" ");
    if (parts.size() < 4) {
        return entry;
    }
    
    bool ok;
    entry.timestamp = parts[0].toLongLong(&ok);
    if (!ok) return entry;
    
    entry.isSend = (parts[1] == "S");
    
    int dataSize = parts[2].toInt(&ok);
    if (!ok || dataSize <= 0) return entry;
    
    QString hexData = parts[3];
    entry.data = QByteArray::fromHex(hexData.toLatin1());
    
    return entry;
}