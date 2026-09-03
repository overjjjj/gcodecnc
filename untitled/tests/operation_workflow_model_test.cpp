#include "../src/core/FeatureIdentity.h"
#include "../src/core/HoleSelectionSession.h"
#include "../src/core/OperationWorkflow.h"
#include "../src/strategies/MachiningOperation.h"

#include <QCoreApplication>

#include <iostream>

namespace {

int expect(bool condition, const char *message)
{
    if (condition) {
        return 0;
    }
    std::cerr << message << '\n';
    return 1;
}

HoleSelectionRecord hole(const QString &id, double diameter, float x, float y,
                         int selectionOrder)
{
    HoleSelectionRecord record;
    record.geometryId = id;
    record.diameter = diameter;
    record.center = QVector3D(x, y, 0.0f);
    record.selectionOrder = selectionOrder;
    return record;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    MachiningFeature feature;
    feature.kind = FeatureKind::Hole;
    feature.subType = QStringLiteral("through_hole");
    feature.radius = 5.0;
    feature.depth = 20.0;
    feature.center = QVector3D(10.0f, 15.0f, -5.0f);
    feature.axis = QVector3D(0.0f, 0.0f, 1.0f);
    feature.faceIndices = {9, 3, 7};

    const QString firstId = stableFeatureId(feature);
    feature.faceIndices = {7, 9, 3};
    const QString reorderedId = stableFeatureId(feature);
    if (expect(!firstId.isEmpty(), "stable feature id must not be empty") ||
        expect(firstId == reorderedId,
               "stable feature id must not depend on face-index order")) {
        return 1;
    }
    feature.faceIndices = {107, 109, 103};
    if (expect(stableFeatureId(feature) == firstId,
               "stable feature id must survive face renumbering after re-import")) {
        return 1;
    }
    feature.center.setX(10.5f);
    if (expect(stableFeatureId(feature) != firstId,
               "a geometric change must invalidate the stable feature id")) {
        return 1;
    }

    ContourFeature contour;
    contour.subType = QStringLiteral("closed_contour");
    contour.depth = 4.0;
    contour.points = {
        QVector3D(0.0f, 0.0f, 0.0f),
        QVector3D(10.0f, 0.0f, 0.0f),
        QVector3D(10.0f, 10.0f, 0.0f),
        QVector3D(0.0f, 10.0f, 0.0f),
    };
    const QString contourId = stableContourId(contour);
    contour.points = {
        QVector3D(10.0f, 10.0f, 0.0f),
        QVector3D(0.0f, 10.0f, 0.0f),
        QVector3D(0.0f, 0.0f, 0.0f),
        QVector3D(10.0f, 0.0f, 0.0f),
    };
    if (expect(!contourId.isEmpty(), "stable contour id must not be empty") ||
        expect(stableContourId(contour) == contourId,
               "closed-contour identity must ignore start point and direction")) {
        return 1;
    }

    HoleSelectionSession session({hole(QStringLiteral("A"), 8.0, 0.0f, 0.0f, 0)});
    if (expect(session.add(hole(QStringLiteral("B"), 12.0, 20.0f, 0.0f, 1)),
               "a new geometry id should be selectable") ||
        expect(!session.add(hole(QStringLiteral("B"), 12.0, 20.0f, 0.0f, 2)),
               "the same geometry id must not be selected twice") ||
        expect(session.add(hole(QStringLiteral("C"), 6.0, 5.0f, 0.0f, 2)),
               "a second unique geometry should be selectable")) {
        return 1;
    }

    session.setDisplaySerialNumbers(true);
    if (expect(session.displaySerialNumbers(), "serial-number display state should be retained") ||
        expect(session.sort(HoleSelectionSort::DiameterDescending),
               "diameter sorting should change the current order") ||
        expect(session.records().at(0).geometryId == QStringLiteral("B") &&
                   session.records().at(2).geometryId == QStringLiteral("C"),
               "diameter-descending sorting should be deterministic")) {
        return 1;
    }

    if (expect(session.undo(), "sorting must be undoable") ||
        expect(session.records().at(0).geometryId == QStringLiteral("A") &&
                   session.records().at(1).geometryId == QStringLiteral("B"),
               "undo should restore selection order") ||
        expect(session.redo(), "an undone sort must be recoverable") ||
        expect(session.records().at(0).geometryId == QStringLiteral("B"),
               "redo should restore the sorted order")) {
        return 1;
    }

    const QList<HoleSelectionRecord> committed = session.commit();
    if (expect(committed.size() == 3 && !session.isDirty(),
               "commit should establish a clean baseline")) {
        return 1;
    }
    if (expect(session.remove(QStringLiteral("B")), "selected geometry should be removable") ||
        expect(session.isDirty(), "removing a committed item should dirty the session")) {
        return 1;
    }
    session.cancel();
    if (expect(session.records().size() == 3 &&
                   session.records().at(0).geometryId == QStringLiteral("B") &&
                   !session.isDirty(),
               "cancel should restore the last committed baseline")) {
        return 1;
    }

    HoleSelectionSession nearestSession({
        hole(QStringLiteral("start"), 4.0, 0.0f, 0.0f, 0),
        hole(QStringLiteral("far"), 4.0, 10.0f, 0.0f, 1),
        hole(QStringLiteral("near"), 4.0, 2.0f, 0.0f, 2),
        hole(QStringLiteral("next"), 4.0, 3.0f, 0.0f, 3),
    });
    if (expect(nearestSession.sort(HoleSelectionSort::NearestNeighbor),
               "nearest-neighbor sorting should reorder a non-optimal list") ||
        expect(nearestSession.records().at(0).geometryId == QStringLiteral("start") &&
                   nearestSession.records().at(1).geometryId == QStringLiteral("near") &&
                   nearestSession.records().at(2).geometryId == QStringLiteral("next") &&
                   nearestSession.records().at(3).geometryId == QStringLiteral("far"),
               "nearest-neighbor sorting should start at the first selected point")) {
        return 1;
    }

    MachiningOperation operation;
    if (expect(operation.enabled, "new operations should be enabled") ||
        expect(operation.toolpathState == ToolpathState::Empty,
               "new operations should start without a toolpath")) {
        return 1;
    }
    operation.geometryRefs = QStringList{firstId};
    operation.markToolpathValid();
    if (expect(operation.toolpathState == ToolpathState::Valid,
               "successful generation should mark a toolpath valid") ||
        expect(operation.warnings.isEmpty(), "valid toolpaths should clear stale warnings")) {
        return 1;
    }
    operation.markToolpathStale(QStringLiteral("geometry changed"));
    if (expect(operation.toolpathState == ToolpathState::Stale,
               "geometry changes should mark a toolpath stale") ||
        expect(operation.warnings.contains(QStringLiteral("geometry changed")),
               "stale operations should explain why recalculation is required")) {
        return 1;
    }
    operation.markToolpathError(QStringLiteral("tool is unavailable"));
    if (expect(operation.toolpathState == ToolpathState::Error,
               "a failed recalculation should mark the toolpath as an error") ||
        expect(operation.warnings == QStringList{QStringLiteral("tool is unavailable")},
               "an error state should retain only the current failure reason")) {
        return 1;
    }

    MachiningOperation disabledOperation = operation;
    disabledOperation.id = QStringLiteral("disabled");
    disabledOperation.enabled = false;
    MachiningOperation enabledOperation = operation;
    enabledOperation.id = QStringLiteral("enabled");
    enabledOperation.enabled = true;
    const QList<MachiningOperation> executable = enabledOperations(
        {disabledOperation, enabledOperation});
    if (expect(executable.size() == 1 && executable.first().id == QStringLiteral("enabled"),
               "disabled operations must not be sent to program generation")) {
        return 1;
    }

    QList<MachiningOperation> changedOperations{enabledOperation, disabledOperation};
    markOperationsStale(changedOperations, QStringLiteral("stock changed"));
    if (expect(changedOperations.at(0).toolpathState == ToolpathState::Stale &&
                   changedOperations.at(1).toolpathState == ToolpathState::Stale,
               "a dependency change should invalidate every affected operation") ||
        expect(changedOperations.at(0).warnings.contains(QStringLiteral("stock changed")),
               "bulk invalidation should retain the recalculation reason")) {
        return 1;
    }

    std::cout << "PASS operation_workflow_model_test\n";
    return 0;
}
