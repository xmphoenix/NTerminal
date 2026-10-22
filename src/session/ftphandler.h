#ifndef FTPHANDLER_H
#define FTPHANDLER_H

#include "connectionhandler.h"
#include <QTcpSocket>
#include <QFile>
#include <QElapsedTimer>

class FtpHandler : public ConnectionHandler
{
    Q_OBJECT

public:
    explicit FtpHandler(const Common::ConnectionSettings &settings, QObject *parent = nullptr);
    ~FtpHandler();

    bool open() override;
    void close() override;
    bool isOpen() const override;
    bool isConnected() const override;
    void sendData(const QByteArray &data) override;
    QString connectionInfo() const override;
    void setTerminalSize(int columns, int rows) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    void onDataConnected();
    void onDataReadyRead();
    void onDataDisconnected();
    void onBytesWritten(qint64 bytes);

private:
    enum AuthState {
        AuthNone,
        AuthSentUser,
        AuthSentPass,
        AuthDone
    };

    enum TransferType { ASCII, Binary };

    void processResponse(const QString &response);
    void processFtpCommand(const QString &command);
    void sendCommand(const QString &command);
    bool handleLocalCommand(const QString &command);
    void showLocalHelp();
    void setupPassiveMode(const QString &response);
    QString formatSize(qint64 bytes) const;
    QString formatTransferSpeed(qint64 bytes, qint64 ms) const;

    // Command handlers
    void handleGetCommand(const QString &args);
    void handleMgetCommand(const QString &args);
    void handlePutCommand(const QString &args);
    void handleMputCommand(const QString &args);
    void handleAppendCommand(const QString &args);
    void handleRenameCommand(const QString &args);
    void handleChmodCommand(const QString &args);
    void handleUserCommand(const QString &args);
    void processNextPendingFile();
    QStringList expandLocalFiles(const QString &pattern);

    QTcpSocket *m_socket;
    QTcpSocket *m_dataSocket;
    Common::ConnectionSettings m_settings;
    QString m_receiveBuffer;
    QString m_commandBuffer;
    bool m_authenticated;
    AuthState m_authState;
    QString m_localDir;
    QString m_pendingCommand;
    QByteArray m_dataBuffer;
    bool m_passiveMode;
    QString m_localFilePath;
    QFile *m_localFile;
    bool m_isDownloading;
    bool m_isListing;
    qint64 m_transferBytes;
    qint64 m_totalBytes;
    QElapsedTimer m_transferTimer;
    TransferType m_transferType;
    QString m_renameFrom;
    QStringList m_pendingFiles;
    bool m_isMultiTransfer;
    bool m_promptMode;
    bool m_verboseMode;
    bool m_hashMode;
    qint64 m_hashBlockSize;
};

#endif // FTPHANDLER_H