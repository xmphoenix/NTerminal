#include "ui/transferdialog.h"
#include "transfer/filetransfer.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
    }
    if (bytes >= 1024) {
        return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

QString formatSpeed(double bytesPerSecond)
{
    if (bytesPerSecond >= 1024 * 1024) {
        return QStringLiteral("%1 MB/s").arg(bytesPerSecond / (1024.0 * 1024.0), 0, 'f', 2);
    }
    if (bytesPerSecond >= 1024) {
        return QStringLiteral("%1 KB/s").arg(bytesPerSecond / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B/s").arg(bytesPerSecond, 0, 'f', 0);
}

} // namespace

TransferDialog::TransferDialog(FileTransfer *transfer, QWidget *parent)
    : QDialog(parent)
    , m_transfer(transfer)
    , m_speedTimer(new QTimer(this))
{
    setWindowTitle(QStringLiteral("%1 %2 - %3")
                       .arg(transfer->protocolName(), transfer->directionName(),
                            transfer->fileSummary()));
    setModal(true);
    resize(560, 360);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 12);
    layout->setSpacing(10);

    QLabel *title = new QLabel(QStringLiteral("%1 %2")
                                   .arg(transfer->protocolName(), transfer->directionName()),
                               this);
    title->setProperty("class", "sectionTitle");
    layout->addWidget(title);

    QLabel *files = new QLabel(transfer->fileSummary(), this);
    files->setProperty("class", "hint");
    files->setWordWrap(true);
    layout->addWidget(files);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);   // 大小未知时显示忙碌状态
    m_progress->setTextVisible(false);
    layout->addWidget(m_progress);

    m_status = new QLabel(transfer->statusText(), this);
    m_status->setObjectName(QStringLiteral("transferStatus"));
    layout->addWidget(m_status);

    m_details = new QLabel(this);
    m_details->setProperty("class", "hint");
    layout->addWidget(m_details);

    m_log = new QPlainTextEdit(this);
    m_log->setObjectName(QStringLiteral("transferLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(300);
    m_log->setPlaceholderText(tr("协议日志"));
    layout->addWidget(m_log, 1);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch();
    const bool asciiReceive = transfer->kind() == FileTransfer::Kind::Ascii
                              && transfer->direction() == FileTransfer::Direction::Receive;
    m_cancelButton = new QPushButton(asciiReceive ? tr("停止并保存") : tr("取消"), this);
    m_cancelButton->setProperty("class", "ghost");
    buttons->addWidget(m_cancelButton);
    layout->addLayout(buttons);

    connect(m_cancelButton, &QPushButton::clicked, this, [this] {
        if (m_transfer && !m_transfer->isFinished()) {
            m_transfer->cancel();
        } else {
            accept();
        }
    });

    connect(transfer, &FileTransfer::progress, this,
            [this](qint64 done, qint64 total) {
                if (total > 0) {
                    if (m_progress->maximum() != int(total)) {
                        m_progress->setRange(0, int(total));
                    }
                    m_progress->setValue(int(qMin(done, total)));
                }
                updateDetails();
            });
    connect(transfer, &FileTransfer::statusChanged, this,
            [this](const QString &text) { m_status->setText(text); });
    connect(transfer, &FileTransfer::logMessage, this,
            [this](const QString &line) { appendLog(line); });
    connect(transfer, &FileTransfer::finished, this, &TransferDialog::onFinished);

    m_clock.start();
    m_lastTickMs = 0;
    m_speedTimer->setInterval(500);
    connect(m_speedTimer, &QTimer::timeout, this, [this] {
        const qint64 now = m_clock.elapsed();
        const qint64 bytes = m_transfer ? m_transfer->transferred() : 0;
        const qint64 deltaMs = now - m_lastTickMs;
        if (deltaMs > 0) {
            const double instant = double(bytes - m_lastBytes) * 1000.0 / double(deltaMs);
            m_speed = m_speed <= 0 ? instant : (m_speed * 0.6 + instant * 0.4);
        }
        m_lastBytes = bytes;
        m_lastTickMs = now;
        updateDetails();
    });
    m_speedTimer->start();

    updateDetails();
    appendLog(tr("协议：%1 · %2").arg(transfer->protocolName(),
                                     transfer->directionName()));
}

void TransferDialog::updateDetails()
{
    if (!m_transfer) {
        return;
    }
    const qint64 done = m_transfer->transferred();
    const qint64 total = m_transfer->total();
    QString text = tr("已传输 %1").arg(formatBytes(done));
    if (total > 0) {
        text += tr(" / %1（%2%）")
                    .arg(formatBytes(total))
                    .arg(qMin(100, int(done * 100 / qMax<qint64>(1, total))));
    }
    if (m_speed > 0 && !m_transfer->isFinished()) {
        text += QStringLiteral(" · ") + formatSpeed(m_speed);
    }
    const qint64 seconds = m_clock.elapsed() / 1000;
    if (seconds > 0) {
        text += tr(" · 用时 %1s").arg(seconds);
    }
    m_details->setText(text);
}

void TransferDialog::appendLog(const QString &line)
{
    m_log->appendPlainText(line);
}

void TransferDialog::onFinished(bool success, const QString &message)
{
    m_speedTimer->stop();
    m_status->setText(message);
    m_status->setProperty("state", success ? QStringLiteral("ok") : QStringLiteral("fail"));
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);

    if (success) {
        m_progress->setRange(0, 1);
        m_progress->setValue(1);
    } else {
        m_progress->setRange(0, 1);
        m_progress->setValue(0);
    }
    m_cancelButton->setText(tr("关闭"));
    updateDetails();
    appendLog(message);

    if (success) {
        QTimer::singleShot(600, this, &QDialog::accept);
    }
}

void TransferDialog::reject()
{
    if (m_transfer && !m_transfer->isFinished()) {
        m_transfer->cancel();
    }
    QDialog::reject();
}
