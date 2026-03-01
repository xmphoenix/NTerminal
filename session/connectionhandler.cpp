#include "connectionhandler.h"
#include "serialhandler.h"
#include "telnethandler.h"
#include "ssshandler.h"
#include "rloginhandler.h"
#include "sftphandler.h"
#include "ftphandler.h"
#include "tftphandler.h"
#include <QDebug>

ConnectionHandler::ConnectionHandler(QObject *parent)
    : QObject(parent)
{
}

ConnectionHandler::~ConnectionHandler()
{
}

ConnectionHandler* ConnectionHandler::create(const Common::ConnectionSettings &settings, QObject *parent)
{
    switch (settings.type) {
        case Common::ConnectionType::Serial:
            return new SerialHandler(settings, parent);
        case Common::ConnectionType::Telnet:
            return new TelnetHandler(settings, parent);
        case Common::ConnectionType::SSH:
            return new SshHandler(settings, parent);
        case Common::ConnectionType::Rlogin:
            return new RloginHandler(settings, parent);
        case Common::ConnectionType::SFTP:
            return new SftpHandler(settings, parent);
        case Common::ConnectionType::FTP:
            return new FtpHandler(settings, parent);
        case Common::ConnectionType::TFTP:
            return new TftpHandler(settings, parent);
        default:
            qWarning() << "[ConnectionHandler] Unknown connection type";
            return nullptr;
    }
}
