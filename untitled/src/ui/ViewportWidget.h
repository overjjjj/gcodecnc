#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_1_1>
#include <QMatrix4x4>
#include <QSet>
#include <QVector3D>
#include "../import/StepImporter.h"

class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_1_1
{
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget *parent = nullptr);

    void setMesh(const MeshData &mesh);
    void setHighlightedFaces(const QVector<int> &faceIndices);
    void clearHighlight();
    void resetCamera();

    void setToolPath(const QVector<QVector3D> &path, const QVector<bool> &rapidSegments);
    void setToolPosition(const QVector3D &pos);
    void setToolDiameter(double diameter);
    void setToolModelPath(const QString &path);
    void clearToolPath();

protected:
    void initializeGL()  override;
    void resizeGL(int w, int h) override;
    void paintGL()       override;

    void mousePressEvent(QMouseEvent *e)   override;
    void mouseMoveEvent(QMouseEvent *e)    override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e)        override;

signals:
    void facePicked(int faceIndex);
    void facePointPicked(int faceIndex, QVector3D point);

private:
    void drawGrid();
    void drawMesh();
    void drawAxes();
    void drawToolPath();
    void drawToolMarker();
    int pickFaceAt(const QPoint &pos, QVector3D *hitPoint = nullptr) const;

    MeshData   m_mesh;
    QSet<int>  m_highlightedFaces;
    QMatrix4x4 m_proj;
    QMatrix4x4 m_view;

    QVector<QVector3D> m_toolPath;
    QVector<bool>      m_toolPathRapidSegments;
    QVector3D          m_toolPos;
    bool               m_hasToolPos = false;
    float              m_toolDiameter = 6.0f;
    MeshData           m_toolModel;
    QString            m_toolModelPath;

    float m_rotX   = 30.0f;
    float m_rotZ   = -45.0f;
    float m_zoom   = 1.0f;
    QVector3D m_pan;
    QPoint    m_lastMouse;
    QPoint    m_pressMouse;
    int       m_mouseButton = 0;
};
