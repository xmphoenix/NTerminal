#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include <QDialog>
#include "update/updatemanager.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;
class QCloseEvent;

// ============================================================================
// 检查/下载/安装更新对话框
// ============================================================================
class UpdateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UpdateDialog(QWidget *parent = nullptr,
                          const UpdateInfo &preloaded = UpdateInfo());
    ~UpdateDialog() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupUi();
    void beginCheck();
    void showAvailable(const UpdateInfo &info);
    void startDownload();
    void onNoUpdate(const QString &currentVersion);
    void onUpdateAvailable(const UpdateInfo &info);
    void onProgress(qint64 received, qint64 total);
    void onDownloadFinished(const QString &zipPath, const UpdateInfo &info);
    void onError(const QString &message);
    void onCancelled();
    void installDownloaded(const QString &zipPath, const UpdateInfo &info,
                           bool restartNow);
    static QString formatBytes(qint64 bytes);

    UpdateManager *m_manager = nullptr;
    UpdateInfo m_info;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QTextBrowser *m_notes = nullptr;
    QPushButton *m_actionButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    bool m_downloading = false;
    bool m_preloaded = false;
};

#endif // UPDATEDIALOG_H
