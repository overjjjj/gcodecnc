#include "../src/import/StepImporter.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>

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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString fixturePath = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral(
                                        "../测试模型/验收/WH250852-模板紅色面加工（零件3）.STEP"));
    QFile fixture(fixturePath);
    if (!expect(fixture.open(QIODevice::ReadOnly),
                "the committed acceptance STEP model should be readable")) {
        return 1;
    }
    const QString fingerprint = QString::fromLatin1(
        QCryptographicHash::hash(fixture.readAll(), QCryptographicHash::Sha256)
            .toHex().toUpper());
    if (!expect(fingerprint == QStringLiteral(
                    "7728E246C323C2B4BD9D6BDE7066BC90DF2E94CED7AD2001D896012C86A1FA50"),
                "the acceptance STEP fingerprint should remain unchanged")) {
        return 1;
    }

    StepImporter importer;
    if (!expect(importer.load(fixturePath),
                importer.lastError().toUtf8().constData())) {
        return 1;
    }
    const double modelZSpan = importer.mesh().bbMax.z() - importer.mesh().bbMin.z();
    if (!expect(modelZSpan > 49.9 && modelZSpan < 50.1,
                "the acceptance model Z thickness should remain stable")) {
        return 1;
    }

    int sideAxisSlots = 0;
    for (const MachiningFeature &feature : importer.features()) {
        if (feature.kind != FeatureKind::Slot) {
            continue;
        }
        if (std::abs(feature.axis.normalized().z()) < 0.65) {
            if (feature.region == FaceRegion::Side) {
                ++sideAxisSlots;
            }
            continue;
        }
        if (feature.region != FaceRegion::Front) {
            continue;
        }
        if (!expect(feature.depth <= modelZSpan + 0.1,
                    "a front slot depth must not exceed the model thickness")) {
            std::cerr << "invalid slot center=(" << feature.center.x() << ','
                      << feature.center.y() << ',' << feature.center.z()
                      << ") depth=" << feature.depth
                      << " modelZSpan=" << modelZSpan << '\n';
            return 1;
        }
    }
    if (!expect(sideAxisSlots >= 4,
                "known side-axis slots should remain recognized as side features")) {
        return 1;
    }
    return 0;
}
