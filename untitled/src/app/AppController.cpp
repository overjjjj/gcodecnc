#include "AppController.h"

#include "../core/ProjectManager.h"
#include "../core/SourceFileFingerprint.h"
#include "../import/StepImporter.h"
#ifdef CNEXT_ENABLE_OCC
#include "../import/FeatureRecognizer.h"
#endif

#include <QFileInfo>
#include <QSet>

// 函数说明：AppController负责当前模块的核心处理。
AppController::AppController(QObject *parent)
    // 函数说明：QObject负责当前模块的核心处理。
    : QObject(parent)
    // 函数说明：m_projectManager负责当前模块的核心处理。
    , m_projectManager(new ProjectManager(this))
{}

// 函数说明：instance负责当前模块的核心处理。
AppController &AppController::instance()
{
    static AppController s_instance;
    return s_instance;
}

// 中文说明：导入入口负责把文件读取、几何分类和项目状态更新串成原子流程；
// 任何失败都通过 errorOccurred 通知界面，不覆盖上一份有效模型。
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
    m_projectManager->setStepSource(filePath, SourceFileFingerprint::calculate(filePath));
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
    m_projectManager->setStepSource(filePath, SourceFileFingerprint::calculate(filePath));
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
// 函数说明：reclassifyFeatures负责当前模块的核心处理。
void AppController::reclassifyFeatures(const QVector3D &frontNormal)
{
    if (m_projectManager->sourceType() == ProjectSourceType::Sketch) return;
    FeatureRecognizer recognizer;
    recognizer.setFrontNormal(frontNormal);

    QVector<MachiningFeature> features = m_importer.features();
    recognizer.reclassifyRegions(m_importer.topoGraph(), features);

    m_projectManager->setFeatures(features);
    emit featuresReclassified();
}

// 函数说明：reloadStepWithSetupRotation负责当前模块的核心处理。
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

// 中文说明：UI 的程序生成槽函数只做命令编排，实际安全校验和快照生成下沉到服务层。
void AppController::generateGCode()
{
    emit statusMessage(tr("正在生成 G 代码..."));
}

// 函数说明：saveProject负责当前模块的核心处理。
bool AppController::saveProject(const QString &path)
{
    if (m_projectManager->sourceFileFingerprint().isEmpty()
        // 函数说明：sourceFilePath负责当前模块的核心处理。
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

// 函数说明：loadProject负责当前模块的核心处理。
bool AppController::loadProject(const QString &path, const QString &replacementSourcePath)
{
    m_lastProjectLoadIssue = ProjectLoadIssue::None;
    ProjectManager staged;
    if (!staged.loadFromFile(path)) {
        m_lastProjectLoadIssue = ProjectLoadIssue::ProjectFileInvalid;
        emit errorOccurred(tr("Failed to load project file: %1").arg(path));
        return false;
    }

    if (staged.sourceType() == ProjectSourceType::Sketch) {
        if (!replacementSourcePath.trimmed().isEmpty()) {
            m_lastProjectLoadIssue = ProjectLoadIssue::ProjectFileInvalid;
            emit errorOccurred(tr("A sketch project cannot be relinked to a STEP file."));
            return false;
        }
        m_projectManager->adoptLoadedState(staged);
#ifdef CNEXT_ENABLE_OCC
        m_importer = StepImporter();
#endif
        emit statusMessage(tr("二维草图项目已加载：%1").arg(path));
        return true;
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
        const auto correctedCapHole = [&importer](const MachiningFeature &old)
                -> const MachiningFeature * {
            if (old.kind != FeatureKind::Hole || old.coordinatePrecisionVersion != 1 ||
                old.faceIndices.isEmpty() || old.subType == QStringLiteral("enclosed_hole"))
                return nullptr;
            const QSet<int> oldFaces(old.faceIndices.cbegin(), old.faceIndices.cend());
            QSet<int> enclosedFaces;
            const MachiningFeature *enclosedEvidence = nullptr;
            for (const auto &current : importer.features()) {
                if (current.kind != FeatureKind::Hole) continue;
                const QSet<int> currentFaces(current.faceIndices.cbegin(), current.faceIndices.cend());
                if (old.subType.contains(QStringLiteral("through_hole")) && current.center == old.center &&
                    current.radius == old.radius &&
                    currentFaces == oldFaces &&
                    (current.subType.contains(QStringLiteral("blind_hole")) ||
                     current.subType == QStringLiteral("enclosed_hole"))) return &current;
                // Old counterbore merging could join independent closed voids.
                // Only complete coverage of the old CAD faces can invalidate it
                // as an enclosed source without guessing a replacement opening.
                if (current.subType == QStringLiteral("enclosed_hole") && !currentFaces.isEmpty() &&
                    (currentFaces - oldFaces).isEmpty()) {
                    enclosedFaces.unite(currentFaces);
                    enclosedEvidence = &current;
                }
            }
            if (enclosedEvidence && enclosedFaces == oldFaces) return enclosedEvidence;
            return nullptr;
        };
        const auto oldCounterboreCenter = [&importer](const MachiningFeature &old) {
            if (old.kind != FeatureKind::Hole || old.coordinatePrecisionVersion != 1 ||
                old.faceIndices.isEmpty() || !old.subType.startsWith(QStringLiteral("counterbore_")))
                return false;
            const QSet<int> oldFaces(old.faceIndices.cbegin(), old.faceIndices.cend());
            for (const auto &current : importer.features()) {
                if (!current.subType.startsWith(QStringLiteral("counterbore_")) ||
                    current.radius != old.radius || current.secondaryRadius != old.secondaryRadius ||
                    current.depth != old.depth || current.center == old.center ||
                    QSet<int>(current.faceIndices.cbegin(), current.faceIndices.cend()) != oldFaces) continue;
                const Coordinate3D halfAxis = Coordinate3D(current.axis.normalized()) * (current.depth * 0.5);
                if ((old.center - current.center - halfAxis).length() < 1e-8 ||
                    (old.center - current.center + halfAxis).length() < 1e-8) return true;
            }
            return false;
        };
        for (const auto &feature : m_projectManager->features()) {
            if ((feature.kind == FeatureKind::Hole || feature.kind == FeatureKind::Thread) &&
                (feature.axis.isNull() ||
                 (feature.region == FaceRegion::Unknown && feature.subType != QStringLiteral("enclosed_hole")) ||
                 feature.coordinatePrecisionVersion != 1 || correctedCapHole(feature) || oldCounterboreCenter(feature))) {
                // Restore selectable source geometry; unproven old operations stay stale.
                m_projectManager->setFeatures(importer.features());
                break;
            }
        }
        auto operations = m_projectManager->operations();
        bool classificationChanged = false;
        for (auto &operation : operations) {
            if (operation.opType != OperationType::Hole) continue;
            const auto *current = correctedCapHole(operation.holeFeature);
            if (!current) {
                if (oldCounterboreCenter(operation.holeFeature)) {
                    // Preserve the old numbers as history, without certifying the
                    // former entry point as a current machining center.
                    operation.holeFeature.coordinatePrecisionVersion = 0;
                    operation.markToolpathStale(tr("Counterbore center evidence changed; select the current source hole again."));
                    classificationChanged = true;
                }
                continue;
            }
            operation.holeFeature.subType = current->subType;
            operation.holeFeature.region = current->region;
            operation.holeFeature.axis = current->axis;
            operation.markToolpathStale(tr("Source hole classification changed; review the re-recognized hole before recalculation."));
            classificationChanged = true;
        }
        if (classificationChanged) m_projectManager->setOperations(operations);
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
