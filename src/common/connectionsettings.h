#ifndef CONNECTIONSETTINGS_H
#define CONNECTIONSETTINGS_H

#include <QString>
#include <QJsonObject>
#include <QJsonValue>
#include <QSerialPort>

// ============================================================================
// 跨进程共享的连接配置（GUI 进程 <-> 会话进程 <-> 持久化）
//
// 本结构是全项目唯一的连接设置定义：
//  - ConnectionDialog（UI 层）读写它；
//  - SessionManager（服务层）将它序列化到 sessions.json；
//  - SessionProcess（会话进程）经 IPC JSON 载荷接收它。
//
// JSON 格式为稳定契约：
//  - dataBits 为整数 5/6/7/8（读取时兼容历史字符串格式 "5".."8"）；
//  - parity/flowControl 使用短名（"None"/"Even"/... "Hardware"/...，
//    读取时兼容历史枚举名格式 "NoParity"/"HardwareControl"/...）。
// ============================================================================

namespace Common {

enum class ConnectionType {
    Serial,
    Telnet,
    SSH,
    Rlogin,
    SFTP,
    FTP,
    TFTP,
    SCP,
    RDP
};

struct ConnectionSettings {
    QString sessionName;
    ConnectionType type;
    
    QString portName;
    qint32 baudRate;
    QSerialPort::DataBits dataBits;
    QSerialPort::Parity parity;
    QSerialPort::StopBits stopBits;
    QSerialPort::FlowControl flowControl;
    
    QString host;
    quint16 port;
    QString username;
    QString password;
    bool useSshKey;
    QString sshKeyPath;

    // RDP
    QString rdpDomain;
    bool rdpUseNla;
    
    ConnectionSettings()
        : type(ConnectionType::Serial)
        , baudRate(115200)
        , dataBits(QSerialPort::Data8)
        , parity(QSerialPort::NoParity)
        , stopBits(QSerialPort::OneStop)
        , flowControl(QSerialPort::NoFlowControl)
        , port(23)
        , useSshKey(false)
        , rdpUseNla(false)
    {}
    
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["sessionName"] = sessionName;
        obj["type"] = connectionTypeToString(type);

        obj["portName"] = portName;
        obj["baudRate"] = baudRate;
        obj["dataBits"] = dataBitsToInt(dataBits);
        obj["parity"] = parityToString(parity);
        obj["stopBits"] = stopBitsToString(stopBits);
        obj["flowControl"] = flowControlToString(flowControl);

        obj["host"] = host;
        obj["port"] = port;
        obj["username"] = username;
        obj["password"] = password;
        obj["useSshKey"] = useSshKey;
        obj["sshKeyPath"] = sshKeyPath;
        obj["rdpDomain"] = rdpDomain;
        obj["rdpUseNla"] = rdpUseNla;

        return obj;
    }
    
    static ConnectionSettings fromJson(const QJsonObject &obj) {
        ConnectionSettings settings;
        settings.sessionName = obj["sessionName"].toString();
        settings.type = stringToConnectionType(obj["type"].toString());
        
        settings.portName = obj["portName"].toString();
        settings.baudRate = obj["baudRate"].toInt(115200);
        settings.dataBits = intToDataBits(readIntOrString(obj["dataBits"], 8));
        settings.parity = stringToParity(obj["parity"].toString());
        settings.stopBits = stringToStopBits(obj["stopBits"].toString());
        settings.flowControl = stringToFlowControl(obj["flowControl"].toString());
        
        settings.host = obj["host"].toString();
        settings.port = obj["port"].toInt(23);
        settings.username = obj["username"].toString();
        settings.password = obj["password"].toString();
        settings.useSshKey = obj["useSshKey"].toBool(false);
        settings.sshKeyPath = obj["sshKeyPath"].toString();
        settings.rdpDomain = obj["rdpDomain"].toString();
        settings.rdpUseNla = obj["rdpUseNla"].toBool(false);
        
        return settings;
    }
    
    // ---- 枚举 <-> 稳定字符串契约（公开供持久化层与测试复用） ----
    static QString connectionTypeToString(ConnectionType type) {
        switch (type) {
            case ConnectionType::Serial: return "Serial";
            case ConnectionType::Telnet: return "Telnet";
            case ConnectionType::SSH: return "SSH";
            case ConnectionType::Rlogin: return "Rlogin";
            case ConnectionType::SFTP: return "SFTP";
            case ConnectionType::FTP: return "FTP";
            case ConnectionType::TFTP: return "TFTP";
            case ConnectionType::SCP: return "SCP";
            case ConnectionType::RDP: return "RDP";
            default: return "Serial";
        }
    }

    static ConnectionType stringToConnectionType(const QString &str) {
        if (str == "Telnet") return ConnectionType::Telnet;
        if (str == "SSH") return ConnectionType::SSH;
        if (str == "Rlogin") return ConnectionType::Rlogin;
        if (str == "SFTP") return ConnectionType::SFTP;
        if (str == "FTP") return ConnectionType::FTP;
        if (str == "TFTP") return ConnectionType::TFTP;
        if (str == "SCP") return ConnectionType::SCP;
        if (str == "RDP") return ConnectionType::RDP;
        return ConnectionType::Serial;
    }
    
    static int dataBitsToInt(QSerialPort::DataBits db) {
        switch (db) {
            case QSerialPort::Data5: return 5;
            case QSerialPort::Data6: return 6;
            case QSerialPort::Data7: return 7;
            default: return 8;
        }
    }
    
    static QSerialPort::DataBits intToDataBits(int val) {
        switch (val) {
            case 5: return QSerialPort::Data5;
            case 6: return QSerialPort::Data6;
            case 7: return QSerialPort::Data7;
            default: return QSerialPort::Data8;
        }
    }
    
    static QString parityToString(QSerialPort::Parity p) {
        switch (p) {
            case QSerialPort::EvenParity: return "Even";
            case QSerialPort::OddParity: return "Odd";
            case QSerialPort::SpaceParity: return "Space";
            case QSerialPort::MarkParity: return "Mark";
            default: return "None";
        }
    }
    
    static QSerialPort::Parity stringToParity(const QString &str) {
        if (str == "Even" || str == "EvenParity") return QSerialPort::EvenParity;
        if (str == "Odd" || str == "OddParity") return QSerialPort::OddParity;
        if (str == "Space" || str == "SpaceParity") return QSerialPort::SpaceParity;
        if (str == "Mark" || str == "MarkParity") return QSerialPort::MarkParity;
        return QSerialPort::NoParity;
    }
    
    static QString stopBitsToString(QSerialPort::StopBits sb) {
        switch (sb) {
            case QSerialPort::OneAndHalfStop: return "1.5";
            case QSerialPort::TwoStop: return "2";
            default: return "1";
        }
    }
    
    static QSerialPort::StopBits stringToStopBits(const QString &str) {
        if (str == "1.5") return QSerialPort::OneAndHalfStop;
        if (str == "2") return QSerialPort::TwoStop;
        return QSerialPort::OneStop;
    }
    
    static QString flowControlToString(QSerialPort::FlowControl fc) {
        switch (fc) {
            case QSerialPort::HardwareControl: return "Hardware";
            case QSerialPort::SoftwareControl: return "Software";
            default: return "None";
        }
    }
    
    static QSerialPort::FlowControl stringToFlowControl(const QString &str) {
        if (str == "Hardware" || str == "HardwareControl") return QSerialPort::HardwareControl;
        if (str == "Software" || str == "SoftwareControl") return QSerialPort::SoftwareControl;
        return QSerialPort::NoFlowControl;
    }

    // 读取整数值；兼容历史持久化格式（值为字符串 "5".."8"）
    static int readIntOrString(const QJsonValue &value, int defaultValue) {
        if (value.isDouble()) return value.toInt(defaultValue);
        if (value.isString()) {
            bool ok = false;
            int parsed = value.toString().toInt(&ok);
            return ok ? parsed : defaultValue;
        }
        return defaultValue;
    }
};

}

Q_DECLARE_METATYPE(Common::ConnectionSettings)

#endif // CONNECTIONSETTINGS_H
