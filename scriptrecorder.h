#ifndef SCRIPTRECORDER_H
#define SCRIPTRECORDER_H

#include <QObject>
#include <QVector>
#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>

class ScriptRecorder : public QObject
{
    Q_OBJECT

public:
    struct ScriptEntry {
        QByteArray data;
        qint64 timestamp; // 毫秒
        bool isSend;      // true: 发送, false: 接收
    };

    explicit ScriptRecorder(QObject *parent = nullptr);
    ~ScriptRecorder();

    bool startRecording(const QString &sessionName, const QString &scriptDir = QString());
    void stopRecording();
    
    bool isRecording() const;
    bool isPlaying() const;
    
    bool saveScript(const QString &fileName);
    bool loadScript(const QString &fileName);
    
    void recordData(const QByteArray &data, bool isSend);
    
    bool playScript();
    void stopPlayback();
    
    QString getCurrentScriptFile() const;

signals:
    void recordingStarted(const QString &scriptFile);
    void recordingStopped();
    void playbackStarted();
    void playbackFinished();
    void playbackError(const QString &error);
    void dataToSend(const QByteArray &data);

private:
    bool writeScriptHeader(QTextStream &stream);
    bool writeScriptEntry(QTextStream &stream, const ScriptEntry &entry);
    ScriptEntry readScriptEntry(QTextStream &stream);
    
    QVector<ScriptEntry> m_scriptEntries;
    QElapsedTimer m_recordingTimer;
    QFile m_scriptFile;
    
    QString m_scriptDir;
    QString m_sessionName;
    bool m_isRecording;
    bool m_isPlaying;
    
    static const QString SCRIPT_FILE_EXTENSION;
};

#endif // SCRIPTRECORDER_H