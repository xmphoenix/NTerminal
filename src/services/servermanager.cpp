#include "services/servermanager.h"
#include "services/serverservice.h"
#include "services/tftpserver.h"
#include "services/ftpserver.h"
#include "services/httpserver.h"
#include "services/telnetserver.h"
#include "services/nfsserver.h"

ServerManager *ServerManager::instance()
{
    static ServerManager manager;
    return &manager;
}

ServerManager::ServerManager(QObject *parent)
    : QObject(parent)
{
}

ServerManager::~ServerManager()
{
    for (ServerService *service : std::as_const(m_services)) {
        if (service->isRunning()) {
            service->stop();
        }
    }
}

void ServerManager::ensureCreated()
{
    if (!m_services.isEmpty()) {
        return;
    }
    m_services = {
        new TftpServer(this),
        new FtpServer(this),
        new HttpServer(this),
        new TelnetServer(this),
        new NfsServer(this),
    };
    for (ServerService *service : std::as_const(m_services)) {
        service->loadConfig();
    }
}

QList<ServerService *> ServerManager::services()
{
    ensureCreated();
    return m_services;
}

ServerService *ServerManager::service(const QString &id)
{
    ensureCreated();
    for (ServerService *service : std::as_const(m_services)) {
        if (service->id() == id) {
            return service;
        }
    }
    return nullptr;
}

void ServerManager::stopAll()
{
    for (ServerService *service : std::as_const(m_services)) {
        service->stop();
    }
}
