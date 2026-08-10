#include "src/core/SetupOrigin.h"

#include <QCoreApplication>
#include <QTextStream>

#include <cmath>
#include <cstdlib>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << Qt::endl;
        std::exit(1);
    }
}

bool closeTo(const QVector3D &actual, const QVector3D &expected)
{
    return (actual - expected).length() < 1.0e-5f;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QVector3D bbMin(-10.0f, 20.0f, -5.0f);
    const QVector3D bbMax(30.0f, 60.0f, 15.0f);

    SetupOrigin origin;
    origin.anchor = SetupOriginAnchor::TopCenter;
    require(closeTo(origin.resolvedPoint(bbMin, bbMax), QVector3D(10.0f, 40.0f, 15.0f)),
            "top-center anchor should resolve from the model bounds");

    origin.anchor = SetupOriginAnchor::TopMinXMaxY;
    origin.offset = QVector3D(1.5f, -2.0f, 3.0f);
    require(closeTo(origin.resolvedPoint(bbMin, bbMax), QVector3D(-8.5f, 58.0f, 18.0f)),
            "anchor offset should be applied in Setup coordinates");

    origin.anchor = SetupOriginAnchor::CustomPoint;
    origin.customPoint = QVector3D(4.0f, 5.0f, 6.0f);
    require(closeTo(origin.resolvedPoint(bbMin, bbMax), QVector3D(5.5f, 3.0f, 9.0f)),
            "custom point should support a measured or hole-center origin");

    origin.confirmed = true;
    const QString originalFingerprint = origin.fingerprint(QStringLiteral("G55"));
    require(!originalFingerprint.isEmpty(), "confirmed origin should have a stable fingerprint");
    require(originalFingerprint == origin.fingerprint(QStringLiteral("g55")),
            "work-offset spelling should be normalized in the fingerprint");
    origin.offset.setX(origin.offset.x() + 0.01f);
    require(originalFingerprint != origin.fingerprint(QStringLiteral("G55")),
            "changing the origin should invalidate the Setup fingerprint");
    require(origin.fingerprint(QStringLiteral("G54")) != origin.fingerprint(QStringLiteral("G55")),
            "changing WCS should invalidate the Setup fingerprint");

    QTextStream(stdout) << "PASS setup_origin_test" << Qt::endl;
    return 0;
}
