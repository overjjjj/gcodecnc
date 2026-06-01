#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include "ProjectManager.h"
#ifdef CNEXT_ENABLE_OCC
#include "../import/StepImporter.h"
#include <QQuaternion>
#include <QVector3D>
#endif

class AppController : public QObject
{
    Q_OBJECT
public:
    static AppController &instance();

    ProjectManager *projectManager() { return m_projectManager; }

    void importStep(const QString &filePath);
    void generateGCode();
    void saveProject(const QString &path);
    void loadProject(const QString &path);
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
};
