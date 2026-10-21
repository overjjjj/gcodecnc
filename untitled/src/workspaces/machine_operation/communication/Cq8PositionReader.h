#pragma once
#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <QVector>

// The transport must frame replies and preserve request IDs. This class never
// opens a port, changes controller configuration, or sends motion commands.
class Cq8PositionReader : public QObject
{
    Q_OBJECT
public:
    explicit Cq8PositionReader(QObject *parent = nullptr, int timeoutMs = 2000);
    bool start();
    bool requestPositions();
    void appendResponse(quint64 requestId, const QByteArray &bytes);
    void finishResponse(quint64 requestId);
    void stop();
    QVector<quint32> positions() const { return m_positions; }
    bool feedbackVerified() const { return m_verified; }
signals:
    // Command text only: the transport supplies its documented wire terminator.
    void readRequested(quint64 requestId, const QString &command);
    void positionsUpdated(quint32 motor1, quint32 motor2, quint32 motor3);
    void positionsInvalidated();
    void errorOccurred(const QString &message);
private:
    void issue(int variable, const QString &command);
    void fail(const QString &message);
    void invalidate();
    QTimer m_timer;
    QByteArray m_response;
    QVector<quint32> m_positions;
    quint64 m_requestId = 0;
    int m_variable = 0;
    bool m_pending = false;
    bool m_verified = false;
};
