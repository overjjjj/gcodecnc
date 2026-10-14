#include "../src/core/ProjectManager.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>

static int failures=0;
static void check(bool ok,const char *message){if(!ok){++failures;std::cerr<<"FAIL "<<message<<std::endl;}}
static QJsonObject read(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(file.readAll()).object();}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    QCoreApplication app(argc,argv);QTemporaryDir directory;ProjectManager project;
    MachiningOperation operation;operation.id="curve-evidence";operation.strategyId="mill_pocket_rough";
    operation.markToolpathValid();
    for(const auto role:{MachiningBoundaryRole::Outer,MachiningBoundaryRole::Island}) {
        SelectionBoundaryLoop loop;loop.faceId="face:1";loop.machiningRole=role;loop.maximumDepth=1;loop.boundaryTolerance=.002;
        loop.supportFaceIds=QStringList{"face:2"};loop.machiningBoundary={{0,0,0},{2,0,0},{0,2,0}};
        MachiningBoundaryCurve curve;curve.start={0,0,0};curve.end={2,0,0};loop.curves.append(curve);
        operation.selectionEvidence.boundaryLoops.append(loop);
    }
    project.setOperations({operation});const auto path=directory.filePath("input.cnext");
    check(project.saveToFile(path),"save seed project");
    auto root=read(path);auto operations=root["operations"].toArray();auto saved=operations[0].toObject();
    auto selection=saved["selectionEvidence"].toObject();auto loops=selection["boundaryLoops"].toArray();
    const QJsonObject curveFields{{"type","bspline"},{"firstParameter",.125},{"lastParameter",.875},{"approximationBound",.0001}};
    const QJsonObject proofFields{{"discretizationRule","occ-bezier-finite-chord-v1"},{"candidateRule","source-polygon-inset-candidate-v1"},{"volumeProofRule","occ-boundary-component-escape-v1"},{"sourceBoundaryBound",.0002},{"consumedBoundaryBound",.0003},{"volumeProofBudget",.0000012},{"maximumEscapeBudget",.0000015}};
    for(int i=0;i<loops.size();++i) {
        auto loop=loops[i].toObject();for(auto it=proofFields.begin();it!=proofFields.end();++it)loop[it.key()]=it.value();
        QJsonObject curve{{"start",QJsonArray{0,0,0}},{"end",QJsonArray{2,0,0}},{"center",QJsonArray{0,0,0}},{"circular",false},{"sweep",0}};
        for(auto it=curveFields.begin();it!=curveFields.end();++it)curve[it.key()]=it.value();
        loop["curves"]=QJsonArray{curve};loop["machiningBoundary"]=QJsonArray{QJsonArray{0,0,0},QJsonArray{2,0,0},QJsonArray{0,2,0}};loops[i]=loop;
    }
    selection["boundaryLoops"]=loops;saved["selectionEvidence"]=selection;operations[0]=saved;root["operations"]=operations;
    QFile file(path);check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"open enriched project");file.write(QJsonDocument(root).toJson());file.close();
    ProjectManager restored;check(restored.loadFromFile(path),"load enriched project");
    const auto output=directory.filePath("roundtrip.cnext");check(restored.saveToFile(output),"save enriched project");
    const auto roundtrip=read(output)["operations"].toArray()[0].toObject()["selectionEvidence"].toObject()["boundaryLoops"].toArray();
    check(roundtrip.size()==2,"both boundary loops survive roundtrip");
    for(int i=0;i<roundtrip.size();++i) {
        const auto loop=roundtrip[i].toObject();const auto curves=loop["curves"].toArray();
        check(loop["machiningRole"].toInt()==int(i==0?MachiningBoundaryRole::Outer:MachiningBoundaryRole::Island),"boundary roles survive roundtrip");
        for(auto it=proofFields.begin();it!=proofFields.end();++it)check(loop[it.key()]==it.value(),qPrintable(QString("role %1 preserves %2").arg(i).arg(it.key())));
        check(curves.size()==1,"both roles retain curves");
        if(curves.size()==1)for(auto it=curveFields.begin();it!=curveFields.end();++it)check(curves[0].toObject()[it.key()]==it.value(),qPrintable(QString("role %1 preserves curve %2").arg(i).arg(it.key())));
        check(loop["machiningBoundary"].toArray().size()==3,"both roles retain consumed polygon");
    }
    const auto referenceOperations=restored.operations();const QStringList referenceIds{operation.id};
    auto stableId=[](const SelectionChain &selection) {
        SelectionChainController controller(selection);controller.begin();
        controller.addGeometry("edge:test",QVector3D(0,0,0));return controller.commit().id;
    };
    const auto referenceFingerprint=ProgramSnapshotFingerprint::calculate(referenceOperations,referenceIds);
    auto priorRule=referenceOperations;
    priorRule[0].selectionEvidence.boundaryLoops[0].volumeProofRule="occ-boundary-escape-v1";
    ProjectManager priorProject;priorProject.setOperations(priorRule);
    check(priorProject.saveToFile(path),"save prior all-face proof rule");
    ProjectManager priorReloaded;check(priorReloaded.loadFromFile(path),"reload prior all-face proof rule");
    const auto priorOps=priorReloaded.operations();
    check(priorOps[0].selectionEvidence.boundaryLoops[0].volumeProofRule=="occ-boundary-escape-v1","prior rule is preserved without automatic upgrade");
    check(!openingEvidenceSupported(priorOps[0].selectionEvidence.boundaryLoops[0]),"prior all-face rule requires explicit reselection");
    check(ProgramSnapshotStatus::evaluate(priorOps,referenceIds,ProgramSnapshotFingerprint::calculate(priorOps,referenceIds),"setup","setup","tools","tools",true).state==ProgramSnapshotState::Changed,"prior rule remains stale with matching fingerprint");
    for(int role=0;role<2;++role)for(int field=0;field<11;++field) {
        auto changed=referenceOperations;auto &loop=changed[0].selectionEvidence.boundaryLoops[role];
        switch(field) {
        case 0:loop.discretizationRule+="changed";break;
        case 1:loop.candidateRule+="changed";break;
        case 2:loop.volumeProofRule+="changed";break;
        case 3:loop.sourceBoundaryBound+=.00001;break;
        case 4:loop.consumedBoundaryBound+=.00001;break;
        case 5:loop.volumeProofBudget+=.00000001;break;
        case 6:loop.maximumEscapeBudget+=.00000001;break;
        case 7:loop.curves[0].type="bezier";break;
        case 8:loop.curves[0].firstParameter+=.001;break;
        case 9:loop.curves[0].lastParameter+=.001;break;
        case 10:loop.curves[0].approximationBound+=.00001;break;
        }
        const auto label=QString("role %1 evidence field %2").arg(role).arg(field);
        check(!(loop==referenceOperations[0].selectionEvidence.boundaryLoops[role]),qPrintable(label+" changes equality"));
        check(ProgramSnapshotFingerprint::calculate(changed,referenceIds)!=referenceFingerprint,qPrintable(label+" changes program fingerprint"));
        check(stableId(changed[0].selectionEvidence)!=stableId(referenceOperations[0].selectionEvidence),qPrintable(label+" changes stable selection ID"));
    }
    // Missing, null and incorrectly typed numeric evidence must remain unknown.
    for (const bool curveField : {false, true}) {
        const QStringList fields=curveField ? QStringList{"firstParameter","lastParameter","approximationBound"}
            : QStringList{"sourceBoundaryBound","consumedBoundaryBound","volumeProofBudget","maximumEscapeBudget"};
        for (const auto &field : fields) for (int invalid=0;invalid<3;++invalid) {
            auto changedRoot=root;auto changedOperations=operations;auto changedOperation=saved;
            auto changedSelection=selection;auto changedLoops=loops;auto changedLoop=loops[0].toObject();
            auto changedCurves=changedLoop["curves"].toArray();
            auto target=curveField ? changedCurves[0].toObject() : changedLoop;
            if(invalid==0)target.remove(field);else target[field]=invalid==1 ? QJsonValue(QJsonValue::Null) : QJsonValue("0");
            if(curveField){changedCurves[0]=target;changedLoop["curves"]=changedCurves;}else changedLoop=target;
            changedLoops[0]=changedLoop;changedSelection["boundaryLoops"]=changedLoops;
            changedOperation["selectionEvidence"]=changedSelection;changedOperations[0]=changedOperation;changedRoot["operations"]=changedOperations;
            QFile changedFile(path);check(changedFile.open(QIODevice::WriteOnly|QIODevice::Truncate),"open invalid evidence project");
            changedFile.write(QJsonDocument(changedRoot).toJson());changedFile.close();
            ProjectManager changed;check(changed.loadFromFile(path),"load invalid evidence for inspection");
            const auto changedOps=changed.operations();const QStringList changedIds{operation.id};
            const auto changedStatus=ProgramSnapshotStatus::evaluate(changedOps,changedIds,ProgramSnapshotFingerprint::calculate(changedOps,changedIds),"setup","setup","tools","tools",true);
            check(changedStatus.state==ProgramSnapshotState::Changed,qPrintable(QString("invalid numeric evidence %1 mode %2 is stale").arg(field).arg(invalid)));
        }
    }
    ProjectManager legacyProject;check(legacyProject.loadFromFile(QString::fromLocal8Bit(argv[1])),"load actual prior rounded-opening project");
    const auto legacy=legacyProject.operations();QStringList ids;
    check(legacy.size()==1,"legacy fixture has one actual confirmed operation");
    for(const auto &op:legacy) {
        ids.append(op.id);
        check(op.toolpathState==ToolpathState::Valid,"legacy fixture retains saved Valid state");
        check(op.selectionEvidence.boundaryLoops.size()==1 && selectionContainsCompleteOpening(op.selectionEvidence,op.selectionEvidence.boundaryLoops.first()),"legacy fixture has complete original selection evidence");
    }
    const auto fingerprint=ProgramSnapshotFingerprint::calculate(legacy,ids);
    const auto status=ProgramSnapshotStatus::evaluate(legacy,ids,fingerprint,"setup","setup","tools","tools",true);
    std::cout<<"LEGACY_STATUS_FRESH="<<(status.state==ProgramSnapshotState::Fresh)<<std::endl;
    check(status.state==ProgramSnapshotState::Changed,"legacy opening evidence stays stale even with matching fingerprint and Valid state");
    std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
