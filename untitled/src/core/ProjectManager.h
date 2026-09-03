#pragma once
#include <QObject>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QList>
#include <QJsonObject>
#include <QQuaternion>
#include <QStringList>
#include "MachineProfile.h"
#include "SetupOrigin.h"
#include "StockDefinition.h"
#include "../import/StepImporter.h"
#include "../strategies/MachiningOperation.h"
#include "../gcode/ParametricToolpathProgram.h"

struct ProgramFileEntry
{
    QString kind;
    QString fileName;
    QString content;
    QString sha256;
};

struct ProgramEntry
{
    QString id;
    QString name;
    QString postProcessorId;
    QStringList sourceOperationIds;
    QString sourceOperationFingerprint;
    QString setupFingerprint;
    QString machineProfileId;
    QString machineProfileVersion;
    QStringList safeStartBlocks;
    QString sourceSummary;
    QString gcodeText;
    QString expandedGcodeText;
    QString macroText;
    QString mainProgramFileName;
    QList<ProgramFileEntry> packageFiles;
    QList<ParametricToolpathProgram> parametricPrograms;
    int lineCount = 0;
    int expandedLineCount = 0;
    QDateTime createdAt;
};

class ProjectManager : public QObject
{
    Q_OBJECT
public:
    explicit ProjectManager(QObject *parent = nullptr);

    void setMesh(const MeshData &mesh);
    void setLoadedMesh(const MeshData &mesh);
    void setFeatures(const QVector<MachiningFeature> &features);
    void setOperations(const QList<MachiningOperation> &operations);
    void setPrograms(const QList<ProgramEntry> &programs);
    void setCurrentProgramId(const QString &programId);
    void setSourceFilePath(const QString &path);
    void setSourceFileFingerprint(const QString &fingerprint);
    void setSetupRotation(const QQuaternion &rotation);
    void setActiveRegion(FaceRegion region);
    void setWorkOffset(const QString &workOffset);
    void setSetupOrigin(const SetupOrigin &origin);
    void setStockDefinition(const StockDefinition &stock);
    // 中文说明：源模型变化时原子替换模型与特征，并使依赖旧几何的工序/程序失效。
    // 不删除历史程序，便于追溯；调用方必须要求用户重新确认工艺。
    void replaceChangedSource(const MeshData &mesh,
                              const QVector<MachiningFeature> &features,
                              const QString &sourceFilePath,
                              const QString &sourceFileFingerprint);
    void setMachineProfile(const MachineProfile &profile);
    QString upsertProgram(ProgramEntry program);

    const MeshData                   &mesh()       const { return m_mesh; }
    const QVector<MachiningFeature>  &features()   const { return m_features; }
    const QList<MachiningOperation>  &operations() const { return m_operations; }
    const QList<ProgramEntry>        &programs()   const { return m_programs; }
    const QString                    &currentProgramId() const { return m_currentProgramId; }
    const QString                    &sourceFilePath() const { return m_sourceFilePath; }
    const QString                    &sourceFileFingerprint() const { return m_sourceFileFingerprint; }
    const QQuaternion                &setupRotation() const { return m_setupRotation; }
    FaceRegion                        activeRegion() const { return m_activeRegion; }
    const QString                    &workOffset() const { return m_workOffset; }
    const SetupOrigin                &setupOrigin() const { return m_setupOrigin; }
    const StockDefinition            &stockDefinition() const { return m_stockDefinition; }
    QString setupFingerprint() const;
    const MachineProfile             &machineProfile() const { return m_machineProfile; }
    ProgramEntry programById(const QString &id) const;

    // 中文说明：保存完整项目快照，包括模型指纹、Setup、毛坯、参数来源和程序状态。
    bool saveToFile(const QString &path);
    // 中文说明：加载后验证源文件指纹；缺失或变化时返回问题状态，不静默使用旧几何。
    bool loadFromFile(const QString &path);
    bool isModified() const { return m_modified; }

signals:
    void projectChanged();
    void operationsChanged();
    void programsChanged();

private:
    MeshData                  m_mesh;
    QVector<MachiningFeature> m_features;
    QList<MachiningOperation> m_operations;
    QList<ProgramEntry>       m_programs;
    QString                   m_currentProgramId;
    QString                   m_sourceFilePath;
    QString                   m_sourceFileFingerprint;
    QQuaternion               m_setupRotation;
    FaceRegion                m_activeRegion = FaceRegion::Unknown;
    QString                   m_workOffset = QStringLiteral("G54");
    SetupOrigin               m_setupOrigin;
    StockDefinition           m_stockDefinition;
    MachineProfile            m_machineProfile;
    bool                      m_modified = false;

    void invalidateOperations(const QString &reason);
};
