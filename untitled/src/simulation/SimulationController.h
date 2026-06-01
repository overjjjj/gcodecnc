#pragma once
#include <QObject>
#include <QTimer>
#include <QVector>
#include <QVector3D>
#include "../import/StepImporter.h"

struct ToolMove {
    QVector3D from;
    QVector3D to;
    bool      isRapid = false; // G0 = true, G1 = false
    double    toolDiameter = 0.0;
    double    shankDiameter = 0.0;
    double    fluteLength = 0.0;
    double    totalLength = 0.0;
    QString   toolModelPath;
    int       sourceLine = -1;
};

class SimulationController : public QObject
{
    Q_OBJECT
public:
    explicit SimulationController(QObject *parent = nullptr);

    void setMesh(const MeshData &mesh);
    void loadGCode(const QString &gcode);

    void play();
    void pause();
    void stop();
    void setSpeed(double multiplier);
    void seekToGCodeLine(int line);

    const QVector<QVector3D> &toolPath() const { return m_path; }
    bool isRunning() const { return m_running; }

signals:
    void progressChanged(double fraction);   // 0.0 – 1.0
    void simulationFinished();
    void toolPositionChanged(QVector3D pos);
    void toolDiameterChanged(double diameter);
    void toolModelPathChanged(const QString &path);
    void toolPathReady(const QVector<QVector3D> &path, const QVector<bool> &rapidSegments);
    void collisionChecked(bool hasCollision, const QString &message);
    void currentGCodeLineChanged(int line);

private slots:
    void onTick();

private:
    static QVector<ToolMove> parseGCode(const QString &gcode);
    static QString detectCollisionMessage(const MeshData &mesh, const QVector<ToolMove> &moves);

    MeshData          m_mesh;
    QString           m_gcode;
    QVector<ToolMove> m_moves;
    QVector<QVector3D> m_path;   // pre-flattened waypoints for rendering
    QVector<bool>      m_rapidSegments;
    int               m_step    = 0;
    bool              m_running = false;
    double            m_speed   = 1.0;
    QTimer           *m_timer;
};
