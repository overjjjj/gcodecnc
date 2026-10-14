#pragma once
#include <QObject>

#ifdef CNEXT_ENABLE_SERIALPORT
class QSerialPort;
#endif

// TODO: Implement QSerialPort DNC communication when the hardware interface is defined.
class CncCommInterface : public QObject
{
    Q_OBJECT
public:
    explicit CncCommInterface(QObject *parent = nullptr);

    virtual bool connect(const QString &portName, int baudRate);
    virtual void disconnect();
    virtual bool isConnected() const;
    virtual bool sendGCode(const QString &gcode);

signals:
    void connected();
    void disconnected();
    void responseReceived(const QString &data);
    void errorOccurred(const QString &msg);

private:
    bool m_connected = false;
#ifdef CNEXT_ENABLE_SERIALPORT
    QSerialPort *m_serial = nullptr;
#endif
};
