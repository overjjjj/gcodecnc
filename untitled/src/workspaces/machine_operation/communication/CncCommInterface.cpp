#include "CncCommInterface.h"

#ifdef CNEXT_ENABLE_SERIALPORT
#include <QIODevice>
#include <QSerialPort>
#endif

CncCommInterface::CncCommInterface(QObject *parent)
    : QObject(parent)
{
#ifdef CNEXT_ENABLE_SERIALPORT
    m_serial = new QSerialPort(this);
    QObject::connect(m_serial, &QSerialPort::readyRead, this, [this]() {
        emit responseReceived(QString::fromLocal8Bit(m_serial->readAll()));
    });
    QObject::connect(m_serial,
                     QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
                     this,
                     [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError) {
            return;
        }
        emit errorOccurred(m_serial->errorString());
    });
#endif
}

bool CncCommInterface::connect(const QString &portName, int baudRate)
{
#ifdef CNEXT_ENABLE_SERIALPORT
    if (!m_serial) {
        return false;
    }
    if (m_serial->isOpen()) {
        m_serial->close();
    }

    m_serial->setPortName(portName);
    m_serial->setBaudRate(baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        m_connected = false;
        emit errorOccurred(m_serial->errorString());
        return false;
    }

    m_connected = true;
    emit connected();
    return true;
#else
    Q_UNUSED(portName)
    Q_UNUSED(baudRate)
    emit errorOccurred(tr("当前构建未包含 QtSerialPort 模块。"));
    return false;
#endif
}

void CncCommInterface::disconnect()
{
#ifdef CNEXT_ENABLE_SERIALPORT
    if (m_serial && m_serial->isOpen()) {
        m_serial->close();
    }
#endif
    if (!m_connected) {
        return;
    }
    m_connected = false;
    emit disconnected();
}

bool CncCommInterface::isConnected() const
{
    return m_connected;
}

bool CncCommInterface::sendGCode(const QString &gcode)
{
#ifdef CNEXT_ENABLE_SERIALPORT
    if (!m_serial || !m_serial->isOpen()) {
        emit errorOccurred(tr("串口未连接。"));
        return false;
    }

    const QByteArray payload = gcode.toLocal8Bit();
    const qint64 written = m_serial->write(payload);
    if (written != payload.size()) {
        emit errorOccurred(m_serial->errorString());
        return false;
    }

    return m_serial->flush();
#else
    Q_UNUSED(gcode)
    emit errorOccurred(tr("当前构建未包含 QtSerialPort 模块。"));
    return false;
#endif
}
