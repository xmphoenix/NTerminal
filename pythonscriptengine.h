#ifndef PYTHONSCRIPTENGINE_H
#define PYTHONSCRIPTENGINE_H

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>
#include <QString>

class PythonScriptEngine : public QObject
{
    Q_OBJECT

public:
    explicit PythonScriptEngine(QObject *parent = nullptr);
    ~PythonScriptEngine();

    // 脚本管理
    bool loadScript(const QString &scriptPath);
    void stopScript();
    bool isRunning() const;
    QString currentScript() const { return m_scriptPath; }

    // 发送数据到脚本
    void sendLine(const QString &line);
    void sendEvent(const QString &eventType, const QJsonObject &data = QJsonObject());

signals:
    // 从脚本接收的命令
    void sendCommand(const QString &command);
    void logMessage(const QString &level, const QString &message);
    void scriptStarted();
    void scriptStopped(int exitCode);
    void scriptError(const QString &error);

private slots:
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onProcessStarted();

private:
    void sendMessage(const QJsonObject &message);
    void processMessage(const QJsonObject &message);
    QString findPythonExecutable();

    QProcess *m_process;
    QString m_scriptPath;
    QByteArray m_buffer;  // 用于处理不完整的 JSON 行
};

#endif // PYTHONSCRIPTENGINE_H
