#include "ViewportWidget.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTextStream>
#include <QVector4D>
#include <QWheelEvent>
#include <QtGlobal>
#include <cmath>
#include <limits>

ViewportWidget::ViewportWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

namespace {

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

void ViewportWidget::setMesh(const MeshData &mesh)
{
    m_mesh = mesh;
    m_highlightedFaces.clear();
    if (!mesh.isEmpty()) {
        QVector3D center = (mesh.bbMin + mesh.bbMax) * 0.5f;
        m_pan = -center;
        float diag = (mesh.bbMax - mesh.bbMin).length();
        m_zoom = diag > 0 ? 150.0f / diag : 1.0f;
    }
    update();
}

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

void ViewportWidget::clearHighlight()
{
    if (m_highlightedFaces.isEmpty()) {
        return;
    }
    m_highlightedFaces.clear();
    update();
}

void ViewportWidget::resetCamera()
{
    m_rotX = 30.0f; m_rotZ = -45.0f; m_zoom = 1.0f; m_pan = {};
    update();
}

void ViewportWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void ViewportWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    m_proj.setToIdentity();
    m_proj.perspective(45.0f, float(w) / float(h ? h : 1), 0.1f, 5000.0f);
}

void ViewportWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_view.setToIdentity();
    m_view.translate(0, 0, -200.0f / m_zoom);
    m_view.rotate(m_rotX, 1, 0, 0);
    m_view.rotate(m_rotZ, 0, 0, 1);
    m_view.translate(m_pan);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m_proj.constData());
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m_view.constData());

    drawGrid();
    drawAxes();
    drawMesh();
    drawContourChoicePreview();
    drawToolPath();
    drawToolMarker();
}

void ViewportWidget::drawGrid()
{
    const int steps = 20;
    const float span = 100.0f;
    const float step = span * 2.0f / steps;

    glLineWidth(1.0f);
    glBegin(GL_LINES);
    glColor4f(0.25f, 0.25f, 0.25f, 1.0f);
    for (int i = 0; i <= steps; ++i) {
        float v = -span + i * step;
        glVertex3f(v, -span, 0); glVertex3f(v,  span, 0);
        glVertex3f(-span, v, 0); glVertex3f( span, v, 0);
    }
    glEnd();
}

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

void ViewportWidget::drawMesh()
{
    if (m_mesh.isEmpty()) return;
    glBegin(GL_TRIANGLES);
    for (const Triangle &tri : m_mesh.triangles) {
        const QVector3D &n = tri.normal;
        if (m_highlightedFaces.contains(tri.faceIndex)) {
            glColor3f(1.0f, 0.45f, 0.1f);
        } else {
            float shade = std::abs(n.z()) * 0.5f + std::abs(n.x()) * 0.3f + std::abs(n.y()) * 0.2f;
            shade = 0.3f + shade * 0.7f;
            glColor3f(shade * 0.6f, shade * 0.75f, shade);
        }
        glNormal3f(n.x(), n.y(), n.z());
        auto v = [this](const QVector3D &p){ glVertex3f(p.x(), p.y(), p.z()); };
        v(tri.v0); v(tri.v1); v(tri.v2);
    }
    glEnd();
}

void ViewportWidget::mousePressEvent(QMouseEvent *e)
{
    m_lastMouse  = e->pos();
    m_pressMouse = e->pos();
    m_mouseButton = e->button();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent *e)
{
    QPoint delta = e->pos() - m_lastMouse;
    m_lastMouse  = e->pos();

    if (m_mouseButton == Qt::LeftButton) {
        m_rotZ += delta.x() * 0.5f;
        m_rotX += delta.y() * 0.5f;
    } else if (m_mouseButton == Qt::MiddleButton || m_mouseButton == Qt::RightButton) {
        float scale = 0.5f / m_zoom;
        m_pan += QVector3D(delta.x() * scale, -delta.y() * scale, 0);
    }
    update();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent *e)
{
    const bool shortLeftClick = m_mouseButton == Qt::LeftButton &&
                                (e->pos() - m_pressMouse).manhattanLength() <= 3;
    if (shortLeftClick) {
        QVector3D hitPoint;
        const int faceIndex = pickFaceAt(e->pos(), &hitPoint);
        if (faceIndex > 0) {
            emit facePicked(faceIndex);
            emit facePointPicked(faceIndex, hitPoint);
        }
    }

    m_mouseButton = 0;
}

void ViewportWidget::wheelEvent(QWheelEvent *e)
{
    float factor = e->angleDelta().y() > 0 ? 1.1f : 1.0f / 1.1f;
    m_zoom *= factor;
    update();
}

int ViewportWidget::pickFaceAt(const QPoint &pos, QVector3D *hitPoint) const
{
    if (m_mesh.isEmpty() || width() <= 0 || height() <= 0) {
        return 0;
    }

    const QMatrix4x4 mvp = m_proj * m_view;
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
            bestPoint = tri.v0 * w0 + tri.v1 * w1 + tri.v2 * w2;
        }
    }

    if (hitPoint && bestFace > 0) {
        *hitPoint = bestPoint;
    }
    return bestFace;
}

void ViewportWidget::setToolPath(const QVector<QVector3D> &path, const QVector<bool> &rapidSegments)
{
    m_toolPath = path;
    m_toolPathRapidSegments = rapidSegments;
    update();
}

void ViewportWidget::setToolPosition(const QVector3D &pos)
{
    m_toolPos    = pos;
    m_hasToolPos = true;
    update();
}

void ViewportWidget::setToolDiameter(double diameter)
{
    m_toolDiameter = diameter > 0.0 ? float(qBound(0.5, diameter, 200.0)) : 6.0f;
    update();
}

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

void ViewportWidget::clearToolPath()
{
    m_toolPath.clear();
    m_toolPathRapidSegments.clear();
    m_hasToolPos = false;
    update();
}

void ViewportWidget::setContourChoicePreview(const QVector<QVector3D> &points,
                                              bool closed,
                                              int cutterSide)
{
    m_contourChoicePoints = points;
    m_contourChoiceClosed = closed;
    m_contourChoiceSide = cutterSide;
    update();
}

void ViewportWidget::clearContourChoicePreview()
{
    if (m_contourChoicePoints.isEmpty()) {
        return;
    }
    m_contourChoicePoints.clear();
    update();
}

void ViewportWidget::drawContourChoicePreview()
{
    if (m_contourChoicePoints.size() < 2) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glLineWidth(3.0f);
    glColor4f(0.1f, 0.85f, 1.0f, 0.95f);
    glBegin(GL_LINE_STRIP);
    for (const QVector3D &point : m_contourChoicePoints) {
        glVertex3f(point.x(), point.y(), point.z());
    }
    if (m_contourChoiceClosed) {
        const QVector3D &first = m_contourChoicePoints.first();
        glVertex3f(first.x(), first.y(), first.z());
    }
    glEnd();

    const QVector3D start = m_contourChoicePoints.at(0);
    const QVector3D next = m_contourChoicePoints.at(1);
    QVector3D direction(next.x() - start.x(), next.y() - start.y(), 0.0f);
    const float segmentLength = direction.length();
    if (segmentLength > 1.0e-5f) {
        direction /= segmentLength;
        const QVector3D left(-direction.y(), direction.x(), 0.0f);
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
    glEnable(GL_DEPTH_TEST);
}

void ViewportWidget::drawToolPath()
{
    if (m_toolPath.size() < 2) return;

    glLineWidth(1.5f);
    glBegin(GL_LINES);
    for (int i = 1; i < m_toolPath.size(); ++i) {
        const bool rapid = (i - 1) < m_toolPathRapidSegments.size() &&
                           m_toolPathRapidSegments[i - 1];
        if (rapid) {
            glColor4f(1.0f, 0.15f, 0.1f, 0.85f);
        } else {
            glColor4f(0.1f, 1.0f, 0.25f, 0.90f);
        }
        const QVector3D &a = m_toolPath[i - 1];
        const QVector3D &b = m_toolPath[i];
        glVertex3f(a.x(), a.y(), a.z());
        glVertex3f(b.x(), b.y(), b.z());
    }
    glEnd();
    glLineWidth(1.0f);
}

void ViewportWidget::drawToolMarker()
{
    if (!m_hasToolPos) return;

    const float x = m_toolPos.x();
    const float y = m_toolPos.y();
    const float z = m_toolPos.z();
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
