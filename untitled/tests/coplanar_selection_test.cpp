#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include <QApplication>
#include <QPushButton>
#include <QComboBox>
#include <QListWidget>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <STEPControl_Writer.hxx>
#include "../src/workspaces/process_design/selection/CoplanarSelection.h"
#include "../src/core/ProjectManager.h"
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x << std::endl; return 1; } } while (0)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    TopoDS_Compound compound;
    BRep_Builder builder; builder.MakeCompound(compound);
    builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),40,20,10).Shape());
    builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(60,0,0),40,20,10).Shape());
    builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(120,0,0),40,20,6).Shape());
    builder.Add(compound, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(180,0,0),gp_Dir(0,0,1)),5,8).Shape());
    STEPControl_Writer writer; writer.Transfer(compound, STEPControl_AsIs);
    const QString step = argc > 1 ? QString::fromLocal8Bit(argv[1]) : dir.filePath("regions.step");
    CHECK(writer.Write(step.toLocal8Bit().constData()) == IFSelect_RetDone);
    StepImporter importer; CHECK(importer.load(step));
    const auto mesh = importer.mesh();
    const auto planes = selectionSourcePlanes(mesh);
    QList<SelectionCandidate> candidates;
    QSet<int> seen;
    int curved = 0;
    for (const auto &wire : mesh.boundaryLoops) {
        if (seen.contains(wire.faceIndex)) continue;
        seen.insert(wire.faceIndex);
        SelectionCandidate candidate;
        candidate.geometryId = QString("topology:face:%1").arg(wire.faceIndex);
        candidate.faceIds = QStringList{wire.faceId};
        candidate.label = candidate.geometryId;
        candidate.sourcePlane = planes.value(wire.faceIndex);
        if (!candidate.sourcePlane.verified) { ++curved; continue; }
        candidate.surfaceNormal = candidate.sourcePlane.normal;
        candidate.topologyOnly = true;
        QSet<QString> edges;
        for (const auto &loop : mesh.boundaryLoops) if (loop.faceIndex == wire.faceIndex) {
            candidate.boundaryLoops.append(loop);
            for (const auto &id : loop.orderedEdgeIds) edges.insert(id);
        }
        for (const auto &edge : mesh.pickEdges)
            if (edges.contains(edge.id)) candidate.topologyEdges.append(edge);
        candidates.append(candidate);
    }
    CHECK(curved > 0);
    QString seed;
    int expected = 0;
    for (const auto &candidate : candidates)
        if (candidate.sourcePlane.normal.z() > .99 && std::abs(candidate.sourcePlane.offset) < 1e-5) {
            seed = candidate.geometryId; ++expected;
        }
    CHECK(expected == 2);
    SelectionChainDialog dialog;
    dialog.setContext("source", "setup", "G54"); dialog.setCandidates(candidates);
    auto *mode = dialog.findChild<QComboBox *>("selectionMode"); CHECK(mode);
    mode->setCurrentIndex(int(ChainSelectionMode::Face));
    QMetaObject::invokeMethod(mode,"activated",Q_ARG(int,int(ChainSelectionMode::Face)));
    dialog.selectGeometry(seed); dialog.selectTopology(seed.mid(9),0,{},0);
    auto *coplanar = dialog.findChild<QPushButton *>("selectionCoplanar");
    CHECK(coplanar && coplanar->isEnabled());
    QStringList highlighted;
    QVector<QVector3D> connectedPreview;
    dialog.facePreviewChanged = [&](const QStringList &faces) { highlighted = faces; };
    dialog.previewChanged = [&](const QVector<QVector3D> &points) { connectedPreview = points; };
    coplanar->click();
    auto *regions = dialog.findChild<QListWidget *>("selectionRegions");
    CHECK(regions && regions->count() == 2 && highlighted.size() == 2 && connectedPreview.isEmpty());
    regions->setCurrentRow(1); dialog.findChild<QPushButton *>("selectionRemoveRegion")->click();
    CHECK(regions->count() == 1);
    dialog.findChild<QPushButton *>("selectionUndo")->click(); CHECK(regions->count() == 2);
    CHECK(!dialog.findChild<QPushButton *>("selectionReverse")->isEnabled());
    auto *side = dialog.findChild<QComboBox *>("selectionSide"); CHECK(side);
    side->setCurrentIndex(int(ChainMachiningSide::Outside));
    QMetaObject::invokeMethod(side,"activated",Q_ARG(int,int(ChainMachiningSide::Outside)));
    dialog.accept(); CHECK(dialog.result() == QDialog::Accepted);
    const auto selections = dialog.selections(); CHECK(selections.size() == 2);
    for (const auto &selection : selections) {
        CHECK(selection.orderedTopologyRefs.size() == 1 && selection.boundaryLoops.size() == 1);
        CHECK(selection.machiningSide == ChainMachiningSide::Outside);
        CHECK(dialog.selectedPoints(selection).size() == 4);
    }
    CHECK(dialog.selectedPoints(selections[0]) != dialog.selectedPoints(selections[1]));
    SelectionChainDialog restored;
    restored.setContext("source","setup","G54"); restored.setCandidates(candidates);
    restored.setPreviousSelections(selections);
    restored.findChild<QPushButton *>("selectionLast")->click(); restored.accept();
    CHECK(restored.selections() == selections);
    SelectionChainDialog differentStrategy;
    differentStrategy.setContext("source","setup","G54"); differentStrategy.setCandidates(candidates);
    differentStrategy.acceptanceError = [&](const SelectionChain &, const QString &id) {
        return id == selections.last().candidateGeometryId ? QString("unsupported region") : QString();
    };
    differentStrategy.setPreviousSelections(selections);
    differentStrategy.findChild<QPushButton *>("selectionLast")->click();
    auto *restoredRegions = differentStrategy.findChild<QListWidget *>("selectionRegions");
    CHECK(restoredRegions->count() == 2);
    differentStrategy.accept(); CHECK(differentStrategy.result() != QDialog::Accepted);
    restoredRegions->setCurrentRow(1);
    differentStrategy.findChild<QPushButton *>("selectionRemoveRegion")->click();
    differentStrategy.accept(); CHECK(differentStrategy.selections().size() == 1);
    auto altered = candidates;
    for (auto &candidate : altered)
        if (candidate.geometryId == selections.last().candidateGeometryId) candidate.boundaryLoops.clear();
    SelectionChainDialog stale;
    stale.setContext("source","setup","G54"); stale.setCandidates(altered); stale.setPreviousSelections(selections);
    stale.findChild<QPushButton *>("selectionLast")->click(); stale.accept();
    CHECK(stale.selections().isEmpty());
    ProjectManager project;
    MachiningOperation older; older.id="older"; older.selectionEvidence=selections.first();
    project.setOperations({older});
    auto a=older,b=older; a.id="a"; b.id="b"; a.selectionGroupId=b.selectionGroupId="group";
    a.selectionEvidence=selections.first(); b.selectionEvidence=selections.last();
    project.setOperations({older,a,b}); CHECK(project.lastEstablishedSelections()==selections);
    project.setOperations({b,older,a}); CHECK(project.lastEstablishedSelections()==selections);
    project.setOperations({older,a}); CHECK(project.lastEstablishedSelections()==selections);
    const QString save=dir.filePath("group.json"); CHECK(project.saveToFile(save));
    ProjectManager loaded; CHECK(loaded.loadFromFile(save)); CHECK(loaded.lastEstablishedSelections()==selections);
    loaded.setOperations({older}); CHECK(loaded.lastEstablishedSelections()==QList<SelectionChain>{older.selectionEvidence});
    QFile file(save); CHECK(file.open(QIODevice::ReadOnly));
    auto json=QJsonDocument::fromJson(file.readAll()).object(); file.close();
    auto groups=json["selectionGroups"].toObject(); auto group=groups["group"].toObject();
    auto evidence=group["regions"].toArray(); evidence.removeLast(); group["regions"]=evidence;
    groups["group"]=group; json["selectionGroups"]=groups;
    CHECK(file.open(QIODevice::WriteOnly|QIODevice::Truncate)); file.write(QJsonDocument(json).toJson()); file.close();
    CHECK(loaded.loadFromFile(save)); CHECK(loaded.lastEstablishedSelections().isEmpty());
    std::cout << "PASS STEP planes, independent regions, removal/undo, whole-group restoration and persistence\n";
    return 0;
}
