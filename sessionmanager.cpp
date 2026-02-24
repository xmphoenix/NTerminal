#include "sessionmanager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDateTime>
#include <QDir>
#include <QDebug>

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
{
    // 设置配置文件路径：用户目录下的 .nterminal/sessions.json
    QString configDir = QDir::homePath() + "/.nterminal";
    QDir().mkpath(configDir);
    m_configFilePath = configDir + "/sessions.json";
}

SessionManager::~SessionManager()
{
    saveSessions();
}

bool SessionManager::loadSessions()
{
    QFile file(m_configFilePath);
    if (!file.exists()) {
        // 配置文件不存在，不是错误
        return true;
    }
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred(QString("无法打开会话配置文件: %1").arg(file.errorString()));
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred(QString("JSON解析错误: %1").arg(parseError.errorString()));
        return false;
    }
    
    if (!doc.isArray()) {
        emit errorOccurred("会话配置文件格式错误: 根元素不是数组");
        return false;
    }
    
    m_sessions.clear();
    QJsonArray sessionsArray = doc.array();
    
    for (const QJsonValue &sessionValue : sessionsArray) {
        if (!sessionValue.isObject()) {
            qWarning() << "跳过无效的会话项（不是对象）";
            continue;
        }
        
        QJsonObject sessionObj = sessionValue.toObject();
        
        if (!sessionObj.contains("name") || !sessionObj["name"].isString()) {
            qWarning() << "跳过无效的会话项（缺少或无效的名称）";
            continue;
        }
        
        QString name = sessionObj["name"].toString();
        
        if (!sessionObj.contains("settings") || !sessionObj["settings"].isObject()) {
            qWarning() << "跳过无效的会话项" << name << "（缺少或无效的设置）";
            continue;
        }
        
        QJsonObject settingsObj = sessionObj["settings"].toObject();
        
        ConnectionDialog::ConnectionSettings settings;
        
        // 读取会话名称
        if (settingsObj.contains("sessionName") && settingsObj["sessionName"].isString()) {
            settings.sessionName = settingsObj["sessionName"].toString();
        } else {
            settings.sessionName = name;
        }
        
        // 读取连接类型
        if (settingsObj.contains("type") && settingsObj["type"].isString()) {
            settings.type = stringToConnectionType(settingsObj["type"].toString());
        } else {
            settings.type = ConnectionDialog::Serial;
        }
        
        // 读取串口设置
        if (settingsObj.contains("portName") && settingsObj["portName"].isString()) {
            settings.portName = settingsObj["portName"].toString();
        }
        
        if (settingsObj.contains("baudRate") && settingsObj["baudRate"].isDouble()) {
            settings.baudRate = settingsObj["baudRate"].toInt();
        } else {
            settings.baudRate = 115200;
        }
        
        if (settingsObj.contains("dataBits") && settingsObj["dataBits"].isString()) {
            settings.dataBits = stringToDataBits(settingsObj["dataBits"].toString());
        } else {
            settings.dataBits = QSerialPort::Data8;
        }
        
        if (settingsObj.contains("parity") && settingsObj["parity"].isString()) {
            settings.parity = stringToParity(settingsObj["parity"].toString());
        } else {
            settings.parity = QSerialPort::NoParity;
        }
        
        if (settingsObj.contains("stopBits") && settingsObj["stopBits"].isString()) {
            settings.stopBits = stringToStopBits(settingsObj["stopBits"].toString());
        } else {
            settings.stopBits = QSerialPort::OneStop;
        }
        
        if (settingsObj.contains("flowControl") && settingsObj["flowControl"].isString()) {
            settings.flowControl = stringToFlowControl(settingsObj["flowControl"].toString());
        } else {
            settings.flowControl = QSerialPort::NoFlowControl;
        }
        
        // 读取网络设置
        if (settingsObj.contains("host") && settingsObj["host"].isString()) {
            settings.host = settingsObj["host"].toString();
        }
        
        if (settingsObj.contains("port") && settingsObj["port"].isDouble()) {
            settings.port = settingsObj["port"].toInt();
        } else {
            settings.port = (settings.type == ConnectionDialog::SSH) ? 22 : 23;
        }
        
        if (settingsObj.contains("username") && settingsObj["username"].isString()) {
            settings.username = settingsObj["username"].toString();
        }
        
        if (settingsObj.contains("password") && settingsObj["password"].isString()) {
            settings.password = settingsObj["password"].toString();
        }
        
        if (settingsObj.contains("useSshKey") && settingsObj["useSshKey"].isBool()) {
            settings.useSshKey = settingsObj["useSshKey"].toBool();
        } else {
            settings.useSshKey = false;
        }
        
        if (settingsObj.contains("sshKeyPath") && settingsObj["sshKeyPath"].isString()) {
            settings.sshKeyPath = settingsObj["sshKeyPath"].toString();
        }
        
        // 读取最后使用时间
        QDateTime lastUsed = QDateTime::currentDateTime();
        if (sessionObj.contains("lastUsed") && sessionObj["lastUsed"].isString()) {
            lastUsed = QDateTime::fromString(sessionObj["lastUsed"].toString(), Qt::ISODate);
        }
        
        Session session(name, settings);
        session.lastUsed = lastUsed;
        m_sessions.append(session);
    }
    
    // 按最后使用时间排序（最近使用的在前）
    std::sort(m_sessions.begin(), m_sessions.end(),
              [](const Session &a, const Session &b) {
                  return a.lastUsed > b.lastUsed;
              });
    
    emit sessionsLoaded();
    return true;
}

bool SessionManager::saveSessions()
{
    QJsonArray sessionsArray;
    
    for (const Session &session : m_sessions) {
        QJsonObject sessionObj;
        sessionObj["name"] = session.name;
        sessionObj["lastUsed"] = session.lastUsed.toString(Qt::ISODate);
        
        QJsonObject settingsObj;
        const ConnectionDialog::ConnectionSettings &settings = session.settings;
        
        settingsObj["sessionName"] = settings.sessionName;
        settingsObj["type"] = connectionTypeToString(settings.type);
        
        // 串口设置
        settingsObj["portName"] = settings.portName;
        settingsObj["baudRate"] = settings.baudRate;
        settingsObj["dataBits"] = dataBitsToString(settings.dataBits);
        settingsObj["parity"] = parityToString(settings.parity);
        settingsObj["stopBits"] = stopBitsToString(settings.stopBits);
        settingsObj["flowControl"] = flowControlToString(settings.flowControl);
        
        // 网络设置
        settingsObj["host"] = settings.host;
        settingsObj["port"] = settings.port;
        settingsObj["username"] = settings.username;
        settingsObj["password"] = settings.password;
        settingsObj["useSshKey"] = settings.useSshKey;
        settingsObj["sshKeyPath"] = settings.sshKeyPath;
        
        sessionObj["settings"] = settingsObj;
        sessionsArray.append(sessionObj);
    }
    
    QJsonDocument doc(sessionsArray);
    QByteArray data = doc.toJson(QJsonDocument::Indented);
    
    QFile file(m_configFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit errorOccurred(QString("无法保存会话配置文件: %1").arg(file.errorString()));
        return false;
    }
    
    qint64 bytesWritten = file.write(data);
    file.close();
    
    if (bytesWritten != data.size()) {
        emit errorOccurred("写入会话配置文件时发生错误");
        return false;
    }
    
    return true;
}

QList<SessionManager::Session> SessionManager::getSessions() const
{
    return m_sessions;
}

ConnectionDialog::ConnectionSettings SessionManager::getSession(const QString &name) const
{
    for (const Session &session : m_sessions) {
        if (session.name == name) {
            return session.settings;
        }
    }
    
    // 返回默认设置
    ConnectionDialog::ConnectionSettings settings;
    settings.sessionName = name;
    settings.type = ConnectionDialog::Serial;
    settings.baudRate = 115200;
    settings.dataBits = QSerialPort::Data8;
    settings.parity = QSerialPort::NoParity;
    settings.stopBits = QSerialPort::OneStop;
    settings.flowControl = QSerialPort::NoFlowControl;
    settings.port = 23;
    return settings;
}

void SessionManager::addSession(const QString &name, const ConnectionDialog::ConnectionSettings &settings)
{
    // 检查是否已存在同名会话
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].name == name) {
            // 更新现有会话
            m_sessions[i].settings = settings;
            m_sessions[i].lastUsed = QDateTime::currentDateTime();
            emit sessionUpdated(name);
            return;
        }
    }
    
    // 添加新会话
    Session session(name, settings);
    m_sessions.append(session);
    
    // 排序
    std::sort(m_sessions.begin(), m_sessions.end(),
              [](const Session &a, const Session &b) {
                  return a.lastUsed > b.lastUsed;
              });
    
    emit sessionAdded(name);
}

void SessionManager::updateSession(const QString &name, const ConnectionDialog::ConnectionSettings &settings)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].name == name) {
            m_sessions[i].settings = settings;
            m_sessions[i].lastUsed = QDateTime::currentDateTime();
            
            // 排序
            std::sort(m_sessions.begin(), m_sessions.end(),
                      [](const Session &a, const Session &b) {
                          return a.lastUsed > b.lastUsed;
                      });
            
            emit sessionUpdated(name);
            return;
        }
    }
    
    // 如果不存在，则添加
    addSession(name, settings);
}

void SessionManager::removeSession(const QString &name)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].name == name) {
            m_sessions.removeAt(i);
            emit sessionRemoved(name);
            return;
        }
    }
}

QString SessionManager::getConfigFilePath() const
{
    return m_configFilePath;
}

ConnectionDialog::ConnectionType SessionManager::stringToConnectionType(const QString &str) const
{
    if (str == "Serial") return ConnectionDialog::Serial;
    if (str == "Telnet") return ConnectionDialog::Telnet;
    if (str == "SSH") return ConnectionDialog::SSH;
    return ConnectionDialog::Serial;
}

QString SessionManager::connectionTypeToString(ConnectionDialog::ConnectionType type) const
{
    switch (type) {
        case ConnectionDialog::Serial: return "Serial";
        case ConnectionDialog::Telnet: return "Telnet";
        case ConnectionDialog::SSH: return "SSH";
        default: return "Serial";
    }
}

QSerialPort::DataBits SessionManager::stringToDataBits(const QString &str) const
{
    if (str == "5") return QSerialPort::Data5;
    if (str == "6") return QSerialPort::Data6;
    if (str == "7") return QSerialPort::Data7;
    if (str == "8") return QSerialPort::Data8;
    return QSerialPort::Data8;
}

QString SessionManager::dataBitsToString(QSerialPort::DataBits dataBits) const
{
    switch (dataBits) {
        case QSerialPort::Data5: return "5";
        case QSerialPort::Data6: return "6";
        case QSerialPort::Data7: return "7";
        case QSerialPort::Data8: return "8";
        default: return "8";
    }
}

QSerialPort::Parity SessionManager::stringToParity(const QString &str) const
{
    if (str == "NoParity") return QSerialPort::NoParity;
    if (str == "EvenParity") return QSerialPort::EvenParity;
    if (str == "OddParity") return QSerialPort::OddParity;
    if (str == "SpaceParity") return QSerialPort::SpaceParity;
    if (str == "MarkParity") return QSerialPort::MarkParity;
    return QSerialPort::NoParity;
}

QString SessionManager::parityToString(QSerialPort::Parity parity) const
{
    switch (parity) {
        case QSerialPort::NoParity: return "NoParity";
        case QSerialPort::EvenParity: return "EvenParity";
        case QSerialPort::OddParity: return "OddParity";
        case QSerialPort::SpaceParity: return "SpaceParity";
        case QSerialPort::MarkParity: return "MarkParity";
        default: return "NoParity";
    }
}

QSerialPort::StopBits SessionManager::stringToStopBits(const QString &str) const
{
    if (str == "1") return QSerialPort::OneStop;
    if (str == "1.5") return QSerialPort::OneAndHalfStop;
    if (str == "2") return QSerialPort::TwoStop;
    return QSerialPort::OneStop;
}

QString SessionManager::stopBitsToString(QSerialPort::StopBits stopBits) const
{
    switch (stopBits) {
        case QSerialPort::OneStop: return "1";
        case QSerialPort::OneAndHalfStop: return "1.5";
        case QSerialPort::TwoStop: return "2";
        default: return "1";
    }
}

QSerialPort::FlowControl SessionManager::stringToFlowControl(const QString &str) const
{
    if (str == "NoFlowControl") return QSerialPort::NoFlowControl;
    if (str == "HardwareControl") return QSerialPort::HardwareControl;
    if (str == "SoftwareControl") return QSerialPort::SoftwareControl;
    return QSerialPort::NoFlowControl;
}

QString SessionManager::flowControlToString(QSerialPort::FlowControl flowControl) const
{
    switch (flowControl) {
        case QSerialPort::NoFlowControl: return "NoFlowControl";
        case QSerialPort::HardwareControl: return "HardwareControl";
        case QSerialPort::SoftwareControl: return "SoftwareControl";
        default: return "NoFlowControl";
    }
}