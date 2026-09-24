#include "AppController.h"

#include "../core/ProjectManager.h"
#include "../core/SourceFileFingerprint.h"
#include "../import/StepImporter.h"
#ifdef CNEXT_ENABLE_OCC
#include "../import/FeatureRecognizer.h"
#endif

#include <QFileInfo>

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_projectManager(new ProjectManager(this))
{}

AppController &AppController::instance()
{
    static AppController s_instance;
    return s_instance;
}

// STEP 读取成功后才更新项目，并清空依赖旧模型的工序、程序和 Setup 确认状态。
// 导入器返回失败时只通知界面，保留当前项目数据。
void AppController::importStep(const QString &filePath)
{
    emit statusMessage(tr("正在导入 STEP 文件：%1").arg(QFileInfo(filePath).fileName()));

#ifdef CNEXT_ENABLE_OCC
    if (!m_importer.load(filePath)) {
        emit errorOccurred(tr("STEP 导入失败：%1").arg(m_importer.lastError()));
        return;
    }

    m_projectManager->setMesh(m_importer.mesh());
    m_projectManager->setFeatures(m_importer.features());
    m_projectManager->setOperations({});
    m_projectManager->setPrograms({});
    m_projectManager->setSourceFilePath(filePath);
    m_projectManager->setSourceFileFingerprint(SourceFileFingerprint::calculate(filePath));
    m_projectManager->setSetupRotation(QQuaternion());
    m_projectManager->setActiveRegion(FaceRegion::Unknown);
    m_projectManager->setWorkOffset(QStringLiteral("G54"));
    m_projectManager->setSetupOrigin(SetupOrigin());
    m_projectManager->setStockDefinition(StockDefinition());
    emit stepImported(filePath);
    emit statusMessage(tr("导入完成，识别到 %1 个特征。").arg(m_importer.features().size()));
#else
    StepImporter importer;
    if (!importer.load(filePath)) {
        emit errorOccurred(tr("STEP 导入失败：%1").arg(importer.lastError()));
        return;
    }

    m_projectManager->setMesh(importer.mesh());
    m_projectManager->setFeatures(importer.features());
    m_projectManager->setOperations({});
    m_projectManager->setPrograms({});
    m_projectManager->setSourceFilePath(filePath);
    m_projectManager->setSourceFileFingerprint(SourceFileFingerprint::calculate(filePath));
    m_projectManager->setSetupRotation(QQuaternion());
    m_projectManager->setActiveRegion(FaceRegion::Unknown);
    m_projectManager->setWorkOffset(QStringLiteral("G54"));
    m_projectManager->setSetupOrigin(SetupOrigin());
    m_projectManager->setStockDefinition(StockDefinition());
    emit stepImported(filePath);
    emit statusMessage(tr("导入完成，识别到 %1 个特征。").arg(importer.features().size()));
#endif
}

#ifdef CNEXT_ENABLE_OCC
// 使用当前导入器保留的拓扑，按新正面法向重新划分特征区域。
void AppController::reclassifyFeatures(const QVector3D &frontNormal)
{
    FeatureRecognizer recognizer;
    recognizer.setFrontNormal(frontNormal);

    QVector<MachiningFeature> features = m_importer.features();
    recognizer.reclassifyRegions(m_importer.topoGraph(), features);

    m_projectManager->setFeatures(features);
    emit featuresReclassified();
}

// 在临时导入器中旋转并重新识别；成功后替换模型，旧工序和程序需重新建立。
bool AppController::reloadStepWithSetupRotation(const QQuaternion &rotation, QString *error)
{
    const QString sourcePath = m_projectManager->sourceFilePath();
    if (sourcePath.isEmpty()) {
        if (error) {
            *error = tr("No source STEP file is available for setup re-recognition.");
        }
        return false;
    }

    StepImporter importer;
    if (!importer.loadWithRotation(sourcePath, rotation)) {
        if (error) {
            *error = importer.lastError();
        }
        return false;
    }

    m_importer = importer;
    m_projectManager->setMesh(m_importer.mesh());
    m_projectManager->setFeatures(m_importer.features());
    m_projectManager->setOperations({});
    m_projectManager->setPrograms({});
    m_projectManager->setSourceFilePath(sourcePath);
    m_projectManager->setSourceFileFingerprint(SourceFileFingerprint::calculate(sourcePath));
    m_projectManager->setSetupRotation(rotation);
    m_projectManager->setActiveRegion(FaceRegion::Front);
    m_projectManager->setSetupOrigin(SetupOrigin());
    m_projectManager->setStockDefinition(StockDefinition());
    emit featuresReclassified();
    emit statusMessage(tr("Setup features re-recognized: %1").arg(m_importer.features().size()));
    return true;
}
#endif

// 此入口目前仅提示状态，不生成程序，也不发出 gcodeReady。
void AppController::generateGCode()
{
    emit statusMessage(tr("正在生成 G 代码..."));
}

// 首次保存缺少源指纹的项目时补录文件摘要；无法读取源文件则取消保存。
bool AppController::saveProject(const QString &path)
{
    if (m_projectManager->sourceFileFingerprint().isEmpty()
        && !m_projectManager->sourceFilePath().isEmpty()) {
        QString fingerprintError;
        const QString fingerprint =
            SourceFileFingerprint::calculate(m_projectManager->sourceFilePath(), &fingerprintError);
        if (fingerprint.isEmpty()) {
            emit errorOccurred(tr("Cannot fingerprint the source STEP file: %1").arg(fingerprintError));
            return false;
        }
        m_projectManager->setSourceFileFingerprint(fingerprint);
    }

    if (!m_projectManager->saveToFile(path)) {
        emit errorOccurred(tr("Failed to save project: %1").arg(path));
        return false;
    }
    emit statusMessage(tr("项目已保存：%1").arg(path));
    return true;
}

// 先在临时项目中读取来源和 Setup，再校验、重导入 STEP；这些步骤失败时保留当前项目。
bool AppController::loadProject(const QString &path, const QString &replacementSourcePath)
{
    m_lastProjectLoadIssue = ProjectLoadIssue::None;
    ProjectManager staged;
    if (!staged.loadFromFile(path)) {
        m_lastProjectLoadIssue = ProjectLoadIssue::ProjectFileInvalid;
        emit errorOccurred(tr("Failed to load project file: %1").arg(path));
        return false;
    }

    const bool hasReplacementSource = !replacementSourcePath.trimmed().isEmpty();
    const QString sourcePath = !hasReplacementSource
                                   ? staged.sourceFilePath()
                                   : replacementSourcePath;
    if (sourcePath.isEmpty()) {
        m_lastProjectLoadIssue = ProjectLoadIssue::SourceMissing;
        emit errorOccurred(tr("The project does not reference a source STEP file."));
        return false;
    }

    QString fingerprintError;
    const QString actualFingerprint =
        SourceFileFingerprint::calculate(sourcePath, &fingerprintError);
    if (actualFingerprint.isEmpty()) {
        m_lastProjectLoadIssue = ProjectLoadIssue::SourceMissing;
        emit errorOccurred(tr("Cannot open the source STEP file: %1").arg(fingerprintError));
        return false;
    }
    const bool fingerprintChanged = !staged.sourceFileFingerprint().isEmpty()
                                    && staged.sourceFileFingerprint() != actualFingerprint;
    if (fingerprintChanged && !hasReplacementSource) {
        m_lastProjectLoadIssue = ProjectLoadIssue::SourceChanged;
        emit errorOccurred(tr("The source STEP file has changed. Re-recognition is required before old operations can be used."));
        return false;
    }
    // 相同内容迁移路径可保留工序；显式换成不同内容或无法核对旧指纹时，清空派生数据。
    const bool invalidateDerivedData = hasReplacementSource
                                       && (staged.sourceFileFingerprint().isEmpty()
                                           || fingerprintChanged);

    StepImporter importer;
    if (!importer.loadWithRotation(sourcePath, staged.setupRotation())) {
        m_lastProjectLoadIssue = ProjectLoadIssue::SourceImportFailed;
        emit errorOccurred(tr("STEP re-import failed: %1").arg(importer.lastError()));
        return false;
    }

    if (!m_projectManager->loadFromFile(path)) {
        m_lastProjectLoadIssue = ProjectLoadIssue::ProjectFileInvalid;
        emit errorOccurred(tr("Failed to apply project data: %1").arg(path));
        return false;
    }
    if (invalidateDerivedData) {
        m_projectManager->replaceChangedSource(importer.mesh(),
                                               importer.features(),
                                               sourcePath,
                                               actualFingerprint);
    } else {
        m_projectManager->setLoadedMesh(importer.mesh());
    }
    if (!invalidateDerivedData
        && (m_projectManager->sourceFilePath() != sourcePath
            || m_projectManager->sourceFileFingerprint().isEmpty())) {
        m_projectManager->setSourceFilePath(sourcePath);
        m_projectManager->setSourceFileFingerprint(actualFingerprint);
    }
#ifdef CNEXT_ENABLE_OCC
    m_importer = importer;
#endif
    emit statusMessage(invalidateDerivedData
                           ? tr("Source STEP relinked and re-recognized. Previous operations and programs were cleared.")
                           : tr("项目已加载：%1").arg(path));
    return true;
}
