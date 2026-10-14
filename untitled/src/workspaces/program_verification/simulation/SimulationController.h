#pragma once
#include <QObject>
#include <QTimer>
#include <QVector>
#include <QVector3D>
#include <QStringList>
#include <optional>
#include "SketchVerificationContext.h"
#include "../../../import/StepImporter.h"

struct ToolMove {
    Coordinate3D from;
    Coordinate3D to;
    bool      isRapid = false; // G0 = true, G1 = false
    double    toolDiameter = 0.0;
    double    shankDiameter = 0.0;
    double    fluteLength = 0.0;
    double    totalLength = 0.0;
    QString   toolModelPath;
    int       toolId = -1;
    int       sourceLine = -1;
    int       fromKnownAxes = 7; // XYZ bits; parser starts with no known program position
    int       toKnownAxes = 7;
    int       commandedAxes = 7;
    double    chordDeviation = 0.0; // upper XY deviation of an original arc from this segment
};

struct ExternalProgramSection {
    QString label;
    QString text;
    int firstLine = 0; // zero-based original file lines, inclusive
    int lastLine = 0;
};

enum class SimulationCheckState { NotChecked, Complete, Limited, Incomplete, Collision, InvalidMotion };

struct SimulationCheckResult {
    SimulationCheckState state = SimulationCheckState::NotChecked;
    QString message;
    QStringList limitations;
    bool blocksOutput() const {
        return state != SimulationCheckState::Complete && state != SimulationCheckState::Limited;
    }
};

class SimulationController : public QObject
{
    Q_OBJECT
public:
    explicit SimulationController(QObject *parent = nullptr);

    void setMesh(const MeshData &mesh);
    void setSketchVerificationContext(const SketchVerificationContext &context);
    // 中文说明：仿真必须解析最终输出文本，而不是独立的刀路缓存，确保显示与导出同源。
    // 解析后立即执行碰撞/越界初筛，并通过信号把路径和告警交给界面。
    void loadGCode(const QString &gcode, bool useProjectTools = true, int sourceLineOffset = 0,
                   const Coordinate3D &modelOrigin = {});
    // External preview accepts a deliberately bounded dialect; empty means supported.
    static QString externalGCodeError(const QString &gcode, int sourceLineOffset = 0);
    static QVector<ExternalProgramSection> externalPrograms(const QString &text, QString *error);

    void play();
    void pause();
    void stop();
    void setSpeed(double multiplier);
    void seekToGCodeLine(int line);

    const QVector<QVector3D> &toolPath() const { return m_path; }
    const QVector<ToolMove> &moves() const { return m_moves; }
    const QString &sourceText() const { return m_gcode; }
    const QString &motionError() const { return m_motionError; }
    const SimulationCheckResult &checkResult() const { return m_checkResult; }
    bool checkMatches(const QString &text, const Coordinate3D &origin,
                      const QString &contextFingerprint = {}) const {
        return m_checkResult.state != SimulationCheckState::NotChecked &&
            m_gcode == text && m_modelOrigin == origin && m_checkedContextFingerprint == contextFingerprint;
    }
    bool isRunning() const { return m_running; }
    // Fanuc/CQ8 G73 does not carry the controller's machine-parameter short
    // retract distance in NC text, so its intermediate retracts are only a
    // hole-position/depth illustration until that profile value is confirmed.
    bool hasUnverifiedRetract() const { return m_hasUnverifiedRetract; }
    bool hasUnverifiedG76LateralShift() const { return m_hasUnverifiedG76LateralShift; }
    bool hasUnverifiedG86SpindleStopRetract() const { return m_hasUnverifiedG86SpindleStopRetract; }
    bool hasUnverifiedSpindleSync() const { return m_hasUnverifiedSpindleSync; }

signals:
    void progressChanged(double fraction);   // 0.0 – 1.0
    void simulationFinished();
    void toolPositionChanged(QVector3D pos);
    void toolDiameterChanged(double diameter);
    void toolModelPathChanged(const QString &path);
    void toolPathReady(const QVector<QVector3D> &path, const QVector<bool> &rapidSegments,
                       const QVector<QVector3D> &segmentStarts);
    void checkResultChanged();
    void currentGCodeLineChanged(int line);

private slots:
    void onTick();

private:
    static QVector<ToolMove> parseGCode(const QString &gcode, bool useProjectTools = true, QString *error = nullptr);
    static SimulationCheckResult checkMotion(const MeshData &mesh, const QVector<ToolMove> &moves);
    SimulationCheckResult m_checkResult;
    std::optional<SketchVerificationContext> m_sketchContext;
    QString m_checkedContextFingerprint;

    MeshData          m_mesh;
    Coordinate3D         m_modelOrigin; // moves stay in NC coordinates; rendering/collision uses model coordinates.
    QString           m_gcode;
    QString           m_motionError;
    QVector<ToolMove> m_moves;
    QVector<QVector3D> m_path;   // pre-flattened waypoints for rendering
    QVector<bool>      m_rapidSegments;
    int               m_step    = 0;
    bool              m_running = false;
    bool              m_hasUnverifiedRetract = false;
    bool              m_hasUnverifiedG76LateralShift = false;
    bool              m_hasUnverifiedG86SpindleStopRetract = false;
    bool              m_hasUnverifiedSpindleSync = false;
    double            m_speed   = 1.0;
    QTimer           *m_timer;
};
