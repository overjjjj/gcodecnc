#include <iostream>

#include "../src/workspaces/process_design/selection/MachiningPlaneBoundaryFilter.h"

int main()
{
    MeshData mesh;
    Triangle front;
    front.faceIndex = 1;
    front.normal = QVector3D(0, 0, 1);
    Triangle side = front;
    side.faceIndex = 2;
    side.normal = QVector3D(1, 0, 0);
    Triangle back = front;
    back.faceIndex = 3;
    back.normal = QVector3D(0, 0, -1);
    mesh.triangles = {front, side, back};

    MeshBoundaryLoop frontOuter;
    frontOuter.faceIndex = 1;
    frontOuter.faceId = QStringLiteral("face:1");
    frontOuter.outer = true;
    frontOuter.orderedEdgeIds = QStringList{QStringLiteral("edge:shared"),
                                            QStringLiteral("edge:front")};
    MeshBoundaryLoop frontIsland = frontOuter;
    frontIsland.outer = false;
    frontIsland.orderedEdgeIds = QStringList{QStringLiteral("edge:island")};
    MeshBoundaryLoop sideOuter = frontOuter;
    sideOuter.faceIndex = 2;
    sideOuter.faceId = QStringLiteral("face:2");
    sideOuter.orderedEdgeIds = QStringList{QStringLiteral("edge:shared"),
                                           QStringLiteral("edge:side")};
    mesh.boundaryLoops = {frontOuter, frontIsland, sideOuter};

    const QVector<int> faces = machiningPlaneFaceIndices(
        mesh, QVector<int>{1, 2, 3}, QVector3D(0, 0, 1));
    if (faces != QVector<int>{1}) {
        std::cerr << "front machining-plane faces must follow Setup tool axis\n";
        return 1;
    }
    const QSet<QString> edges = machiningPlaneBoundaryEdgeIds(mesh, faces);
    if (!edges.contains(QStringLiteral("edge:shared")) ||
        !edges.contains(QStringLiteral("edge:front")) ||
        !edges.contains(QStringLiteral("edge:island")) ||
        edges.contains(QStringLiteral("edge:side"))) {
        std::cerr << "shared/front/island edges must remain while side-only edges are filtered\n";
        return 1;
    }
    std::cout << "PASS machining-plane face and boundary filtering\n";
    return 0;
}
