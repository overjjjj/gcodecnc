#include "../src/import/StepImporter.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

bool near(double actual, double expected, double tolerance = 0.02)
{
    return std::abs(actual - expected) <= tolerance;
}

struct ExpectedHole {
    double x;
    double y;
    double radius;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString fixturePath = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("../测试模型/孔.stp"));
    QFile fixture(fixturePath);
    if (!expect(fixture.open(QIODevice::ReadOnly),
                "the committed hole STEP fixture should be readable")) {
        return 1;
    }
    const QString fingerprint = QString::fromLatin1(
        QCryptographicHash::hash(fixture.readAll(), QCryptographicHash::Sha256)
            .toHex()
            .toUpper());
    if (!expect(fingerprint == QStringLiteral(
                    "8285E06022B3A6FA447AB6060811F647E3ED35FE4D858A125C7CF1E1E33896A6"),
                "the hole STEP fixture fingerprint should remain unchanged")) {
        return 1;
    }

    StepImporter importer;
    if (!expect(importer.load(fixturePath),
                importer.lastError().toUtf8().constData())) {
        return 1;
    }
    if (!expect(!importer.mesh().isEmpty(),
                "the hole fixture should produce a non-empty mesh")) {
        return 1;
    }
    const bool stableBounds = near(importer.mesh().bbMin.x(), -44.0) &&
                              near(importer.mesh().bbMin.y(), -35.0) &&
                              near(importer.mesh().bbMin.z(), -10.0) &&
                              near(importer.mesh().bbMax.x(), 68.0) &&
                              near(importer.mesh().bbMax.y(), 27.0) &&
                              near(importer.mesh().bbMax.z(), 0.0);
    if (!expect(stableBounds, "the imported stock bounds should remain stable")) {
        std::cerr << "bounds min=(" << importer.mesh().bbMin.x() << ','
                  << importer.mesh().bbMin.y() << ',' << importer.mesh().bbMin.z()
                  << ") max=(" << importer.mesh().bbMax.x() << ','
                  << importer.mesh().bbMax.y() << ',' << importer.mesh().bbMax.z()
                  << ")\n";
        return 1;
    }

    QVector<MachiningFeature> holes;
    for (const MachiningFeature &feature : importer.features()) {
        if (feature.kind == FeatureKind::Hole || feature.kind == FeatureKind::Thread) {
            holes.append(feature);
        }
    }
    std::sort(holes.begin(), holes.end(), [](const MachiningFeature &left,
                                             const MachiningFeature &right) {
        return left.center.x() < right.center.x();
    });

    const ExpectedHole expected[] = {
        {-13.803439, -18.785132, 7.724859},
        {25.427388, 8.717962, 9.981747},
        {47.741219, -18.785132, 9.501312}};
    if (!expect(holes.size() == 3,
                "the fixture should recognize exactly three holes")) {
        std::cerr << "recognized hole-like features: " << holes.size() << '\n';
        for (int index = 0; index < holes.size(); ++index) {
            const MachiningFeature &hole = holes[index];
            std::cerr << "candidate " << index
                      << " center=(" << hole.center.x() << ',' << hole.center.y() << ','
                      << hole.center.z() << ") radius=" << hole.radius
                      << " depth=" << hole.depth
                      << " subtype=" << hole.subType.toStdString()
                      << " faces=" << hole.faceIndices.size() << '\n';
        }
        return 1;
    }

    for (int index = 0; index < holes.size(); ++index) {
        const MachiningFeature &hole = holes[index];
        if (!expect(hole.kind == FeatureKind::Hole,
                    "plain cylindrical bores must not be classified as threads") ||
            !expect(hole.subType == QStringLiteral("through_hole"),
                    "all three fixture bores should be through holes") ||
            !expect(hole.region == FaceRegion::Front,
                    "through holes opening on both sides should belong to the active front Setup") ||
            !expect(near(hole.center.x(), expected[index].x) &&
                        near(hole.center.y(), expected[index].y) &&
                        near(hole.center.z(), -5.0),
                    "recognized hole centers should match the STEP cylinders") ||
            !expect(near(hole.radius, expected[index].radius) &&
                        near(hole.depth, 10.0),
                    "recognized hole radius and depth should match the fixture") ||
            !expect(std::abs(hole.axis.z()) > 0.999 &&
                        std::abs(hole.axis.x()) < 0.001 &&
                        std::abs(hole.axis.y()) < 0.001,
                    "fixture hole axes should remain parallel to the Z Setup") ||
            !expect(!hole.faceIndices.isEmpty(),
                    "recognized holes should retain their STEP face association")) {
            std::cerr << "hole " << index
                      << " center=(" << hole.center.x() << ',' << hole.center.y() << ','
                      << hole.center.z() << ") radius=" << hole.radius
                      << " depth=" << hole.depth
                      << " subtype=" << hole.subType.toStdString() << '\n';
            return 1;
        }
    }

    for (const MachiningFeature &hole : holes) {
        if (!expect(!near(hole.radius, 5.0),
                    "the stock corner cylinder must not be recognized as a hole")) {
            return 1;
        }
    }

    return 0;
}
