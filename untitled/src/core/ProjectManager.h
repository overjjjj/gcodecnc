#pragma once
#include <QObject>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QList>
#include <QJsonObject>
#include <QQuaternion>
#include <QStringList>
#include <QSet>
#include "../workspaces/machine_operation/model/MachineProfile.h"
#include "SetupOrigin.h"
#include "StockDefinition.h"
#include "../import/StepImporter.h"
#include "../workspaces/process_design/operations/MachiningOperation.h"
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
    // 记录生成来源及其顺序；工序、刀具和 Setup 分别保留摘要，供输出前核对。
    QStringList sourceOperationIds;
    QString sourceOperationFingerprint;
    QString toolFingerprint;
    QString setupFingerprint;
    QString machineProfileId;
    QString machineProfileVersion;
    QStringList safeStartBlocks;
    QString sourceSummary;
    QString gcodeText;          // 面向目标控制器的主程序文本。
    QString expandedGcodeText;  // 使用宏压缩时保留展开文本；与主程序相同时为空。
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
    void invalidateToolOperations(const QSet<int> &toolIds);
    void setPrograms(const QList<ProgramEntry> &programs);
    void setCurrentProgramId(const QString &programId);
    void setSourceFilePath(const QString &path);
    void setSourceFileFingerprint(const QString &fingerprint);
    void setSetupRotation(const QQuaternion &rotation);
    void setActiveRegion(FaceRegion region);
    void setWorkOffset(const QString &workOffset);
    void setSetupOrigin(const SetupOrigin &origin);
    void setStockDefinition(const StockDefinition &stock);
    // 替换模型、特征和来源指纹，清空旧工序、程序及当前程序 ID，再通知界面刷新。
    // 保留当前 Setup 配置；新的工艺需重新确认。
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

    // 保存源文件信息、特征、Setup、毛坯、工序和程序；网格由源 STEP 重建，不写入 JSON。
    bool saveToFile(const QString &path);
    // 校验 JSON 基本结构并恢复项目字段；源文件校验和 STEP 重导入由 AppController 负责。
    bool loadFromFile(const QString &path);
    bool isModified() const { return m_modified; }

signals:
    void machiningContextChanged();
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
