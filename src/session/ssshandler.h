#ifndef SSHHANDLER_H
#define SSHHANDLER_H

#include "connectionhandler.h"
#include <libssh/libssh.h>
#include <QSocketNotifier>
#include <QByteArray>
#include <QElapsedTimer>
#include <QMap>
#include <QStringList>

class QTimer;

class SshHandler : public ConnectionHandler
{
    Q_OBJECT

public:
    explicit SshHandler(const Common::ConnectionSettings &settings, QObject *parent = nullptr);
    ~SshHandler();
    
    bool open() override;
    void close() override;
    bool isOpen() const override;
    bool isConnected() const override;
    void sendData(const QByteArray &data) override;
    QString connectionInfo() const override;
    void setTerminalSize(int columns, int rows) override;
    
private slots:
    void onSocketActivated(int socket);
    void readStatsData();

private:
    bool authenticate();
    bool setupChannel();
    void checkData();
    void cleanup();

    // 远端资源采集：登录后常驻一个 exec 通道，由远端循环每 3 秒输出一次快照。
    // 读取全部为非阻塞轮询，避免阻塞终端数据流。
    void setupStatsChannel();
    void closeStatsChannel();
    void handleStatsLine(const QString &line);
    void publishStats();

    ssh_session m_session;
    ssh_channel m_channel;
    ssh_channel m_statsChannel;
    Common::ConnectionSettings m_settings;
    QSocketNotifier *m_notifier;
    bool m_connected;
    int m_columns;
    int m_rows;

    QTimer *m_statsDrainTimer;
    QByteArray m_statsBuffer;
    QMap<QString, QStringList> m_statsSections;
    QString m_statsSection;
    QElapsedTimer m_statsClock;
    quint64 m_prevCpuTotal;
    quint64 m_prevCpuIdle;
    quint64 m_prevNetRx;
    quint64 m_prevNetTx;
    qint64 m_prevStatsMs;
};

#endif // SSHHANDLER_H
