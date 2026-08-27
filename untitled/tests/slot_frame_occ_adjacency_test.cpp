#include "../src/import/SlotFrameRecognizer.h"
#include "../src/import/TopoAnalyzer.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <QCoreApplication>

#include <cmath>
#include <iostream>

namespace {

MachiningFeature BuildPocketCandidate(const TopoGraph &graph)
{
    MachiningFeature pocket;
    for (int face_index = 1; face_index <= graph.faceMap.Extent(); ++face_index) {
        if (graph.nodes.at(face_index - 1).surfType != GeomAbs_Plane ||
            std::abs(graph.nodes.at(face_index - 1).normal.z()) < 0.9f) {
            continue;
        }
        GProp_GProps properties;
        BRepGProp::SurfaceProperties(
            TopoDS::Face(graph.faceMap(face_index)), properties);
        if (std::abs(properties.CentreOfMass().Z() + 5.0) > 1.0e-6) {
            continue;
        }

        pocket.kind = FeatureKind::Pocket;
        pocket.subType = QStringLiteral("rectangular_pocket");
        pocket.region = FaceRegion::Front;
        pocket.axis = QVector3D(0.0f, 0.0f, 1.0f);
        pocket.depth = 5.0;
        pocket.width = 20.0;
        pocket.length = 40.0;
        pocket.faceIndices.append(face_index);
        for (const auto &entry : graph.aag.value(face_index)) {
            const int neighbor_index = entry.first;
            if (graph.nodes.at(neighbor_index - 1).surfType == GeomAbs_Plane &&
                std::abs(graph.nodes.at(neighbor_index - 1).normal.z()) < 0.1f) {
                pocket.faceIndices.append(neighbor_index);
            }
        }

        const TopoDS_Wire outer = BRepTools::OuterWire(
            TopoDS::Face(graph.faceMap(face_index)));
        for (BRepTools_WireExplorer explorer(outer);
             explorer.More(); explorer.Next()) {
            const gp_Pnt point = BRep_Tool::Pnt(explorer.CurrentVertex());
            pocket.boundaryPoints.append(
                QVector3D(float(point.X()), float(point.Y()), float(point.Z())));
        }
        break;
    }
    return pocket;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const TopoDS_Shape stock = BRepPrimAPI_MakeBox(
        gp_Pnt(0.0, 0.0, -10.0), 60.0, 40.0, 10.0).Shape();
    const TopoDS_Shape cutter = BRepPrimAPI_MakeBox(
        gp_Pnt(10.0, 10.0, -5.0), 40.0, 20.0, 6.0).Shape();
    BRepAlgoAPI_Cut cut(stock, cutter);
    cut.Build();
    if (!cut.IsDone()) {
        std::cerr << "FAIL: OCC pocket fixture could not be built\n";
        return 1;
    }

    const TopoGraph graph = TopoAnalyzer().analyze(cut.Shape());
    const MachiningFeature pocket = BuildPocketCandidate(graph);
    if (pocket.faceIndices.size() != 5 || pocket.boundaryPoints.size() != 4) {
        std::cerr << "FAIL: OCC fixture should expose one bottom and four walls\n";
        return 1;
    }

    SetupContext setup;
    setup.workOffset = QStringLiteral("G54");
    setup.origin.confirmed = true;
    const SlotFrameTopologyEvidence evidence =
        SlotFrameRecognizer::BuildEvidence(graph, pocket);
    const SlotFrameRecognitionResult result =
        SlotFrameRecognizer::Recognize(pocket, setup, evidence);
    if (!result.ok || !result.feature.closed ||
        evidence.edgeFaceCounts.size() != pocket.boundaryPoints.size()) {
        std::cerr << "FAIL: actual OCC edge-face and vertex-edge evidence should "
                     "confirm the closed pocket\n";
        return 1;
    }
    for (int count : evidence.edgeFaceCounts) {
        if (count != 2) {
            std::cerr << "FAIL: every verified bottom boundary edge needs two faces\n";
            return 1;
        }
    }
    return 0;
}
