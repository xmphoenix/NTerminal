#include "ui/updatedialog.h"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

UpdateDialog::UpdateDialog(QWidget *parent, const UpdateInfo &preloaded)
    : QDialog(parent)
    , m_manager(new UpdateManager(this))
    , m_info(preloaded)
    , m_preloaded(preloaded.valid)
{
    setupUi();
    setWindowTitle(tr("软件更新"));
    setModal(true);
    resize(580, 420);

    connect(m_manager, &UpdateManager::checking, this, [this] {
        m_statusLabel->setText(tr("正在检查更新…"));
        m_progress->setVisible(true);
        m_progress->setRange(0, 0);
        m_actionButton->setVisible(false);
        m_closeButton->setText(tr("取消"));
    });
    connect(m_manager, &UpdateManager::noUpdate, this, &UpdateDialog::onNoUpdate);
    connect(m_manager, &UpdateManager::updateAvailable, this,
            &UpdateDialog::onUpdateAvailable);
    connect(m_manager, &UpdateManager::downloadProgress, this,
            &UpdateDialog::onProgress);
    connect(m_manager, &UpdateManager::downloadFinished, this,
            &UpdateDialog::onDownloadFinished);
    connect(m_manager, &UpdateManager::error, this, &UpdateDialog::onError);
    connect(m_manager, &UpdateManager::cancelled, this, &UpdateDialog::onCancelled);

    connect(m_actionButton, &QPushButton::clicked, this, [this] {
        if (!m_info.valid) {
            beginCheck();
        } else {
            startDownload();
        }
    });
    connect(m_closeButton, &QPushButton::clicked, this, &QWidget::close);

    if (m_preloaded) {
        showAvailable(m_info);
    } else {
        beginCheck();
    }
}

UpdateDialog::~UpdateDialog()
{
    if (m_manager && m_manager->isBusy()) {
        m_manager->cancel();
    }
}

void UpdateDialog::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(12);

    QLabel *title = new QLabel(tr("软件更新"), this);
    title->setProperty("class", "sectionTitle");
    layout->addWidget(title);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);
    layout->addWidget(m_progress);

    m_notes = new QTextBrowser(this);
    m_notes->setOpenExternalLinks(true);
    m_notes->setVisible(false);
    m_notes->setMinimumHeight(150);
    layout->addWidget(m_notes, 1);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_actionButton = new QPushButton(tr("下载并安装"), this);
    m_actionButton->setProperty("class", "primary");
    m_actionButton->setVisible(false);
    m_closeButton = new QPushButton(tr("关闭"), this);
    buttons->addWidget(m_actionButton);
    buttons->addWidget(m_closeButton);
    layout->addLayout(buttons);
}

void UpdateDialog::beginCheck()
{
    m_info = UpdateInfo();
    m_progress->setVisible(true);
    m_progress->setRange(0, 0);
    m_notes->setVisible(false);
    m_actionButton->setVisible(false);
    m_closeButton->setText(tr("取消"));
    m_statusLabel->setText(tr("正在检查更新…"));
    m_manager->checkForUpdates();
}

void UpdateDialog::showAvailable(const UpdateInfo &info)
{
    m_info = info;
    m_progress->setVisible(false);
    m_actionButton->setVisible(true);
    m_actionButton->setText(tr("下载并安装"));
    m_closeButton->setText(tr("稍后"));

    QString published;
    if (!info.published.isEmpty()) {
        published = tr("发布时间：%1\n").arg(info.published);
    }
    m_statusLabel->setText(
        tr("发现新版本 %1（当前 %2）。\n%3"
           "下载并校验后，可选择立即重启安装，或下次启动时安装。")
            .arg(info.version, UpdateManager::currentVersion(), published));

    if (!info.notes.trimmed().isEmpty()) {
        m_notes->setMarkdown(info.notes);
        m_notes->setVisible(true);
    } else {
        m_notes->setVisible(false);
    }
}

void UpdateDialog::startDownload()
{
    if (!m_info.valid || m_manager->isBusy()) {
        return;
    }
    m_downloading = true;
    m_actionButton->setVisible(false);
    m_closeButton->setText(tr("取消"));
    m_progress->setVisible(true);
    m_progress->setRange(0, m_info.size > 0 ? int(m_info.size) : 0);
    m_progress->setValue(0);
    m_statusLabel->setText(tr("正在下载更新…"));
    m_manager->downloadUpdate(m_info);
}

void UpdateDialog::onNoUpdate(const QString &currentVersion)
{
    m_progress->setVisible(false);
    m_actionButton->setVisible(false);
    m_closeButton->setText(tr("关闭"));
    m_statusLabel->setText(tr("当前已是最新版本（%1）。").arg(currentVersion));
}

void UpdateDialog::onUpdateAvailable(const UpdateInfo &info)
{
    showAvailable(info);
}

void UpdateDialog::onProgress(qint64 received, qint64 total)
{
    if (!m_downloading) {
        return;
    }
    if (total > 0) {
        m_progress->setRange(0, int(total));
        m_progress->setValue(int(qMin(received, total)));
        m_statusLabel->setText(tr("正在下载更新… %1 / %2")
                                   .arg(formatBytes(received), formatBytes(total)));
    } else {
        m_progress->setRange(0, 0);
        m_statusLabel->setText(tr("正在下载更新… %1").arg(formatBytes(received)));
    }
}

void UpdateDialog::onDownloadFinished(const QString &zipPath,
                                      const UpdateInfo &info)
{
    m_downloading = false;
    m_progress->setVisible(false);
    m_statusLabel->setText(tr("下载完成，SHA-256 校验通过。"));
    m_actionButton->setVisible(false);
    m_closeButton->setText(tr("关闭"));

    QMessageBox box(QMessageBox::Question, tr("安装更新"),
                    tr("更新已下载并校验完成。\n\n是否立即重启并安装？"),
                    QMessageBox::NoButton, this);
    QAbstractButton *now = box.addButton(tr("立即重启"), QMessageBox::AcceptRole);
    QAbstractButton *later = box.addButton(tr("下次启动"),
                                           QMessageBox::ActionRole);
    box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == now) {
        installDownloaded(zipPath, info, true);
    } else if (box.clickedButton() == later) {
        installDownloaded(zipPath, info, false);
    }
}

void UpdateDialog::installDownloaded(const QString &zipPath,
                                     const UpdateInfo &info, bool restartNow)
{
    QString error;
    if (!m_manager->applyUpdate(zipPath, info, restartNow, &error)) {
        QMessageBox::critical(this, tr("安装更新失败"), error);
        m_statusLabel->setText(tr("安装准备失败，请重试。"));
        m_actionButton->setVisible(true);
        m_actionButton->setText(tr("重试"));
        return;
    }

    if (restartNow) {
        QMessageBox::information(
            this, tr("安装更新"),
            tr("NTerminal 即将退出，更新程序会在退出后替换文件并重新启动。"));
        accept();
        QCoreApplication::quit();
    } else {
        QMessageBox::information(
            this, tr("安装更新"),
            tr("更新已准备好，下次启动 NTerminal 时会自动安装并重启。"));
        accept();
    }
}

void UpdateDialog::onError(const QString &message)
{
    m_downloading = false;
    m_progress->setVisible(false);
    m_closeButton->setText(tr("关闭"));
    m_actionButton->setVisible(true);
    m_actionButton->setText(tr("重试"));
    m_statusLabel->setText(tr("操作失败：%1").arg(message));
}

void UpdateDialog::onCancelled()
{
    m_downloading = false;
    m_progress->setVisible(false);
    m_closeButton->setText(tr("关闭"));
    m_actionButton->setVisible(true);
    m_actionButton->setText(tr("重试"));
    m_statusLabel->setText(tr("操作已取消。"));
}

void UpdateDialog::closeEvent(QCloseEvent *event)
{
    if (m_manager && m_manager->isBusy()) {
        m_manager->cancel();
    }
    QDialog::closeEvent(event);
}

QString UpdateDialog::formatBytes(qint64 bytes)
{
    if (bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
    }
    if (bytes >= 1024) {
        return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}
