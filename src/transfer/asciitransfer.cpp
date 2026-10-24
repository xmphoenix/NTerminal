#include "transfer/asciitransfer.h"

#include <QFile>
#include <QTimer>

namespace {
constexpr int kChunkSize = 256;
constexpr int kChunkIntervalMs = 30;
} // namespace

AsciiTransfer::AsciiTransfer(Direction direction, const QStringList &paths,
                             QObject *parent)
    : FileTransfer(Kind::Ascii, direction, paths, parent)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(kChunkIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &AsciiTransfer::sendChunk);
}

AsciiTransfer::~AsciiTransfer()
{
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
}

void AsciiTransfer::start()
{
    if (direction() == Direction::Send) {
        startSend();
    } else {
        startReceive();
    }
}

void AsciiTransfer::startSend()
{
    if (paths().isEmpty()) {
        fail(tr("没有要发送的文件"));
        return;
    }
    m_file = new QFile(paths().first());
    if (!m_file->open(QIODevice::ReadOnly)) {
        fail(tr("无法打开文件：%1").arg(m_file->errorString()));
        return;
    }
    m_payload = m_file->readAll();
    m_file->close();
    // 文本传输：把本地行尾统一为 CRLF
    m_payload.replace("\r\n", "\n");
    m_payload.replace('\r', '\n');
    m_payload.replace("\n", "\r\n");
    m_offset = 0;
    m_total = m_payload.size();
    setProgress(0, m_total);
    setStatus(tr("正在发送文本…"));
    log(tr("ASCII 发送 %1（%2 字节，已转换为 CRLF）").arg(paths().first()).arg(m_total));
    // 初始延时，给对端进入接收状态的时间
    QTimer::singleShot(300, this, [this] {
        if (!m_finished) {
            m_timer->start();
        }
    });
}

void AsciiTransfer::startReceive()
{
    if (paths().isEmpty()) {
        fail(tr("没有指定保存文件"));
        return;
    }
    m_file = new QFile(paths().first());
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("无法创建文件：%1").arg(m_file->errorString()));
        return;
    }
    m_received = 0;
    setStatus(tr("正在接收（完成后点击“停止并保存”）"));
    log(tr("ASCII 接收保存到 %1").arg(paths().first()));
}

void AsciiTransfer::onData(const QByteArray &data)
{
    if (m_finished || direction() != Direction::Receive) {
        return;
    }
    if (m_file && m_file->isOpen()) {
        m_file->write(data);
        m_file->flush();
    }
    m_received += data.size();
    setProgress(m_received, -1);
}

void AsciiTransfer::sendChunk()
{
    if (m_finished) {
        return;
    }
    if (m_offset >= m_payload.size()) {
        m_timer->stop();
        setProgress(m_total, m_total);
        finish(true, tr("ASCII 发送完成：%1 字节").arg(m_total));
        return;
    }
    const int length = qMin(kChunkSize, int(m_payload.size() - m_offset));
    write(m_payload.mid(int(m_offset), length));
    m_offset += length;
    setProgress(m_offset, m_total);
}

void AsciiTransfer::finishReceive()
{
    if (m_file) {
        m_file->flush();
        m_file->close();
    }
    finish(true, tr("ASCII 接收已停止：保存 %1 字节").arg(m_received));
}

void AsciiTransfer::cancel()
{
    if (m_finished) {
        return;
    }
    m_timer->stop();
    if (direction() == Direction::Receive) {
        finishReceive();
        return;
    }
    if (m_file) {
        m_file->close();
    }
    finish(false, tr("传输已取消"));
}
