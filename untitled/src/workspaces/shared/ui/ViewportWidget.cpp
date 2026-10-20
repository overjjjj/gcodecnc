#include "ViewportWidget.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QRegularExpression>
#include <QTextStream>
#include <QVector4D>
#include <QWheelEvent>
#include <QPainter>
#include <QLabel>
#include "../../../core/Settings.h"
#include <QtGlobal>
#include <cmath>
#include <limits>

// 函数说明：ViewportWidget负责当前模块的核心处理。
ViewportWidget::ViewportWidget(QWidget *parent)
    // 函数说明：QOpenGLWidget负责当前模块的核心处理。
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    m_coordinateLabel = new QLabel(QStringLiteral("X: —   Y: —   mm"), this);
    m_coordinateLabel->setObjectName(QStringLiteral("mouseCoordinates"));
    m_coordinateLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_coordinateLabel->setStyleSheet(QStringLiteral("QLabel { background: #ffffff; color: #27364d; border: 1px solid #c6d0df; border-radius: 4px; padding: 5px 8px; }"));
    m_coordinateLabel->setToolTip(tr("模型表面坐标（毫米），不是机床实时坐标"));
    m_coordinateLabel->adjustSize();
}

namespace {

// 函数说明：normalizeToolMesh负责当前模块的核心处理。
static void normalizeToolMesh(MeshData &mesh)
{
    if (mesh.triangles.isEmpty()) {
        return;
    }

    QVector3D bbMin(std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max());
    QVector3D bbMax(-std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max());

    auto includePoint = [&](const QVector3D &p) {
        bbMin.setX(std::min(bbMin.x(), p.x()));
        bbMin.setY(std::min(bbMin.y(), p.y()));
        bbMin.setZ(std::min(bbMin.z(), p.z()));
        bbMax.setX(std::max(bbMax.x(), p.x()));
        bbMax.setY(std::max(bbMax.y(), p.y()));
        bbMax.setZ(std::max(bbMax.z(), p.z()));
    };

    for (const Triangle &tri : mesh.triangles) {
        includePoint(tri.v0);
        includePoint(tri.v1);
        includePoint(tri.v2);
    }

    const QVector3D shift((bbMin.x() + bbMax.x()) * -0.5f,
                          (bbMin.y() + bbMax.y()) * -0.5f,
                          -bbMin.z());
    for (Triangle &tri : mesh.triangles) {
        tri.v0 += shift;
        tri.v1 += shift;
        tri.v2 += shift;
        tri.normal = QVector3D::normal(tri.v0, tri.v1, tri.v2);
    }

    mesh.bbMin = bbMin + shift;
    mesh.bbMax = bbMax + shift;
}

// 函数说明：loadObjToolModel负责当前模块的核心处理。
static bool loadObjToolModel(const QString &path, MeshData &mesh)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QVector<QVector3D> vertices;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("v "))) {
            const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (parts.size() >= 4) {
                vertices.append(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
            }
        } else if (line.startsWith(QLatin1String("f "))) {
            const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            QVector<int> indices;
            for (int i = 1; i < parts.size(); ++i) {
                const QString vertexIndex = parts[i].section('/', 0, 0);
                int idx = vertexIndex.toInt();
                if (idx < 0) {
                    idx = vertices.size() + idx + 1;
                }
                if (idx > 0 && idx <= vertices.size()) {
                    indices.append(idx - 1);
                }
            }
            for (int i = 1; i + 1 < indices.size(); ++i) {
                Triangle tri;
                tri.v0 = vertices[indices[0]];
                tri.v1 = vertices[indices[i]];
                tri.v2 = vertices[indices[i + 1]];
                tri.normal = QVector3D::normal(tri.v0, tri.v1, tri.v2);
                mesh.triangles.append(tri);
            }
        }
    }

    normalizeToolMesh(mesh);
    return !mesh.triangles.isEmpty();
}

// 函数说明：loadAsciiStlToolModel负责当前模块的核心处理。
static bool loadAsciiStlToolModel(const QString &path, MeshData &mesh)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QVector<QVector3D> facetVertices;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.startsWith(QLatin1String("vertex "))) {
            continue;
        }

        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 4) {
            continue;
        }
        facetVertices.append(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
        if (facetVertices.size() == 3) {
            Triangle tri;
            tri.v0 = facetVertices[0];
            tri.v1 = facetVertices[1];
            tri.v2 = facetVertices[2];
            tri.normal = QVector3D::normal(tri.v0, tri.v1, tri.v2);
            mesh.triangles.append(tri);
            facetVertices.clear();
        }
    }

    normalizeToolMesh(mesh);
    return !mesh.triangles.isEmpty();
}

// 函数说明：loadBinaryStlToolModel负责当前模块的核心处理。
static bool loadBinaryStlToolModel(const QString &path, MeshData &mesh)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    if (file.size() < 84) {
        return false;
    }

    file.seek(80);
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setFloatingPointPrecision(QDataStream::SinglePrecision);

    quint32 triangleCount = 0;
    in >> triangleCount;
    const qint64 expectedSize = 84 + qint64(triangleCount) * 50;
    if (triangleCount == 0 || expectedSize != file.size()) {
        return false;
    }

    mesh.triangles.reserve(int(triangleCount));
    for (quint32 i = 0; i < triangleCount; ++i) {
        float nx = 0.0f, ny = 0.0f, nz = 0.0f;
        float x0 = 0.0f, y0 = 0.0f, z0 = 0.0f;
        float x1 = 0.0f, y1 = 0.0f, z1 = 0.0f;
        float x2 = 0.0f, y2 = 0.0f, z2 = 0.0f;
        quint16 attribute = 0;
        in >> nx >> ny >> nz;
        in >> x0 >> y0 >> z0;
        in >> x1 >> y1 >> z1;
        in >> x2 >> y2 >> z2;
        in >> attribute;

        if (in.status() != QDataStream::Ok) {
            mesh = MeshData{};
            return false;
        }

        Triangle tri;
        tri.v0 = QVector3D(x0, y0, z0);
        tri.v1 = QVector3D(x1, y1, z1);
        tri.v2 = QVector3D(x2, y2, z2);
        tri.normal = QVector3D(nx, ny, nz);
        if (tri.normal.lengthSquared() <= 1.0e-8f) {
            tri.normal = QVector3D::normal(tri.v0, tri.v1, tri.v2);
        } else {
            tri.normal.normalize();
        }
        mesh.triangles.append(tri);
    }

    normalizeToolMesh(mesh);
    return !mesh.triangles.isEmpty();
}

// 函数说明：loadToolModel负责当前模块的核心处理。
static bool loadToolModel(const QString &path, MeshData &mesh)
{
    mesh = MeshData{};
    QString resolvedPath = path;
    if (QFileInfo(resolvedPath).isRelative()) {
        const QString appPath = QCoreApplication::applicationDirPath() + QStringLiteral("/") + path;
        const QString projectPath = QCoreApplication::applicationDirPath() + QStringLiteral("/../") + path;
        if (QFileInfo(appPath).exists()) {
            resolvedPath = appPath;
        } else if (QFileInfo(projectPath).exists()) {
            resolvedPath = projectPath;
        }
    }

    const QString suffix = QFileInfo(resolvedPath).suffix().toLower();
    if (suffix == QStringLiteral("obj")) {
        return loadObjToolModel(resolvedPath, mesh);
    }
    if (suffix == QStringLiteral("stl")) {
        if (loadBinaryStlToolModel(resolvedPath, mesh)) {
            return true;
        }
        mesh = MeshData{};
        return loadAsciiStlToolModel(resolvedPath, mesh);
    }
    return false;
}

} // namespace

// 函数说明：setMesh负责当前模块的核心处理。
void ViewportWidget::setMesh(const MeshData &mesh)
{
    m_mesh = mesh;
    m_highlightedFaces.clear();
    clearTopologyPickFilter();
    fitAll();
}

void ViewportWidget::setModelVisible(bool visible)
{
    if (m_modelVisible == visible) return;
    m_modelVisible = visible;
    m_hasHover = false;
    m_topologyHoverPoints.clear();
    m_coordinateLabel->setText(QStringLiteral("X: —   Y: —   mm"));
    emit topologyHovered(QString(), static_cast<int>(m_topologyPickMode), QVector3D(), 0.0);
    emit modelVisibilityChanged(visible);
    update();
}

// 函数说明：setHighlightedFaces负责当前模块的核心处理。
void ViewportWidget::setHighlightedFaces(const QVector<int> &faceIndices)
{
    m_highlightedFaces.clear();
    for (int faceIndex : faceIndices) {
        if (faceIndex > 0) {
            m_highlightedFaces.insert(faceIndex);
        }
    }
    update();
}

// 函数说明：clearHighlight负责当前模块的核心处理。
void ViewportWidget::clearHighlight()
{
    if (m_highlightedFaces.isEmpty()) {
        return;
    }
    m_highlightedFaces.clear();
    update();
}

// 函数说明：resetCamera负责当前模块的核心处理。
void ViewportWidget::resetCamera()
{
    setStandardView(StandardView::Isometric);
}

void ViewportWidget::setStandardView(StandardView view)
{
    const bool zh = Settings::instance().language() == QStringLiteral("zh_CN");
    switch (view) {
    case StandardView::Front: m_rotX = -90; m_rotZ = 0; m_viewLabel = zh ? QStringLiteral("前视") : QStringLiteral("Front"); break;
    case StandardView::Back: m_rotX = -90; m_rotZ = 180; m_viewLabel = zh ? QStringLiteral("后视") : QStringLiteral("Back"); break;
    case StandardView::Left: m_rotX = -90; m_rotZ = -90; m_viewLabel = zh ? QStringLiteral("左视") : QStringLiteral("Left"); break;
    case StandardView::Right: m_rotX = -90; m_rotZ = 90; m_viewLabel = zh ? QStringLiteral("右视") : QStringLiteral("Right"); break;
    case StandardView::Top: m_rotX = 0; m_rotZ = 0; m_viewLabel = zh ? QStringLiteral("顶视") : QStringLiteral("Top"); break;
    case StandardView::Bottom: m_rotX = 180; m_rotZ = 0; m_viewLabel = zh ? QStringLiteral("底视") : QStringLiteral("Bottom"); break;
    case StandardView::Isometric: m_rotX = -54.7356f; m_rotZ = -45; m_viewLabel = zh ? QStringLiteral("等轴测") : QStringLiteral("Isometric"); break;
    }
    fitAll();
}

void ViewportWidget::fitAll()
{
    m_autoFit = true;
    QVector3D bbMin = m_mesh.bbMin, bbMax = m_mesh.bbMax;
    bool hasBounds = !m_mesh.isEmpty() || !m_mesh.pickEdges.isEmpty();
    for (const auto *points : {&m_toolPath, &m_toolPathStarts}) {
        for (const auto &point : *points) {
            if (!hasBounds) { bbMin = bbMax = point; hasBounds = true; }
            for (int axis = 0; axis < 3; ++axis) {
                bbMin[axis] = qMin(bbMin[axis], point[axis]);
                bbMax[axis] = qMax(bbMax[axis], point[axis]);
            }
        }
    }
    if (hasBounds) {
        const QVector3D center = (bbMin + bbMax) * 0.5f;
        m_pan = -center;
        QMatrix4x4 rotation;
        rotation.rotate(m_rotX, 1, 0, 0);
        rotation.rotate(m_rotZ, 0, 0, 1);
        const float tanY = std::tan(22.5f * 3.14159265f / 180.0f);
        const float tanX = tanY * float(qMax(1, width())) / float(qMax(1, height()));
        float distance = 1.0f;
        for (int mask = 0; mask < 8; ++mask) {
            const QVector3D corner(mask & 1 ? bbMax.x() : bbMin.x(),
                                   mask & 2 ? bbMax.y() : bbMin.y(),
                                   mask & 4 ? bbMax.z() : bbMin.z());
            const QVector3D point = rotation.map(corner - center);
            distance = qMax(distance, point.z() + 1.18f * qMax(std::abs(point.x()) / tanX, std::abs(point.y()) / tanY));
        }
        m_zoom = 200.0f / distance;
    } else {
        m_pan = {};
        m_zoom = 1.0f;
    }
    updateProjection();
    update();
}

void ViewportWidget::updateProjection()
{
    const float distance = 200.0f / m_zoom;
    float radius = qMax(1.0f, (m_mesh.bbMax - m_mesh.bbMin).length());
    for (const auto *points : {&m_toolPath, &m_toolPathStarts})
        for (const auto &point : *points) radius = qMax(radius, (point + m_pan).length());
    m_proj.setToIdentity();
    m_proj.perspective(45.0f, float(qMax(1, width())) / float(qMax(1, height())),
                       qMax(0.01f, distance * 0.0001f), qMax(5000.0f, distance + radius * 4));
}

QMatrix4x4 ViewportWidget::currentViewMatrix() const
{
    QMatrix4x4 view;
    view.translate(0, 0, -200.0f / m_zoom);
    view.rotate(m_rotX, 1, 0, 0);
    view.rotate(m_rotZ, 0, 0, 1);
    view.translate(m_pan);
    return view;
}

// 函数说明：initializeGL负责当前模块的核心处理。
void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.84f, 0.86f, 0.89f, 1.0f);
}

// 函数说明：resizeGL负责当前模块的核心处理。
void ViewportWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    if (m_autoFit) fitAll();
    else updateProjection();
}

// 函数说明：paintGL负责当前模块的核心处理。
void ViewportWidget::paintGL()
{
    // QPainter changes these states. Keep the opaque canvas alpha intact when
    // blending translucent geometry so Qt can composite its framebuffer.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    context()->functions()->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                               GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_view = currentViewMatrix();

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m_proj.constData());
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m_view.constData());

    if (Settings::instance().showGrid()) drawGrid();
    drawAxes();
    if (m_modelVisible) {
        drawMesh();
        drawContourChoicePreview();
    }
    drawToolPath();
    drawToolMarker();
    drawOrientation();
    QVector3D point;
    const bool hit = m_hasHover && pickFaceAt(m_hoverPosition, &point) > 0;
    m_coordinateLabel->setText(hit ? QStringLiteral("X: %1   Y: %2   mm")
        .arg(double(point.x()), 0, 'f', 3).arg(double(point.y()), 0, 'f', 3)
        : QStringLiteral("X: —   Y: —   mm"));
    m_coordinateLabel->adjustSize();
    m_coordinateLabel->move(12, qMax(0, height() - m_coordinateLabel->height() - 12));
}

void ViewportWidget::drawOrientation()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QMatrix4x4 rotation;
    rotation.rotate(m_rotX, 1, 0, 0);
    rotation.rotate(m_rotZ, 0, 0, 1);
    const QPointF origin(width() - 65, 66);
    struct CubeFace { QVector3D normal; QVector<QVector3D> points; QString label; };
    const bool zh = Settings::instance().language() == QStringLiteral("zh_CN");
    const QVector<CubeFace> faces = {
        {{0,-1,0}, {{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}}, zh ? QStringLiteral("前") : QStringLiteral("F")},
        {{0,1,0}, {{1,1,-1},{-1,1,-1},{-1,1,1},{1,1,1}}, zh ? QStringLiteral("后") : QStringLiteral("B")},
        {{-1,0,0}, {{-1,1,-1},{-1,-1,-1},{-1,-1,1},{-1,1,1}}, zh ? QStringLiteral("左") : QStringLiteral("L")},
        {{1,0,0}, {{1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1}}, zh ? QStringLiteral("右") : QStringLiteral("R")},
        {{0,0,1}, {{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}, zh ? QStringLiteral("顶") : QStringLiteral("T")},
        {{0,0,-1}, {{-1,1,-1},{1,1,-1},{1,-1,-1},{-1,-1,-1}}, zh ? QStringLiteral("底") : QStringLiteral("D")}
    };
    for (const auto &face : faces) {
        const QVector3D normal = rotation.mapVector(face.normal);
        if (normal.z() <= 0.001f) continue;
        QPolygonF polygon;
        for (const auto &point : face.points) {
            const auto p = rotation.map(point);
            polygon << origin + QPointF(p.x() * 23, -p.y() * 23);
        }
        painter.setPen(QColor(QStringLiteral("#8694a5")));
        painter.setBrush(QColor::fromRgbF(0.79 + normal.z()*0.14, 0.82 + normal.z()*0.12, 0.87 + normal.z()*0.09));
        painter.drawPolygon(polygon);
        painter.setPen(QColor(QStringLiteral("#27364d")));
        painter.drawText(polygon.boundingRect(), Qt::AlignCenter, face.label);
    }
    const QPointF axisOrigin(42, height() - 45);
    const QVector<QColor> colors = {QColor("#bd3036"), QColor("#267743"), QColor("#1769c9")};
    const QStringList labels = {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    for (int axis = 0; axis < 3; ++axis) {
        QVector3D unit; unit[axis] = 1;
        const auto p = rotation.mapVector(unit);
        const QPointF end = axisOrigin + QPointF(p.x()*28, -p.y()*28);
        painter.setPen(QPen(colors.at(axis), 2));
        painter.drawLine(axisOrigin, end);
        painter.drawText(end + QPointF(3, -3), labels.at(axis));
    }
    painter.setPen(QColor(QStringLiteral("#27364d")));
    painter.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignBottom | Qt::AlignRight, m_viewLabel);
}

// 函数说明：drawGrid负责当前模块的核心处理。
void ViewportWidget::drawGrid()
{
    const int steps = 20;
    const float span = 100.0f;
    const float step = span * 2.0f / steps;

    glLineWidth(1.0f);
    glBegin(GL_LINES);
    glColor4f(0.56f, 0.60f, 0.65f, 0.35f);
    for (int i = 0; i <= steps; ++i) {
        float v = -span + i * step;
        glVertex3f(v, -span, 0); glVertex3f(v,  span, 0);
        glVertex3f(-span, v, 0); glVertex3f( span, v, 0);
    }
    glEnd();
}

// 函数说明：drawAxes负责当前模块的核心处理。
void ViewportWidget::drawAxes()
{
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glColor3f(1,0,0); glVertex3f(0,0,0); glVertex3f(20,0,0);  // X red
    glColor3f(0,1,0); glVertex3f(0,0,0); glVertex3f(0,20,0);  // Y green
    glColor3f(0,0,1); glVertex3f(0,0,0); glVertex3f(0,0,20);  // Z blue
    glEnd();
    glLineWidth(1.0f);
}

// 函数说明：drawMesh负责当前模块的核心处理。
void ViewportWidget::drawMesh()
{
    if (m_mesh.isEmpty()) {
        glColor3f(0.12f, 0.30f, 0.52f);
        glLineWidth(2.0f);
        for (const auto &edge : m_mesh.pickEdges) {
            glBegin(GL_LINE_STRIP);
            for (const auto &point : edge.points) glVertex3f(point.x(), point.y(), point.z());
            glEnd();
        }
        glLineWidth(1.0f);
        return;
    }
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    for (const Triangle &tri : m_mesh.triangles) {
        const QVector3D &n = tri.normal;
        if (m_highlightedFaces.contains(tri.faceIndex)) {
            glColor3f(0.20f, 0.53f, 0.95f);
        } else {
            float shade = std::abs(n.z()) * 0.5f + std::abs(n.x()) * 0.3f + std::abs(n.y()) * 0.2f;
            shade = 0.3f + shade * 0.7f;
            glColor3f(shade * 0.88f, shade * 0.90f, shade * 0.93f);
        }
        glNormal3f(n.x(), n.y(), n.z());
        auto v = [this](const QVector3D &p){ glVertex3f(p.x(), p.y(), p.z()); };
        v(tri.v0); v(tri.v1); v(tri.v2);
    }
    glEnd();
    glDisable(GL_POLYGON_OFFSET_FILL);
}

// 函数说明：mousePressEvent负责当前模块的核心处理。
void ViewportWidget::mousePressEvent(QMouseEvent *e)
{
    m_lastMouse  = e->pos();
    m_pressMouse = e->pos();
    m_dragged = false;
    m_mouseButton = e->button();
}

// 函数说明：mouseMoveEvent负责当前模块的核心处理。
void ViewportWidget::mouseMoveEvent(QMouseEvent *e)
{
    m_hoverPosition = e->pos();
    m_hasHover = true;
    QPoint delta = e->pos() - m_lastMouse;
    m_lastMouse  = e->pos();

    if (m_mouseButton != 0 && (e->pos() - m_pressMouse).manhattanLength() > 3)
        m_dragged = true;

    if (m_mouseButton != 0) m_autoFit = false;
    if (m_mouseButton == Qt::LeftButton) {
        m_viewLabel.clear();
        m_rotZ += delta.x() * 0.5f;
        m_rotX += delta.y() * 0.5f;
    } else if (m_mouseButton == Qt::MiddleButton || m_mouseButton == Qt::RightButton) {
        QMatrix4x4 rotation;
        rotation.rotate(m_rotX, 1, 0, 0);
        rotation.rotate(m_rotZ, 0, 0, 1);
        const QVector3D center = (m_mesh.bbMin + m_mesh.bbMax) * 0.5f;
        const float depth = 200.0f / m_zoom - rotation.map(center + m_pan).z();
        const float scale = 2.0f * depth * std::tan(22.5f * 3.14159265f / 180.0f)
            / float(qMax(1, height()));
        // Pan is stored before camera rotation; convert screen motion back to model axes.
        m_pan += rotation.transposed().mapVector(QVector3D(delta.x() * scale, -delta.y() * scale, 0));
    }
    if (m_mouseButton == 0 && m_topologyPickMode == TopologyPickMode::Edge) {
        QString id;
        QVector3D hitPoint;
        double parameter = 0.0;
        if (pickEdgeAt(e->pos(), &id, &hitPoint, &parameter))
            emit topologyHovered(id, static_cast<int>(TopologyPickMode::Edge),
                                 hitPoint, parameter);
        else
            emit topologyHovered(QString(), static_cast<int>(TopologyPickMode::Edge),
                                 QVector3D(), 0.0);
    }
    update();
}

// 函数说明：mouseReleaseEvent负责当前模块的核心处理。
void ViewportWidget::mouseReleaseEvent(QMouseEvent *e)
{
    const bool shortLeftClick = m_mouseButton == Qt::LeftButton && !m_dragged &&
                                (e->pos() - m_pressMouse).manhattanLength() <= 3;
    if (shortLeftClick) {
        QVector3D hitPoint;
        if (m_topologyPickMode == TopologyPickMode::Edge) {
            QString id;
            double parameter = 0.0;
            if (pickEdgeAt(e->pos(), &id, &hitPoint, &parameter)) {
                emit topologyPicked(id, static_cast<int>(TopologyPickMode::Edge), hitPoint, parameter);
            }
        } else if (m_topologyPickMode == TopologyPickMode::Vertex) {
            QString id;
            if (pickVertexAt(e->pos(), &id, &hitPoint)) {
                emit topologyPicked(id, static_cast<int>(TopologyPickMode::Vertex), hitPoint, 0.0);
            }
        } else {
            const int faceIndex = pickFaceAt(e->pos(), &hitPoint);
            if (faceIndex > 0 && (!m_topologyPickFilterActive ||
                                  m_selectableTopologyFaces.contains(faceIndex))) {
                emit facePicked(faceIndex);
                emit facePointPicked(faceIndex, hitPoint);
                emit topologyPicked(QStringLiteral("face:%1").arg(faceIndex),
                                    static_cast<int>(TopologyPickMode::Face), hitPoint, 0.0);
            }
        }
    }

    m_mouseButton = 0;
}

// 函数说明：wheelEvent负责当前模块的核心处理。
void ViewportWidget::leaveEvent(QEvent *e)
{
    m_hasHover = false;
    emit topologyHovered(QString(), static_cast<int>(m_topologyPickMode), QVector3D(), 0.0);
    update();
    QOpenGLWidget::leaveEvent(e);
}

void ViewportWidget::wheelEvent(QWheelEvent *e)
{
    float factor = e->angleDelta().y() > 0 ? 1.1f : 1.0f / 1.1f;
    m_autoFit = false;
    m_zoom = qBound(0.00001f, m_zoom * factor, 100000.0f);
    updateProjection();
    update();
}

// 函数说明：pickFaceAt负责当前模块的核心处理。
int ViewportWidget::pickFaceAt(const QPointF &pos, QVector3D *hitPoint,
                               float *ndcDepth) const
{
    if (!m_modelVisible || m_mesh.isEmpty() || width() <= 0 || height() <= 0) {
        return 0;
    }

    const QMatrix4x4 mvp = m_proj * currentViewMatrix();
    const QVector2D p(float(pos.x()), float(pos.y()));
    int bestFace = 0;
    float bestDepth = std::numeric_limits<float>::max();
    QVector3D bestPoint;

    auto project = [&](const QVector3D &v, QVector2D &out, float &depth) -> bool {
        const QVector4D clip = mvp * QVector4D(v, 1.0f);
        if (std::abs(clip.w()) <= 1.0e-6f) {
            return false;
        }
        const QVector3D ndc = clip.toVector3DAffine();
        if (ndc.z() < -1.0f || ndc.z() > 1.0f) {
            return false;
        }
        out = QVector2D((ndc.x() * 0.5f + 0.5f) * width(),
                        (1.0f - (ndc.y() * 0.5f + 0.5f)) * height());
        depth = ndc.z();
        return true;
    };

    auto edge = [](const QVector2D &a, const QVector2D &b, const QVector2D &c) {
        const QVector2D ab = b - a;
        const QVector2D ac = c - a;
        return ab.x() * ac.y() - ab.y() * ac.x();
    };

    for (const Triangle &tri : m_mesh.triangles) {
        QVector2D a, b, c;
        float za = 0.0f, zb = 0.0f, zc = 0.0f;
        if (!project(tri.v0, a, za) || !project(tri.v1, b, zb) || !project(tri.v2, c, zc)) {
            continue;
        }

        const float area = edge(a, b, c);
        if (std::abs(area) <= 1.0e-5f) {
            continue;
        }

        const float w0 = edge(b, c, p) / area;
        const float w1 = edge(c, a, p) / area;
        const float w2 = edge(a, b, p) / area;
        constexpr float eps = -1.0e-4f;
        if (w0 < eps || w1 < eps || w2 < eps) {
            continue;
        }

        const float depth = w0 * za + w1 * zb + w2 * zc;
        if (depth < bestDepth) {
            bestDepth = depth;
            bestFace = tri.faceIndex;
            // Screen barycentric weights need perspective correction for model coordinates.
            const float aWeight = w0 / (mvp * QVector4D(tri.v0, 1.0f)).w();
            const float bWeight = w1 / (mvp * QVector4D(tri.v1, 1.0f)).w();
            const float cWeight = w2 / (mvp * QVector4D(tri.v2, 1.0f)).w();
            bestPoint = (tri.v0 * aWeight + tri.v1 * bWeight + tri.v2 * cWeight)
                / (aWeight + bWeight + cWeight);
        }
    }

    if (hitPoint && bestFace > 0) {
        *hitPoint = bestPoint;
    }
    if (ndcDepth && bestFace > 0) {
        *ndcDepth = bestDepth;
    }
    return bestFace;
}

bool ViewportWidget::topologyPointVisible(const QVector3D &point, const QPointF &screen) const
{
    QVector3D surface;
    if (pickFaceAt(screen, &surface) <= 0) return true;
    const QMatrix4x4 view = currentViewMatrix();
    // Compare distances on the candidate's ray, not compressed projection depth
    // or the cursor's different ray. Allow only model-scale rounding error.
    const float tolerance = qMax(1.0e-5f, (m_mesh.bbMax - m_mesh.bbMin).length() * 1.0e-6f);
    return view.map(point).z() >= view.map(surface).z() - tolerance;
}

bool ViewportWidget::pickEdgeAt(const QPoint &pos, QString *id, QVector3D *hitPoint,
                                double *curveParameter) const
{
    if (!m_modelVisible || width() <= 0 || height() <= 0) {
        return false;
    }
    const QMatrix4x4 mvp = m_proj * currentViewMatrix();
    const QVector2D mouse(float(pos.x()), float(pos.y()));
    float bestDistanceSquared = 7.0f * 7.0f;
    float bestDepth = std::numeric_limits<float>::max();
    QString bestId;
    QVector3D bestPoint;
    double bestParameter = 0.0;

    auto project = [&](const QVector3D &point, QVector2D *screen,
                       float *depth, float *clipW) -> bool {
        const QVector4D clip = mvp * QVector4D(point, 1.0f);
        if (clip.w() <= 1.0e-6f) {
            return false;
        }
        const QVector3D ndc = clip.toVector3DAffine();
        if (ndc.z() < -1.0f || ndc.z() > 1.0f) {
            return false;
        }
        *screen = QVector2D((ndc.x() * 0.5f + 0.5f) * width(),
                            (1.0f - (ndc.y() * 0.5f + 0.5f)) * height());
        *depth = ndc.z();
        *clipW = clip.w();
        return true;
    };

    for (const MeshPickEdge &edge : m_mesh.pickEdges) {
        if (m_topologyPickFilterActive && !m_selectableTopologyEdges.contains(edge.id)) {
            continue;
        }
        if (edge.degenerated || edge.points.size() < 2) {
            continue;
        }
        for (int i = 0; i + 1 < edge.points.size(); ++i) {
            QVector2D a;
            QVector2D b;
            float za = 0.0f;
            float zb = 0.0f;
            float wa = 1.0f;
            float wb = 1.0f;
            if (!project(edge.points.at(i), &a, &za, &wa) ||
                !project(edge.points.at(i + 1), &b, &zb, &wb)) {
                continue;
            }
            const QVector2D segment = b - a;
            const float lengthSquared = segment.lengthSquared();
            if (lengthSquared <= 1.0e-6f) {
                continue;
            }
            const float screenT = qBound(0.0f,
                QVector2D::dotProduct(mouse - a, segment) / lengthSquared, 1.0f);
            const float distanceSquared = (mouse - (a + segment * screenT)).lengthSquared();
            const float depth = za + (zb - za) * screenT;
            if (distanceSquared > bestDistanceSquared + 1.0e-4f ||
                (std::abs(distanceSquared - bestDistanceSquared) <= 1.0e-4f && depth >= bestDepth)) {
                continue;
            }
            const float leftWeight = (1.0f - screenT) / wa;
            const float rightWeight = screenT / wb;
            const float weightSum = leftWeight + rightWeight;
            const float modelT = weightSum > 1.0e-8f ? rightWeight / weightSum : screenT;
            const QVector3D point = edge.points.at(i) * (1.0f - modelT) + edge.points.at(i + 1) * modelT;
            if (!topologyPointVisible(point, (a + segment * screenT).toPointF())) continue;
            bestDistanceSquared = distanceSquared;
            bestDepth = depth;
            bestId = edge.id;
            bestPoint = point;
            if (edge.parameters.size() == edge.points.size()) {
                bestParameter = edge.parameters.at(i) * (1.0 - modelT)
                    + edge.parameters.at(i + 1) * modelT;
            } else {
                bestParameter = modelT;
            }
        }
    }

    if (bestId.isEmpty()) {
        return false;
    }
    if (id) *id = bestId;
    if (hitPoint) *hitPoint = bestPoint;
    if (curveParameter) *curveParameter = bestParameter;
    return true;
}

bool ViewportWidget::pickVertexAt(const QPoint &pos, QString *id, QVector3D *hitPoint) const
{
    if (!m_modelVisible || width() <= 0 || height() <= 0) {
        return false;
    }
    const QMatrix4x4 mvp = m_proj * currentViewMatrix();
    const QVector2D mouse(float(pos.x()), float(pos.y()));
    float bestDistanceSquared = 8.0f * 8.0f;
    float bestDepth = std::numeric_limits<float>::max();
    const MeshPickVertex *best = nullptr;

    for (const MeshPickVertex &vertex : m_mesh.pickVertices) {
        if (m_topologyPickFilterActive &&
            !m_selectableTopologyVertices.contains(vertex.id)) {
            continue;
        }
        const QVector4D clip = mvp * QVector4D(vertex.point, 1.0f);
        if (clip.w() <= 1.0e-6f) {
            continue;
        }
        const QVector3D ndc = clip.toVector3DAffine();
        if (ndc.z() < -1.0f || ndc.z() > 1.0f) {
            continue;
        }
        const QVector2D screen((ndc.x() * 0.5f + 0.5f) * width(),
                               (1.0f - (ndc.y() * 0.5f + 0.5f)) * height());
        const float distanceSquared = (mouse - screen).lengthSquared();
        if (distanceSquared < bestDistanceSquared ||
            (std::abs(distanceSquared - bestDistanceSquared) <= 1.0e-4f && ndc.z() < bestDepth)) {
            if (!topologyPointVisible(vertex.point, screen.toPointF())) continue;
            bestDistanceSquared = distanceSquared;
            bestDepth = ndc.z();
            best = &vertex;
        }
    }

    if (!best) {
        return false;
    }
    if (id) *id = best->id;
    if (hitPoint) *hitPoint = best->point;
    return true;
}

// 函数说明：setToolPath负责当前模块的核心处理。
void ViewportWidget::setToolPath(const QVector<QVector3D> &path, const QVector<bool> &rapidSegments,
                                 const QVector<QVector3D> &segmentStarts)
{
    m_toolPath = path;
    m_toolPathStarts = segmentStarts;
    m_toolPathVisible.clear();
    m_toolPathRapidSegments = rapidSegments;
    if (path.isEmpty()) m_hasToolPos = false;
    if (m_autoFit) fitAll();
    else { updateProjection(); update(); }
}

// 函数说明：setToolPosition负责当前模块的核心处理。
void ViewportWidget::setToolPosition(const QVector3D &pos)
{
    m_toolPos    = pos;
    m_hasToolPos = true;
    update();
}

// 函数说明：setToolDiameter负责当前模块的核心处理。
void ViewportWidget::setToolDiameter(double diameter)
{
    m_toolDiameter = diameter > 0.0 ? float(qBound(0.5, diameter, 200.0)) : 6.0f;
    update();
}

// 函数说明：setToolModelPath负责当前模块的核心处理。
void ViewportWidget::setToolModelPath(const QString &path)
{
    const QString normalized = path.trimmed();
    if (normalized == m_toolModelPath) {
        return;
    }

    m_toolModelPath = normalized;
    m_toolModel = MeshData{};
    if (!m_toolModelPath.isEmpty()) {
        loadToolModel(m_toolModelPath, m_toolModel);
    }
    update();
}

void ViewportWidget::setToolGeometryVisible(bool visible)
{
    m_toolGeometryVisible = visible;
    update();
}

// 函数说明：clearToolPath负责当前模块的核心处理。
void ViewportWidget::clearToolPath()
{
    setToolPath({}, {});
}

// 函数说明：setContourChoicePreview负责当前模块的核心处理。
void ViewportWidget::setContourChoicePreview(const QVector<QVector3D> &points,
                                              bool closed,
                                              int cutterSide,
                                              bool candidate)
{
    m_contourChoicePoints = points;
    m_contourChoiceClosed = closed;
    m_contourChoiceSide = cutterSide;
    m_contourChoiceCandidate = candidate;
    update();
}

// 函数说明：clearContourChoicePreview负责当前模块的核心处理。
void ViewportWidget::clearContourChoicePreview()
{
    if (m_contourChoicePoints.isEmpty()) {
        m_contourChoiceCandidate = false;
        return;
    }
    m_contourChoicePoints.clear();
    m_contourChoiceCandidate = false;
    update();
}

void ViewportWidget::setConfirmedContourPreview(const QVector<QVector3D> &points,
                                                bool closed)
{
    m_confirmedContourPoints = points;
    m_confirmedContourClosed = closed;
    update();
}

void ViewportWidget::clearConfirmedContourPreview()
{
    m_confirmedContourPoints.clear();
    m_confirmedContourClosed = false;
    update();
}

void ViewportWidget::setTopologyHoverPreview(const QVector<QVector3D> &points)
{
    m_topologyHoverPoints = points;
    update();
}

void ViewportWidget::setTopologyPickFilter(const QStringList &edgeIds,
                                           const QStringList &vertexIds,
                                           const QVector<int> &faceIndices)
{
    m_topologyPickFilterActive = true;
    m_selectableTopologyEdges = QSet<QString>(edgeIds.cbegin(), edgeIds.cend());
    m_selectableTopologyVertices = QSet<QString>(vertexIds.cbegin(), vertexIds.cend());
    m_selectableTopologyFaces.clear();
    for (int faceIndex : faceIndices)
        if (faceIndex > 0) m_selectableTopologyFaces.insert(faceIndex);
    update();
}

void ViewportWidget::clearTopologyPickFilter()
{
    setTopologyHoverPreview({});
    m_topologyPickFilterActive = false;
    m_selectableTopologyEdges.clear();
    m_selectableTopologyVertices.clear();
    m_selectableTopologyFaces.clear();
    update();
}

void ViewportWidget::setTopologyPickMode(int mode)
{
    if (mode < static_cast<int>(TopologyPickMode::Face) ||
        mode > static_cast<int>(TopologyPickMode::Vertex)) {
        return;
    }
    m_topologyPickMode = static_cast<TopologyPickMode>(mode);
    setTopologyHoverPreview({});
}

// 函数说明：drawContourChoicePreview负责当前模块的核心处理。
void ViewportWidget::drawContourChoicePreview()
{
    if (m_confirmedContourPoints.size() >= 2) {
        glEnable(GL_DEPTH_TEST);
        glLineWidth(4.0f);
        glColor4f(0.1f, 0.85f, 0.25f, 0.95f);
        glBegin(GL_LINE_STRIP);
        for (const QVector3D &point : m_confirmedContourPoints)
            glVertex3f(point.x(), point.y(), point.z());
        if (m_confirmedContourClosed) {
            const QVector3D &first = m_confirmedContourPoints.first();
            glVertex3f(first.x(), first.y(), first.z());
        }
        glEnd();
    }

    if (m_contourChoicePoints.size() >= 2) {
        glEnable(GL_DEPTH_TEST);
        glLineWidth(3.0f);
        glColor4f(0.1f, 0.65f, 1.0f, 0.95f);
        if (m_contourChoiceCandidate) {
            glEnable(GL_LINE_STIPPLE);
            glLineStipple(1, 0x00ff);
        }
        glBegin(GL_LINE_STRIP);
        for (const QVector3D &point : m_contourChoicePoints)
            glVertex3f(point.x(), point.y(), point.z());
        if (m_contourChoiceClosed) {
            const QVector3D &first = m_contourChoicePoints.first();
            glVertex3f(first.x(), first.y(), first.z());
        }
        glEnd();
        if (m_contourChoiceCandidate) glDisable(GL_LINE_STIPPLE);
    }

    if (m_topologyHoverPoints.size() >= 2) {
        glLineWidth(2.5f);
        glColor4f(1.0f, 0.85f, 0.1f, 1.0f);
        glBegin(GL_LINE_STRIP);
        for (const QVector3D &point : m_topologyHoverPoints)
            glVertex3f(point.x(), point.y(), point.z());
        glEnd();
        glLineWidth(1.0f);
    }

    const QVector<QVector3D> &directionPoints = m_contourChoicePoints.size() >= 2
        ? m_contourChoicePoints : m_confirmedContourPoints;
    if (directionPoints.size() < 2) return;

    const QVector3D start = directionPoints.at(0);
    const QVector3D next = directionPoints.at(1);
    QVector3D direction = next - start;
    const float segmentLength = direction.length();
    if (segmentLength > 1.0e-5f) {
        direction /= segmentLength;
        QVector3D reference = std::abs(direction.z()) < 0.85f
            ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
        QVector3D left = QVector3D::crossProduct(reference, direction).normalized();
        const float arrowLength = qBound(1.5f, segmentLength * 0.25f, 8.0f);
        const QVector3D tip = start + direction * qMin(segmentLength * 0.65f, arrowLength * 2.0f);
        const QVector3D wingBase = tip - direction * arrowLength * 0.55f;

        glColor4f(1.0f, 0.85f, 0.1f, 1.0f);
        glLineWidth(3.5f);
        glBegin(GL_LINES);
        glVertex3f(start.x(), start.y(), start.z());
        glVertex3f(tip.x(), tip.y(), tip.z());
        const QVector3D wingA = wingBase + left * arrowLength * 0.35f;
        const QVector3D wingB = wingBase - left * arrowLength * 0.35f;
        glVertex3f(tip.x(), tip.y(), tip.z());
        glVertex3f(wingA.x(), wingA.y(), wingA.z());
        glVertex3f(tip.x(), tip.y(), tip.z());
        glVertex3f(wingB.x(), wingB.y(), wingB.z());
        glEnd();

        const float sideSign = m_contourChoiceSide < 0 ? -1.0f : 1.0f;
        const QVector3D midpoint = (start + next) * 0.5f;
        const QVector3D sideTip = midpoint + left * (arrowLength * sideSign);
        glColor4f(1.0f, 0.45f, 0.1f, 1.0f);
        glLineWidth(2.5f);
        glBegin(GL_LINES);
        glVertex3f(midpoint.x(), midpoint.y(), midpoint.z());
        glVertex3f(sideTip.x(), sideTip.y(), sideTip.z());
        glEnd();
    }

    glPointSize(10.0f);
    glColor4f(1.0f, 0.9f, 0.15f, 1.0f);
    glBegin(GL_POINTS);
    glVertex3f(start.x(), start.y(), start.z());
    glEnd();
    glPointSize(1.0f);
    glLineWidth(1.0f);
}

// 函数说明：drawToolPath负责当前模块的核心处理。
void ViewportWidget::drawToolPath()
{
    if (m_toolPath.size() < 2) return;

    glLineWidth(1.5f);
    glBegin(GL_LINES);
    for (int i = 1; i < m_toolPath.size(); ++i) {
        if (i - 1 < m_toolPathVisible.size() && !m_toolPathVisible.at(i - 1)) continue;
        const bool rapid = (i - 1) < m_toolPathRapidSegments.size() &&
                           m_toolPathRapidSegments[i - 1];
        if (rapid) {
            glColor4f(1.0f, 0.15f, 0.1f, 0.85f);
        } else {
            glColor4f(0.1f, 1.0f, 0.25f, 0.90f);
        }
        const QVector3D &a = m_toolPathStarts.size() == m_toolPath.size() - 1
            ? m_toolPathStarts[i - 1] : m_toolPath[i - 1];
        const QVector3D &b = m_toolPath[i];
        glVertex3f(a.x(), a.y(), a.z());
        glVertex3f(b.x(), b.y(), b.z());
    }
    glEnd();
    glLineWidth(1.0f);
}

// 函数说明：drawToolMarker负责当前模块的核心处理。
void ViewportWidget::drawToolMarker()
{
    if (!m_hasToolPos) return;

    const float x = m_toolPos.x();
    const float y = m_toolPos.y();
    const float z = m_toolPos.z();
    if (!m_toolGeometryVisible) {
        glPointSize(7.0f);
        glColor3f(1.0f, 0.65f, 0.0f);
        glBegin(GL_POINTS);
        glVertex3f(x, y, z);
        glEnd();
        glPointSize(1.0f);
        return;
    }
    if (!m_toolModel.isEmpty()) {
        glPushMatrix();
        glTranslatef(x, y, z);
        glBegin(GL_TRIANGLES);
        for (const Triangle &tri : m_toolModel.triangles) {
            const QVector3D &n = tri.normal;
            const float shade = 0.55f + 0.35f * std::abs(n.z());
            glColor4f(shade, shade * 0.88f, shade * 0.45f, 0.82f);
            glNormal3f(n.x(), n.y(), n.z());
            glVertex3f(tri.v0.x(), tri.v0.y(), tri.v0.z());
            glVertex3f(tri.v1.x(), tri.v1.y(), tri.v1.z());
            glVertex3f(tri.v2.x(), tri.v2.y(), tri.v2.z());
        }
        glEnd();
        glPopMatrix();
        return;
    }

    const float radius = qMax(0.25f, m_toolDiameter * 0.5f);
    const float fluteLen = qMax(12.0f, m_toolDiameter * 3.0f);
    const float holderRadius = radius * 1.35f;
    const float holderLen = qMax(10.0f, m_toolDiameter * 1.5f);
    constexpr int segments = 32;
    constexpr double markerPi = 3.14159265358979323846;

    auto disk = [&](float zPos, float r, bool top) {
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(x, y, zPos);
        for (int i = 0; i <= segments; ++i) {
            const int idx = top ? i : segments - i;
            const float b = float(2.0 * markerPi * double(idx) / double(segments));
            glVertex3f(x + std::cos(b) * r, y + std::sin(b) * r, zPos);
        }
        glEnd();
    };

    auto cylinder = [&](float z0, float z1, float r) {
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= segments; ++i) {
            const float a = float(2.0 * markerPi * double(i) / double(segments));
            const float px = x + std::cos(a) * r;
            const float py = y + std::sin(a) * r;
            glVertex3f(px, py, z0);
            glVertex3f(px, py, z1);
        }
        glEnd();
    };

    glColor4f(1.0f, 0.78f, 0.08f, 0.72f);
    cylinder(z, z + fluteLen, radius);
    disk(z, radius, false);
    disk(z + fluteLen, radius, true);

    glColor4f(0.75f, 0.75f, 0.78f, 0.55f);
    cylinder(z + fluteLen, z + fluteLen + holderLen, holderRadius);
    disk(z + fluteLen + holderLen, holderRadius, true);

    glLineWidth(2.5f);
    glBegin(GL_LINES);
    glColor3f(1.0f, 1.0f, 0.0f);
    glVertex3f(x - radius, y,          z); glVertex3f(x + radius, y,          z);
    glVertex3f(x,          y - radius, z); glVertex3f(x,          y + radius, z);
    glVertex3f(x,          y,          z); glVertex3f(x,          y,          z + fluteLen + holderLen);
    glEnd();
    glLineWidth(1.0f);
}

void ViewportWidget::setToolPathVisibility(const QVector<bool> &visible)
{
    m_toolPathVisible = visible;
    update();
}
