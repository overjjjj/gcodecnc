#include "src/workspaces/process_design/selection/SlopedFloorBinding.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTextStream>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTextStream out(stdout);
    const QString path=QString::fromUtf8("D:/yanggao/gcodecnc/untitled/测试模型/验收/特征板.STEP");
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return 2;
    if(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex()!=
       "57878dc9d1f6a590dd756a245e0b3d3272aae6752dbd1b1725dc745b34e05541")return 3;
    StepImporter importer;if(!importer.load(path))return 4;
    for(int face:{217,172,48,213,214}) {
        SelectionChain selection;selection.explicitUserSelection=true;selection.toolAxis={0,0,1};
        SelectionTopologyRef ref;ref.kind=SelectionTopologyKind::Face;ref.id=QString("face:%1").arg(face);
        selection.orderedTopologyRefs={ref};
        for(const auto &loop:importer.mesh().boundaryLoops)if(loop.faceId==ref.id)
            selection.boundaryLoops.append({loop.faceId,loop.outer,loop.orderedEdgeIds,loop.orderedForward});
        const auto result=bindSlopedFloor(importer.mesh(),selection);
        const QString expected=(face==217 || face==172)?"SLOPED_FLOOR_SETUP_DIRECTION_MISMATCH":"SLOPED_FLOOR_PLANE_REQUIRED";
        out<<"FACE "<<face<<" WIRES "<<selection.boundaryLoops.size()<<" ERROR "<<result.error<<Qt::endl;
        if(selection.boundaryLoops.isEmpty() || result.error!=expected)return 5;
    }
    out<<"PASS actual back-facing exit surrounds and conical exit chamfers cannot become +Z planar floors"<<Qt::endl;
    return 0;
}
