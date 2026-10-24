#include "services/tftpserver.h"
#include "services/serviceutil.h"

#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>
#include <functional>
#include <utility>

namespace {

constexpr int kDefaultBlockSize = 512;
constexpr int kTimeoutMs = 2000;
constexpr int kMaxRetries = 5;

quint16 readU16(const QByteArray &data, int offset)
{
    return quint16((quint8(data.at(offset)) << 8) | quint8(data.at(offset + 1)));
}

void appendU16(QByteArray &data, quint16 value)
{
    data.append(char(value >> 8));
    data.append(char(value & 0xFF));
}

QByteArray makePacket(quint16 opcode)
{
    QByteArray packet;
    appendU16(packet, opcode);
    return packet;
}

QByteArray makeError(quint16 code, const QString &message)
{
    QByteArray packet = makePacket(5);
    appendU16(packet, code);
    packet.append(message.toUtf8());
    packet.append('\0');
    return packet;
}

struct TftpRequest {
    quint16 opcode = 0;
    QString fileName;
    QString mode;
    QMap<QString, QString> options;
};

bool parseRequest(const QByteArray &data, TftpRequest *request)
{
    if (data.size() < 4) {
        return false;
    }
    request->opcode = readU16(data, 0);
    const QList<QByteArray> fields = data.mid(2).split('\0');
    if (fields.size() < 2) {
        return false;
    }
    request->fileName = QString::fromUtf8(fields.at(0));
    request->mode = QString::fromUtf8(fields.at(1)).toLower();
    for (int i = 2; i + 1 < fields.size(); i += 2) {
        if (fields.at(i).isEmpty()) {
            break;
        }
        request->options.insert(QString::fromUtf8(fields.at(i)).toLower(),
                                QString::fromUtf8(fields.at(i + 1)));
    }
    return !request->fileName.isEmpty();
}

} // namespace

// ---------------------------------------------------------------------------
// 单个传输（每个客户端请求一个独立 UDP 端口，符合 TFTP 的 TID 约定）
// ---------------------------------------------------------------------------
class TftpSession : public QObject
{
    Q_OBJECT

public:
    TftpSession(QString root, bool allowWrite, const TftpRequest &request,
                const QHostAddress &peerAddress, quint16 peerPort,
                std::function<void(const QString &)> logger,
                std::function<void(TftpSession *)> onFinished,
                QObject *parent)
        : QObject(parent)
        , m_root(std::move(root))
        , m_allowWrite(allowWrite)
        , m_request(request)
        , m_peerAddress(peerAddress)
        , m_peerPort(peerPort)
        , m_log(std::move(logger))
        , m_onFinished(std::move(onFinished))
    {
        m_socket = new QUdpSocket(this);
        m_socket->bind(QHostAddress::AnyIPv4, 0);
        connect(m_socket, &QUdpSocket::readyRead, this, &TftpSession::onReadyRead);

        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        m_timer->setInterval(kTimeoutMs);
        connect(m_timer, &QTimer::timeout, this, &TftpSession::onTimeout);

        // 选项协商
        const QString blksize = m_request.options.value(QStringLiteral("blksize"));
        if (!blksize.isEmpty()) {
            bool ok = false;
            const int requested = blksize.toInt(&ok);
            if (ok && requested >= 8) {
                m_blockSize = qMin(requested, 65464);
            }
        }
        m_useOack = m_request.options.contains(QStringLiteral("blksize"))
                    || m_request.options.contains(QStringLiteral("tsize"));
    }

    void begin()
    {
        m_isWrite = m_request.opcode == 2;
        QString path;
        if (m_isWrite) {
            if (!m_allowWrite) {
                abortWith(2, tr("服务未允许上传"));
                return;
            }
            path = ServiceUtil::safeJoin(m_root, m_request.fileName, true);
            if (path.isEmpty() || QFileInfo(path).isDir()) {
                abortWith(2, tr("目标路径不合法"));
                return;
            }
            m_file.setFileName(path);
            if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                abortWith(2, tr("无法写入文件：%1").arg(m_file.errorString()));
                return;
            }
        } else {
            path = ServiceUtil::safeJoin(m_root, m_request.fileName);
            if (path.isEmpty() || !QFileInfo(path).isFile()) {
                abortWith(1, tr("文件不存在：%1").arg(m_request.fileName));
                return;
            }
            m_file.setFileName(path);
            if (!m_file.open(QIODevice::ReadOnly)) {
                abortWith(2, tr("无法读取文件：%1").arg(m_file.errorString()));
                return;
            }
        }
        m_fileSize = m_isWrite ? 0 : m_file.size();
        m_log(tr("TFTP %1 %2（%3，%4 字节）")
                  .arg(m_isWrite ? tr("接收") : tr("发送"),
                       m_request.fileName, peerText())
                  .arg(m_isWrite ? qint64(0) : m_fileSize));

        if (m_useOack) {
            sendOack();
        } else if (m_isWrite) {
            sendAck(0);
            m_state = WriteData;
        } else {
            m_block = 0;
            sendNextBlock();
        }
    }

private:
    enum State { Idle, ReadAfterOack, ReadData, WriteData };

    QString peerText() const
    {
        return QStringLiteral("%1:%2").arg(m_peerAddress.toString()).arg(m_peerPort);
    }

    void sendPacket(const QByteArray &packet)
    {
        m_socket->writeDatagram(packet, m_peerAddress, m_peerPort);
        m_lastPacket = packet;
        m_timer->start();
    }

    void sendOack()
    {
        QByteArray packet = makePacket(6);
        if (m_request.options.contains(QStringLiteral("blksize"))) {
            packet.append("blksize\0");
            packet.append(QByteArray::number(m_blockSize));
            packet.append('\0');
        }
        if (m_request.options.contains(QStringLiteral("tsize")) && !m_isWrite) {
            packet.append("tsize\0");
            packet.append(QByteArray::number(m_fileSize));
            packet.append('\0');
        }
        sendPacket(packet);
        m_state = m_isWrite ? WriteData : ReadAfterOack;
    }

    void sendAck(quint16 block)
    {
        QByteArray packet = makePacket(4);
        appendU16(packet, block);
        sendPacket(packet);
    }

    void sendNextBlock()
    {
        ++m_block;
        const QByteArray payload = m_file.read(m_blockSize);
        QByteArray packet = makePacket(3);
        appendU16(packet, m_block);
        packet.append(payload);
        sendPacket(packet);
        m_lastBlock = payload.size() < m_blockSize;
        m_state = ReadData;
    }

    void onReadyRead()
    {
        while (m_socket->hasPendingDatagrams()) {
            const QNetworkDatagram datagram = m_socket->receiveDatagram();
            if (datagram.senderAddress() != m_peerAddress
                || datagram.senderPort() != m_peerPort) {
                continue;   // 未知 TID
            }
            const QByteArray data = datagram.data();
            if (data.size() < 4) {
                continue;
            }
            const quint16 opcode = readU16(data, 0);
            const quint16 block = readU16(data, 2);

            if (opcode == 5) {   // 客户端报错
                m_log(tr("TFTP 客户端中止：%1")
                          .arg(QString::fromUtf8(data.mid(4))));
                finish();
                return;
            }
            if (m_isWrite) {
                if (opcode != 3) {
                    continue;
                }
                if (block == quint16(m_block + 1)) {
                    const QByteArray payload = data.mid(4);
                    m_file.write(payload);
                    m_fileSize += payload.size();
                    m_block = block;
                    if (payload.size() < m_blockSize) {
                        // 最后一个数据块：先落盘关闭，再回 ACK——客户端收到
                        // 最后一个 ACK 后立刻读取文件也能看到完整内容
                        m_file.flush();
                        m_file.close();
                        sendAck(block);
                        m_log(tr("TFTP 接收完成：%1（%2 字节）")
                                  .arg(m_request.fileName).arg(m_fileSize));
                        finish();
                        return;
                    }
                    sendAck(block);
                } else if (block == quint16(m_block)) {
                    sendAck(block);   // 重复块，重发 ACK
                } else if (m_block == 0 && block == 1) {
                    sendAck(block);
                }
                continue;
            }

            if (opcode != 4) {
                continue;
            }
            if (m_state == ReadAfterOack && block == 0) {
                m_block = 0;
                sendNextBlock();
            } else if (m_state == ReadData) {
                if (block == quint16(m_block)) {
                    if (m_lastBlock) {
                        m_log(tr("TFTP 发送完成：%1（%2 字节）")
                                  .arg(m_request.fileName).arg(m_fileSize));
                        m_file.close();
                        finish();
                        return;
                    }
                    sendNextBlock();
                } else if (block == quint16(m_block - 1)) {
                    sendPacket(m_lastPacket);   // ACK 迟到，重发上一块
                }
            }
        }
    }

    void onTimeout()
    {
        if (m_finished) {
            return;
        }
        if (++m_retries > kMaxRetries) {
            abortWith(0, tr("等待客户端响应超时"));
            return;
        }
        if (!m_lastPacket.isEmpty()) {
            m_socket->writeDatagram(m_lastPacket, m_peerAddress, m_peerPort);
            m_timer->start();
        }
    }

    void abortWith(quint16 code, const QString &message)
    {
        if (m_finished) {
            return;
        }
        m_log(tr("TFTP 中止（%1）：%2").arg(code).arg(message));
        m_socket->writeDatagram(makeError(code, message), m_peerAddress, m_peerPort);
        finish();
    }

    void finish()
    {
        if (m_finished) {
            return;
        }
        m_finished = true;
        m_timer->stop();
        if (m_file.isOpen()) {
            m_file.close();
        }
        m_socket->close();
        if (m_onFinished) {
            m_onFinished(this);
        }
    }

    QString m_root;
    bool m_allowWrite = false;
    TftpRequest m_request;
    QHostAddress m_peerAddress;
    quint16 m_peerPort = 0;

    QUdpSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    QFile m_file;
    qint64 m_fileSize = 0;
    int m_blockSize = kDefaultBlockSize;
    int m_block = 0;
    int m_retries = 0;
    bool m_useOack = false;
    bool m_isWrite = false;
    bool m_lastBlock = false;
    bool m_finished = false;
    State m_state = Idle;
    QByteArray m_lastPacket;
    std::function<void(const QString &)> m_log;
    std::function<void(TftpSession *)> m_onFinished;
};

// ---------------------------------------------------------------------------
// TftpServer
// ---------------------------------------------------------------------------
TftpServer::TftpServer(QObject *parent)
    : ServerService(parent)
{
}

QString TftpServer::displayName() const
{
    return tr("TFTP 服务器");
}

QString TftpServer::description() const
{
    return tr("通过 UDP 提供简单的文件上传/下载（嵌入式设备、路由器刷机常用）");
}

QJsonObject TftpServer::defaultConfig() const
{
    QJsonObject config;
    config.insert(QStringLiteral("port"), defaultPort());
    config.insert(QStringLiteral("root"), QString());
    config.insert(QStringLiteral("allowWrite"), false);
    return config;
}

bool TftpServer::startServer()
{
    const QString root = ServiceUtil::canonicalRoot(configString(QStringLiteral("root")));
    if (root.isEmpty()) {
        setError(tr("根目录不存在或不是文件夹"));
        return false;
    }
    const int listenPort = port();
    if (listenPort < 1 || listenPort > 65535) {
        setError(tr("端口无效"));
        return false;
    }

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::AnyIPv4, quint16(listenPort))) {
        setError(tr("无法监听 UDP 端口 %1：%2").arg(listenPort).arg(m_socket->errorString()));
        delete m_socket;
        m_socket = nullptr;
        return false;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &TftpServer::onReadyRead);

    log(tr("TFTP 服务已启动：根目录 %1，端口 %2").arg(root).arg(listenPort));
    return true;
}

void TftpServer::stopServer()
{
    for (TftpSession *session : std::as_const(m_sessions)) {
        session->deleteLater();
    }
    m_sessions.clear();
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void TftpServer::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram();
        const QByteArray data = datagram.data();
        if (data.size() < 4) {
            continue;
        }
        const quint16 opcode = readU16(data, 0);
        TftpRequest request;
        if ((opcode != 1 && opcode != 2) || !parseRequest(data, &request)) {
            m_socket->writeDatagram(makeError(4, tr("非法的 TFTP 请求")),
                                    datagram.senderAddress(), datagram.senderPort());
            continue;
        }
        if (!request.mode.isEmpty() && request.mode != QLatin1String("octet")
            && request.mode != QLatin1String("netascii")) {
            m_socket->writeDatagram(makeError(4, tr("不支持的传输模式")),
                                    datagram.senderAddress(), datagram.senderPort());
            continue;
        }

        auto *session = new TftpSession(
            ServiceUtil::canonicalRoot(configString(QStringLiteral("root"))),
            configBool(QStringLiteral("allowWrite")), request,
            datagram.senderAddress(), datagram.senderPort(),
            [this](const QString &line) { log(line); },
            [this](TftpSession *finished) {
                m_sessions.removeAll(finished);
                finished->deleteLater();
            },
            this);
        m_sessions.append(session);
        session->begin();
    }
}

#include "tftpserver.moc"
