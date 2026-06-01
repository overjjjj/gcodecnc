#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QList>
#include <QJsonObject>
#include "../import/StepImporter.h"
#include "../strategies/MachiningOperation.h"

class ProjectManager : public QObject
{
    Q_OBJECT
public:
    explicit ProjectManager(QObject *parent = nullptr);

    void setMesh(const MeshData &mesh);
    void setFeatures(const QVector<MachiningFeature> &features);
    void setOperations(const QList<MachiningOperation> &operations);
    void setSourceFilePath(const QString &path);

    const MeshData                   &mesh()       const { return m_mesh; }
    const QVector<MachiningFeature>  &features()   const { return m_features; }
    const QList<MachiningOperation>  &operations() const { return m_operations; }
    const QString                    &sourceFilePath() const { return m_sourceFilePath; }

    bool saveToFile(const QString &path);
    bool loadFromFile(const QString &path);
    bool isModified() const { return m_modified; }

signals:
    void projectChanged();
    void operationsChanged();

private:
    MeshData                  m_mesh;
    QVector<MachiningFeature> m_features;
    QList<MachiningOperation> m_operations;
    QString                   m_sourceFilePath;
    bool                      m_modified = false;
};
