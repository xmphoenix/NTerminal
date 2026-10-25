#ifndef FILEMANAGERDIALOG_H
#define FILEMANAGERDIALOG_H

#include <QDialog>
#include <QElapsedTimer>
#include <QStringList>
#include <QVector>

#include "common/connectionsettings.h"
#include "files/remotefileentry.h"

class QCloseEvent;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTreeView;
class RemoteFileSession;

// ============================================================================
// SFTP / FTP / SCP 图形化双栏文件管理器
//
// 以主窗口标签页的形式显示（窗口标志为 Qt::Widget），不再作为独立弹窗。
// 左侧本地文件（QFileSystemModel），右侧远端目录（QTableWidget）；
// 支持前进/后退、拖拽上传下载、新建目录、删除（递归）、重命名、
// 实时进度与取消。
// ============================================================================
class FileManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FileManagerDialog(const Common::ConnectionSettings &settings,
                               const QString &sessionName = QString(),
                               QWidget *parent = nullptr);
    ~FileManagerDialog() override;

    // 标签页标题（例如 "SFTP 文件管理器 - 主机"）
    QString tabTitle() const;
    bool isConnected() const;
    // 标签条按协议着色用（SFTP / FTP / SCP）
    Common::ConnectionType connectionType() const { return m_settings.type; }
    // 请求关闭：正在传输时先确认并取消，完成后发出 closeRequested
    void requestClose();
    // 主窗口销毁前主动停止后台会话
    void stopSession();

signals:
    void closeRequested();
    void connectionStateChanged(bool connected);

protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    void setupUi();
    void connectSession();
    void navigateLocal(const QString &path, bool recordHistory = true);
    void navigateRemote(const QString &path, bool recordHistory = true);
    void goLocalBack();
    void goLocalForward();
    void goLocalRoot();
    void goLocalHome();
    void goRemoteBack();
    void goRemoteForward();
    void goRemoteRoot();
    void goRemoteHome();
    void goRemoteHistory(int index);
    void refreshLocal();
    void refreshRemote();
    void populateRemote(const QVector<RemoteFileEntry> &entries);
    void uploadSelected();
    void downloadSelected();
    void uploadPaths(const QStringList &paths, const QString &remoteDirectory);
    void downloadItems(const QVector<RemoteTransferItem> &items,
                       const QString &localDirectory);
    void makeRemoteDirectory();
    void removeSelected();
    void renameSelected();
    void updateButtonStates();
    void updateNavigationButtons();
    void setBusy(bool busy);
    void setStatus(const QString &text, bool error = false);
    QVector<RemoteTransferItem> selectedRemoteItems() const;
    QStringList selectedLocalPaths() const;
    QString remoteParent(const QString &path) const;
    QString remoteTargetDirectory(int row) const;
    QString localRootPath() const;
    QString joinRemote(const QString &directory, const QString &name) const;
    QString protocolLabel() const;
    static QString formatBytes(qint64 bytes);

    void onConnected(const QString &home, bool supportsDirectories);
    void onConnectionFailed(const QString &error);
    void onDirectoryListed(const QString &path,
                           const QVector<RemoteFileEntry> &entries);
    void onListFailed(const QString &path, const QString &error);
    void onTransferStarted(const QString &name, qint64 total);
    void onTransferProgress(qint64 done, qint64 total);
    void onTransferFinished(const QString &name, bool success,
                            const QString &message);
    void onOperationFinished(const QString &operation, bool success,
                             const QString &message);

    Common::ConnectionSettings m_settings;
    QString m_sessionName;
    RemoteFileSession *m_session = nullptr;
    bool m_connected = false;
    bool m_directorySupport = true;
    bool m_busy = false;
    bool m_closePending = false;
    QString m_title;
    QString m_localPath;
    QString m_remotePath;
    QString m_remoteHome;
    QString m_requestedRemotePath;   // 最近一次请求的目录，用于丢弃迟到的列表响应
    QString m_currentTransferName;
    QElapsedTimer m_transferTimer;   // 当前文件传输计时，用于计算实时速度

    // 双栏各自的前进/后退历史
    QStringList m_localHistory;
    QStringList m_remoteHistory;
    int m_localHistoryIndex = -1;
    int m_remoteHistoryIndex = -1;
    bool m_remoteNavPending = false;   // 列表请求成功后才写入历史，避免记录失败路径
    int m_remoteHistoryTarget = -1;    // >=0：跳到既有历史项；-1：追加新路径

    QLabel *m_connectionLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLineEdit *m_localPathEdit = nullptr;
    QLineEdit *m_remotePathEdit = nullptr;
    QTreeView *m_localView = nullptr;
    QFileSystemModel *m_localModel = nullptr;
    QTableWidget *m_remoteTable = nullptr;
    QPushButton *m_localBackButton = nullptr;
    QPushButton *m_localForwardButton = nullptr;
    QPushButton *m_localUpButton = nullptr;
    QPushButton *m_localRootButton = nullptr;
    QPushButton *m_localHomeButton = nullptr;
    QPushButton *m_localRefreshButton = nullptr;
    QPushButton *m_remoteBackButton = nullptr;
    QPushButton *m_remoteForwardButton = nullptr;
    QPushButton *m_remoteUpButton = nullptr;
    QPushButton *m_remoteRootButton = nullptr;
    QPushButton *m_remoteHomeButton = nullptr;
    QPushButton *m_remoteRefreshButton = nullptr;
    QPushButton *m_uploadButton = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QPushButton *m_mkdirButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_renameButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QVector<RemoteFileEntry> m_entries;
};

#endif // FILEMANAGERDIALOG_H
