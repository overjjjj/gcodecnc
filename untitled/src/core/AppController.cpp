#include "AppController.h"

#include "ProjectManager.h"
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
    m_projectManager->setSourceFilePath(filePath);
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
    m_projectManager->setSourceFilePath(filePath);
    emit stepImported(filePath);
    emit statusMessage(tr("导入完成，识别到 %1 个特征。").arg(importer.features().size()));
#endif
}

#ifdef CNEXT_ENABLE_OCC
void AppController::reclassifyFeatures(const QVector3D &frontNormal)
{
    FeatureRecognizer recognizer;
    recognizer.setFrontNormal(frontNormal);

    QVector<MachiningFeature> features = m_importer.features();
    recognizer.reclassifyRegions(m_importer.topoGraph(), features);

    m_projectManager->setFeatures(features);
    emit featuresReclassified();
}

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
    m_projectManager->setSourceFilePath(sourcePath);
    emit featuresReclassified();
    emit statusMessage(tr("Setup features re-recognized: %1").arg(m_importer.features().size()));
    return true;
}
#endif

void AppController::generateGCode()
{
    emit statusMessage(tr("正在生成 G 代码..."));
}

void AppController::saveProject(const QString &path)
{
    m_projectManager->saveToFile(path);
    emit statusMessage(tr("项目已保存：%1").arg(path));
}

void AppController::loadProject(const QString &path)
{
    m_projectManager->loadFromFile(path);
    emit statusMessage(tr("项目已加载：%1").arg(path));
}
