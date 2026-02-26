#include "serialhandler.h"
#include <QDebug>

SerialHandler::SerialHandler(const Common::ConnectionSettings &settings, QObject *parent)
    : ConnectionHandler(parent)
    , m_serialPort(new QSerialPort(this))
    , m_settings(settings)
{
    connect(m_serialPort, &QSerialPort::readyRead, this, &SerialHandler::onReadyRead);
    connect(m_serialPort, &QSerialPort::errorOccurred, this, &SerialHandler::onError);
}

SerialHandler::~SerialHandler()
{
    close();
}

bool SerialHandler::open()
{
    m_serialPort->setPortName(m_settings.portName);
    m_serialPort->setBaudRate(m_settings.baudRate);
    m_serialPort->setDataBits(m_settings.dataBits);
    m_serialPort->setParity(m_settings.parity);
    m_serialPort->setStopBits(m_settings.stopBits);
    m_serialPort->setFlowControl(m_settings.flowControl);
    
    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        QString error = QString("Failed to open serial port %1: %2")
                        .arg(m_settings.portName, m_serialPort->errorString());
        qWarning() << "[SerialHandler]" << error;
        emit errorOccurred(error);
        return false;
    }
    
    qDebug() << "[SerialHandler] Serial port opened:" << m_settings.portName;
    emit connected();
    return true;
}

void SerialHandler::close()
{
    if (m_serialPort->isOpen()) {
        m_serialPort->close();
        qDebug() << "[SerialHandler] Serial port closed";
        emit disconnected();
    }
}

bool SerialHandler::isOpen() const
{
    return m_serialPort->isOpen();
}

bool SerialHandler::isConnected() const
{
    return m_serialPort->isOpen();
}

void SerialHandler::sendData(const QByteArray &data)
{
    if (m_serialPort->isOpen()) {
        m_serialPort->write(data);
        m_serialPort->flush();
    }
}

QString SerialHandler::connectionInfo() const
{
    return QString("Serial: %1 @ %2").arg(m_settings.portName).arg(m_settings.baudRate);
}

void SerialHandler::onReadyRead()
{
    QByteArray data = m_serialPort->readAll();
    if (!data.isEmpty()) {
        emit dataReceived(data);
    }
}

void SerialHandler::onError(QSerialPort::SerialPortError error)
{
    if (error != QSerialPort::NoError && error != QSerialPort::TimeoutError) {
        QString errorString = m_serialPort->errorString();
        qWarning() << "[SerialHandler] Error:" << error << errorString;
        emit errorOccurred(errorString);
    }
}
