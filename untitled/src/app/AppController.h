#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include "../core/ProjectManager.h"
#ifdef CNEXT_ENABLE_OCC
#include "../import/StepImporter.h"
#include <QQuaternion>
#include <QVector3D>
#endif

enum class ProjectLoadIssue {
    None,
    ProjectFileInvalid,
    SourceMissing,
    SourceChanged,
    SourceImportFailed
};

class AppController : public QObject
{
    Q_OBJECT
public:
    static AppController &instance();

    ProjectManager *projectManager() { return m_projectManager; }

    // 中文说明：导入 STEP 后更新项目模型、特征候选和界面状态；失败时保留原项目。
    void importStep(const QString &filePath);
    // 兼容占位入口，目前只发送状态消息；实际生成由 ProgramGenerationService 承担。
    void generateGCode();
    bool saveProject(const QString &path);
    bool loadProject(const QString &path, const QString &replacementSourcePath = QString());
    ProjectLoadIssue lastProjectLoadIssue() const { return m_lastProjectLoadIssue; }
#ifdef CNEXT_ENABLE_OCC
    void reclassifyFeatures(const QVector3D &frontNormal);
    bool reloadStepWithSetupRotation(const QQuaternion &rotation, QString *error = nullptr);
    const StepImporter &importer() const { return m_importer; }
#endif

signals:
    void stepImported(const QString &filePath);
    void gcodeReady(const QString &gcode);
    void statusMessage(const QString &msg);
    void errorOccurred(const QString &msg);
#ifdef CNEXT_ENABLE_OCC
    void featuresReclassified();
#endif

private:
    explicit AppController(QObject *parent = nullptr);
    ProjectManager *m_projectManager;
#ifdef CNEXT_ENABLE_OCC
    StepImporter m_importer;
#endif
    ProjectLoadIssue m_lastProjectLoadIssue = ProjectLoadIssue::None;
};
