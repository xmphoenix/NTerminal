#include "connectionhandler.h"
#include "serialhandler.h"
#include "telnethandler.h"
#include "ssshandler.h"
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
        default:
            qWarning() << "[ConnectionHandler] Unknown connection type";
            return nullptr;
    }
}
