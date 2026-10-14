#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_1_1>
#include <QMatrix4x4>
#include <QSet>
#include <QVector3D>
#include "../../../import/StepImporter.h"

class QLabel;

class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions_1_1
{
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget *parent = nullptr);

    enum class TopologyPickMode { Face = 0, Edge = 1, Vertex = 2 };

    void setMesh(const MeshData &mesh);
    void setModelVisible(bool visible);
    bool modelVisible() const { return m_modelVisible; }
    void setHighlightedFaces(const QVector<int> &faceIndices);
    void clearHighlight();
    void resetCamera();
    enum class StandardView { Front, Back, Left, Right, Top, Bottom, Isometric };
    void setStandardView(StandardView view);
    void fitAll();

    void setToolPath(const QVector<QVector3D> &path, const QVector<bool> &rapidSegments,
                     const QVector<QVector3D> &segmentStarts = {});
    void setToolPathVisibility(const QVector<bool> &visible);
    void setToolPosition(const QVector3D &pos);
    void setToolDiameter(double diameter);
    void setToolModelPath(const QString &path);
    void setToolGeometryVisible(bool visible);
    void clearToolPath();
    void setContourChoicePreview(const QVector<QVector3D> &points,
                                 bool closed,
                                 int cutterSide,
                                 bool candidate = false);
    void clearContourChoicePreview();
    void setConfirmedContourPreview(const QVector<QVector3D> &points, bool closed);
    void clearConfirmedContourPreview();
    void setTopologyHoverPreview(const QVector<QVector3D> &points);
    void setTopologyPickFilter(const QStringList &edgeIds,
                               const QStringList &vertexIds,
                               const QVector<int> &faceIndices);
    void clearTopologyPickFilter();

public slots:
    void setTopologyPickMode(int mode);

protected:
    void initializeGL()  override;
    void resizeGL(int w, int h) override;
    void paintGL()       override;

    void mousePressEvent(QMouseEvent *e)   override;
    void mouseMoveEvent(QMouseEvent *e)    override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e)        override;
    void leaveEvent(QEvent *e) override;

signals:
    void modelVisibilityChanged(bool visible);
    void facePicked(int faceIndex);
    void facePointPicked(int faceIndex, QVector3D point);
    void topologyPicked(QString topologyId, int kind, QVector3D point, double curveParameter);
    void topologyHovered(QString topologyId, int kind, QVector3D point, double curveParameter);

private:
    void updateProjection();
    QMatrix4x4 currentViewMatrix() const;
    void drawOrientation();
    void drawGrid();
    void drawMesh();
    void drawAxes();
    void drawToolPath();
    void drawToolMarker();
    void drawContourChoicePreview();
    void drawSelectableTopology();
    int pickFaceAt(const QPointF &pos, QVector3D *hitPoint = nullptr,
                   float *ndcDepth = nullptr) const;
    bool topologyPointVisible(const QVector3D &point, const QPointF &screen) const;
    bool pickEdgeAt(const QPoint &pos, QString *id, QVector3D *hitPoint,
                    double *curveParameter) const;
    bool pickVertexAt(const QPoint &pos, QString *id, QVector3D *hitPoint) const;

    QLabel *m_coordinateLabel = nullptr;
    QPoint m_hoverPosition;
    bool m_hasHover = false;
    MeshData   m_mesh;
    bool m_modelVisible = true;
    QSet<int>  m_highlightedFaces;
    QMatrix4x4 m_proj;
    QMatrix4x4 m_view;

    QVector<QVector3D> m_toolPath;
    QVector<QVector3D> m_toolPathStarts;
    QVector<bool>      m_toolPathRapidSegments;
    QVector<bool>      m_toolPathVisible;
    QVector3D          m_toolPos;
    bool               m_hasToolPos = false;
    bool               m_toolGeometryVisible = true;
    float              m_toolDiameter = 6.0f;
    MeshData           m_toolModel;
    QString            m_toolModelPath;
    QVector<QVector3D> m_contourChoicePoints;
    bool               m_contourChoiceClosed = false;
    int                m_contourChoiceSide = 1;
    bool               m_contourChoiceCandidate = false;
    QVector<QVector3D> m_confirmedContourPoints;
    bool               m_confirmedContourClosed = false;
    QVector<QVector3D> m_topologyHoverPoints;

    bool m_autoFit = true;
    QString m_viewLabel;
    float m_rotX   = 30.0f;
    float m_rotZ   = -45.0f;
    float m_zoom   = 1.0f;
    QVector3D m_pan;
    QPoint    m_lastMouse;
    QPoint    m_pressMouse;
    bool      m_dragged = false;
    int       m_mouseButton = 0;
    TopologyPickMode m_topologyPickMode = TopologyPickMode::Face;
    bool m_topologyPickFilterActive = false;
    QSet<QString> m_selectableTopologyEdges;
    QSet<QString> m_selectableTopologyVertices;
    QSet<int> m_selectableTopologyFaces;
};
