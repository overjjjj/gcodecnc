#include "OutputWorkflowTestSupport.h"
#include "../src/app/MainWindow.h"
#include "../src/app/AppController.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/process_design/ui/SetupOriginDialog.h"
#include "../src/workspaces/process_design/ui/StockDefinitionDialog.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/workspaces/program_verification/safety/GCodeSafetyValidator.h"
#include "../src/tool/ToolLibrary.h"
#include <QApplication>
#include <QTimer>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QToolButton>
#include <QListWidget>
#include <QLabel>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <iostream>
#include "../src/gcode/GCodeModalOptimizer.h"
#include "../src/workspaces/program_verification/generation/RegionToolpathValidation.h"

#include "../src/workspaces/process_design/selection/PocketOpening.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <STEPControl_Writer.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>

class ExactFailureCapture : public QObject {
    bool eventFilter(QObject *object,QEvent *event) override {
        if(event->type()==QEvent::Polish)if(auto *box=qobject_cast<QMessageBox *>(object))
            QTimer::singleShot(0,box,[box]{
                if(!box->detailedText().isEmpty())std::cerr<<"DETAIL "<<box->detailedText().toStdString()<<std::endl;
            });
        return false;
    }
};

static bool editParameter(QObject *window,const QString &id,const QString &key,double value) {
    auto *panel=window->findChild<OperationListPanel *>();if(!panel)return false;
    panel->selectOperationById(id);auto *tree=panel->findChild<QTreeWidget *>("operationTree");
    if(!tree || !tree->currentItem())return false;
    emit tree->itemDoubleClicked(tree->currentItem(),0);
    auto *dialog=window->findChild<OperationEditDialog *>("operationEditDialog");
    if(!dialog || !dialog->isVisible())return false;
    auto *field=dialog->findChild<QDoubleSpinBox *>("operationParameter_"+key);
    auto *buttons=dialog->findChild<QDialogButtonBox *>();if(!field || !buttons)return false;
    field->setValue(value);buttons->button(QDialogButtonBox::Ok)->click();
    return dialog->result()==QDialog::Accepted;
}
static void settle() { QEventLoop loop; QTimer::singleShot(200,&loop,&QEventLoop::quit); loop.exec(); }

// Deliberately bounded test reader for the exported fixture's Macro B package.
// It reads the disk files, never the in-memory parametric program.
static QString expandExport(const QString &directory)
{
    auto read=[&](const QString &name) {
        QFile file(directory+"/"+name);
        if(!file.open(QIODevice::ReadOnly))return QString();
        return QString::fromUtf8(file.readAll()).replace("\r\n","\n");
    };
    const auto main=read("CQ8_MAIN.NC"),library=read("CQ8_MACROS.NC");
    if(main.isEmpty() || library.isEmpty())return {};
    const QRegularExpression label("^O([0-9]+)$"),call("^M98 P([0-9]+)$"),variable("#([0-9]+)");
    const QRegularExpression assignment("^#(1[0-9]{2})=([-+]?[0-9]+(?:\\.[0-9]+)?)$");
    QHash<int,QStringList> routines;
    int active=-1;
    for(const auto &raw:library.split('\n')) {
        const auto line=raw.trimmed();if(line.isEmpty())continue;
        const auto id=label.match(line);
        if(id.hasMatch()) {
            if(active>=0 || routines.contains(id.captured(1).toInt()))return {};
            active=id.captured(1).toInt();routines.insert(active,{});continue;
        }
        if(active<0)return {};
        if(line=="M99") {active=-1;continue;}
        if(line.startsWith("; CNEXT ROUTINE: "))continue; // Definition header, not an executable block.
        if(line.contains("M98") || line.contains("M99") || line.contains('='))return {};
        routines[active].append(line);
    }
    if(active>=0 || routines.isEmpty())return {};
    QHash<int,QString> values;QStringList expanded;
    for(const auto &raw:main.split('\n')) {
        const auto line=raw.trimmed();const auto set=assignment.match(line),invoke=call.match(line);
        if(set.hasMatch()){values.insert(set.captured(1).toInt(),set.captured(2));continue;}
        if(invoke.hasMatch()) {
            const int id=invoke.captured(1).toInt();if(!routines.contains(id))return {};
            for(QString block:routines[id]) {
                auto match=variable.match(block);
                while(match.hasMatch()) {
                    const int index=match.captured(1).toInt();if(!values.contains(index))return {};
                    block.replace(match.capturedStart(),match.capturedLength(),values[index]);
                    match=variable.match(block);
                }
                expanded.append(block);
            }
        } else {
            if(line.contains('#') || line.contains("M98") || line.contains("M99"))return {};
            expanded.append(line);
        }
    }
    return GCodeModalOptimizer::optimize(expanded.join('\n'));
}

struct ExportBounds {
    double minX=1e30,maxX=-1e30,minY=1e30,maxY=-1e30,minZ=1e30;
    int cuts=0,retracts=0,rapidXY=0;
    double lastZ=0;
};
static bool verifyExportedChain(const QString &directory,const ProgramEntry &program,
                                const QList<MachiningOperation> &operations,const MeshData &mesh,
                                bool edited,QVector<ExportBounds> *bounds)
{
    const QString disk=expandExport(directory);
    if(disk.isEmpty() || operations.size()!=2)return false;
    // Only the generated timestamp header is excluded; operation and SAFE comments remain.
    auto body=[](QString code) {
        code.replace("\r\n","\n");auto lines=code.trimmed().split('\n');
        if(!lines.isEmpty() && lines.first().startsWith("( Generated by CNEXT-CAM "))lines.removeFirst();
        return lines.join('\n');
    };
    QFile evidence(directory+"/expanded-from-disk.nc");
    if(!evidence.open(QIODevice::WriteOnly) || evidence.write(disk.toUtf8())<0)return false;
    if(body(disk)!=body(program.expandedGcodeText)) {
        std::cerr<<"FAIL exported expansion differs from snapshot text\n";return false;
    }
    SimulationController verifier;verifier.setMesh(mesh);
    verifier.loadGCode(disk,true,0,program.modelOrigin);
    if(!GCodeSafetyValidator::validate(disk).ok || !verifier.motionError().isEmpty() ||
       verifier.checkResult().blocksOutput())return false;
    const QRegularExpression marker("\\[op:([^\\]]+)\\]");
    QVector<int> owners;int owner=-1;
    for(const auto &line:disk.split('\n')) {
        const auto match=marker.match(line);
        if(match.hasMatch()) {
            owner=-1;for(int i=0;i<2;++i)if(operations[i].id==match.captured(1))owner=i;
            if(owner<0)return false;
        }
        owners.append(owner);
    }
    *bounds=QVector<ExportBounds>(2);
    for(const auto &move:verifier.moves()) {
        const int index=owners.value(move.sourceLine,-1);if(index<0)return false;
        const auto &operation=operations[index];auto &box=(*bounds)[index];
        auto modelMove=move;modelMove.from+=program.modelOrigin;modelMove.to+=program.modelOrigin;
        if(!regionMoveError(modelMove,operation.contourFeature,operation.params,move.toolDiameter).isEmpty())return false;
        box.lastZ=move.to.z();
        if(move.isRapid) {
            if(move.fromKnownAxes==7 && move.toKnownAxes==7) {
                const bool lateral=move.from.x()!=move.to.x() || move.from.y()!=move.to.y();
                const double feed=operation.contourFeature.center.z()+operation.params.get("feedHeight")-program.modelOrigin.z();
                if(lateral) {
                    ++box.rapidXY;
                    if(std::min(move.from.z(),move.to.z())<feed-1e-6)return false;
                } else if(move.to.z()>move.from.z() && modelMove.from.z()<=operation.contourFeature.center.z()) {
                    ++box.retracts;
                    if(move.to.z()<operation.params.get("safeHeight")-1e-6)return false;
                }
            }
            continue;
        }
        if(move.fromKnownAxes!=7 || move.toKnownAxes!=7)return false;
        ++box.cuts;
        for(const auto &point:{move.from,move.to}) {
            box.minX=std::min(box.minX,point.x());box.maxX=std::max(box.maxX,point.x());
            box.minY=std::min(box.minY,point.y());box.maxY=std::max(box.maxY,point.y());
            box.minZ=std::min(box.minZ,point.z());
        }
    }
    for(int i=0;i<2;++i) {
        const auto &box=(*bounds)[i];const auto &operation=operations[i];
        const double target=operation.contourFeature.center.z()-program.modelOrigin.z()-operation.params.get("depth")+operation.params.get("floorStockToLeave");
        double expected=QString::number(target,'f',3).toDouble();if(expected<target)expected+=.001;
        std::cout<<"EXPORTED_REAL_SLOT "<<operation.strategyId.toStdString()<<" cuts="<<box.cuts<<" bottom="<<box.minZ<<" expected="<<expected<<std::endl;
        if(!box.cuts || !box.retracts || std::abs(box.minZ-expected)>1e-6 || box.lastZ<operation.params.get("safeHeight")-1e-6)return false;
    }
    return true;
}
int main(int argc,char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    QElapsedTimer elapsed;elapsed.start();
    auto phase=[&](const char *name){std::cout<<"PHASE "<<name<<" elapsed_ms="<<elapsed.elapsed()<<std::endl;};
    QTemporaryDir settings;isolateOutputWorkflowSettings(settings.path());
    OutputWorkflowDialogs dialogs;app.installEventFilter(&dialogs);
    ExactFailureCapture exactFailure;app.installEventFilter(&exactFailure);
    QTimer::singleShot(21600000,&app,[]{std::cerr<<"FAIL workflow timeout"<<std::endl;std::_Exit(99);});
    if(argc<3) return 1;
    const QString targetEdge=QString::fromLatin1(argv[2]);
    const QString out=QString::fromLocal8Bit(argv[1]);
    const QString strategy="mill_pocket_rough";
    const QString model=QString::fromUtf8("D:/codex/home/worktrees/5a59/gcodecnc/untitled/测试模型/验收/特征板.STEP");
    const bool rounded=false;
    if(QFileInfo::exists(out))return 90;
    QDir().mkpath(out);dialogs.evidenceDirectory=out;
    QFile source(model); if(!source.open(QIODevice::ReadOnly)) return 2;
    std::cout << "MODEL " << model.toStdString() << " SHA256 "
              << QCryptographicHash::hash(source.readAll(),QCryptographicHash::Sha256).toHex().toStdString() << '\n';
    ToolEntry tool; tool.id=960; tool.type="end_mill"; tool.name="Slot acceptance D6";
    tool.diameter=6;tool.fluteLen=60;tool.totalLen=100;
    MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1600,980);window.show();
    ToolLibrary::instance().removeTool(tool.id);if(!ToolLibrary::instance().addTool(tool))return 91;
    auto &controller=AppController::instance(); controller.importStep(model); settle();
    auto *project=controller.projectManager(); if(project->mesh().isEmpty()) return 3;
    QToolButton *setup=nullptr,*slot=nullptr;
    for(auto *b:window.findChildren<QToolButton *>()) {
        if(b->text()==QStringLiteral("加工设置")) setup=b;
        const QString command=strategy=="mill_pocket_rough"?QStringLiteral("挖槽"):
            strategy=="mill_pocket_finish"?QStringLiteral("侧壁精加工"):QStringLiteral("底面精加工");
        if(b->text()==command) slot=b;
    }
    if(!setup || !slot) return 4;
    setup->click(); settle();
    auto *overview=window.findChild<QDialog *>("setupOverviewDialog");
    overview->findChild<QPushButton *>("setupDirectionAction")->click();
    ViewportWidget *viewport=nullptr;
    for(auto *v:window.findChildren<ViewportWidget *>()) if(v->isVisible()) viewport=v;
    int face=-1;QVector3D point;QString specification;
    for(const auto &loop:project->mesh().boundaryLoops) {
        if(loop.faceId!="face:113" || loop.orderedEdgeIds.first()!=targetEdge)continue;
        SelectionBoundaryLoop proof;QString error;
        if(!verifyPocketOpening(project->mesh(),loop,&proof,nullptr,&error)) {
            std::cout<<"OPENING_REJECT "<<loop.faceId.toStdString()<<" "<<error.toStdString()<<'\n';continue;
        }
        face=loop.faceIndex;specification="topology:"+loop.faceId+"@"+loop.orderedEdgeIds.first();
        for(const auto &triangle:project->mesh().triangles)if(triangle.faceIndex==face){point=triangle.v0;break;}
        break;
    }
    if(!viewport || face<=0) return 5;
    emit viewport->facePointPicked(face,point); emit viewport->facePointPicked(face,point); settle();
    QTimer::singleShot(0,[&] {
        for(auto *d:window.findChildren<SetupOriginDialog *>()) if(d->isVisible()) {d->setWorkOffset("G54"); static_cast<QDialog *>(d)->accept();}
    });
    QMetaObject::invokeMethod(&window,"onEditSetupOrigin",Qt::DirectConnection);
    QTimer::singleShot(0,[&] {
        for(auto *d:window.findChildren<StockDefinitionDialog *>()) if(d->isVisible()) d->accept();
    });
    QMetaObject::invokeMethod(&window,"onEditStockDefinition",Qt::DirectConnection);
    if(!project->setupOrigin().confirmed || !project->stockDefinition().confirmed) return 6;
    std::cout << "SETUP confirmed G54\n";
    for(const auto &f:project->features()) if(f.kind==FeatureKind::Slot)
        std::cout<<"RECOGNIZED "<<f.subType.toStdString()<<" L="<<f.length<<" W="<<f.width<<" D="<<f.depth
                 <<" axis="<<f.axis.x()<<','<<f.axis.y()<<','<<f.axis.z()<<" region="<<int(f.region)<<'\n';
    const QStringList strategies={"mill_pocket_rough","mill_pocket_finish"};
    QList<MachiningOperation> expectedOperations;
    for (const auto &strategy : strategies) {
    const QString label=strategy=="mill_pocket_rough"?QStringLiteral("挖槽"):
        strategy=="mill_pocket_finish"?QStringLiteral("侧壁精加工"):QStringLiteral("底面精加工");
    slot=nullptr;
    for(auto *button:window.findChildren<QToolButton *>())if(button->text()==label)slot=button;
    if(!slot)return 42;
    dialogs.confirmedEntries=0;
    slot->click(); settle();
    SelectionChainDialog *chain=nullptr;
    for(auto *d:window.findChildren<SelectionChainDialog *>()) if(d->isVisible()) chain=d;
    if(!chain) {std::cerr<<"BLOCKED no slot selection dialog\n";return 7;}
    auto *list=chain->findChild<QListWidget *>("selectionCandidates");
    if(!list || !list->count()) {std::cerr<<"BLOCKED no real slot candidate\n";return 8;}
    for(int i=0;i<list->count();++i) std::cout<<"CANDIDATE "<<i<<' '<<list->item(i)->text().toStdString()<<'\n';
    const QString requested=specification.section('@',0,0),requestedEdge=specification.section('@',1,1);
    bool numeric=false;const int requestedRow=requested.toInt(&numeric);
    if(numeric)list->setCurrentRow(requestedRow);
    else {
        bool found=false;
        for(int row=0;row<list->count();++row) {
            list->setCurrentRow(row);settle();
            if(chain->selectedCandidateGeometryId()==requested){found=true;break;}
        }
        if(!found)return 94;
    }
    settle();
    std::cout<<"SELECTED_GEOMETRY "<<chain->selectedCandidateGeometryId().toStdString()<<std::endl;
    if(!requestedEdge.isEmpty()) {
        bool found=false;
        for(const auto &edge:project->mesh().pickEdges)if(edge.id==requestedEdge && !edge.points.isEmpty()) {
            chain->selectTopology(edge.id,1,edge.points.first(),edge.parameters.isEmpty()?0:edge.parameters.first());
            found=true;break;
        }
        if(!found)return 95;
        settle();
    }
    auto *fullBoundary=chain->findChild<QPushButton *>("selectionUseFullBoundary");
    if(!fullBoundary || !fullBoundary->isVisible() || !fullBoundary->isEnabled()) {
        chain->grab().save(out+"/boundary-unavailable.png");
        auto *status=chain->findChild<QLabel *>("selectionStatus");
        std::cerr<<"BLOCKED full boundary: "<<(status?status->text().toStdString():"no status")<<std::endl;
        const QString faceId=chain->selectedCandidateGeometryId().mid(QStringLiteral("topology:").size());
        for(const auto &loop:project->mesh().boundaryLoops)if(loop.faceId==faceId)
            std::cout<<"BOUNDARY_LOOP "<<loop.faceId.toStdString()<<" outer="<<loop.outer<<" edges="<<loop.orderedEdgeIds.join(",").toStdString()<<std::endl;
        return 92;
    }
    fullBoundary->click();settle();
    auto *next=chain->findChild<QPushButton *>("selectionNext");
    if(!next || !next->isVisible() || !next->isEnabled()) return 93;
    chain->grab().save(out+"/selection.png"); next->click(); settle();
    const auto selected=chain->selection();
    if(selected.boundaryLoops.size()!=1)return 98;
    if(chain->selectedPoints()!=selected.boundaryLoops.first().machiningBoundary)return 99;

    auto *panel=window.findChild<StrategyPanel *>();
    if(!panel || !panel->isVisible()) {
        chain->grab().save(out+"/selection-rejected.png");
        auto *status=chain->findChild<QLabel *>("selectionStatus");
        std::cerr<<"BLOCKED selection not accepted: "<<(status?status->text().toStdString():"no status")<<std::endl;return 9;
    }
    // Use an actual selectable catalogue entry, keeping recognized geometry and strategy untouched.
    auto draft=panel->parameterDraft();
    draft.set("depth",50.0);
    draft.set("stepDown",10.0);
    draft.set("sideStockToLeave",strategy=="mill_pocket_rough"?.3:.1);
    draft.set("floorStockToLeave",0);
    if(!requestedEdge.isEmpty()) {
        draft.set("entryMode",0);
        dialogs.confirmCenterCuttingFixture=true;
    }
    const double feedHeight=std::max(3.0,3.0-double(project->mesh().bbMin.z()));
    const double safeHeight=std::max(50.0,double(project->mesh().bbMax.z())+feedHeight+5.0);
    draft.set("feedHeight",feedHeight);draft.set("safeHeight",safeHeight);
    std::cout<<"PARAMETERS feedHeight="<<feedHeight<<" safeHeight="<<safeHeight<<'\n';
    panel->restoreDraft(draft,tool.id);
    panel->window()->grab().save(out+"/parameters.png");
    if(project->operations().size()!=expectedOperations.size()) return 10;
    QObject::connect(panel,&StrategyPanel::operationProposalConfirmed,[&](const OperationProposal &proposal) {
        const auto &f=proposal.contourFeature;
        std::cout<<"PROPOSAL "<<proposal.strategyId.toStdString()<<" axis="<<f.axis.x()<<','<<f.axis.y()<<','<<f.axis.z()
                 <<" L="<<f.length<<" W="<<f.width<<" D="<<f.depth<<'\n';
    });
    QMetaObject::invokeMethod(panel,"onConfirmOperation",Qt::DirectConnection); settle();
    if(project->operations().size()!=expectedOperations.size()+1) {std::cerr<<"BLOCKED operation confirmation\n";return 11;}
    const auto op=project->operations().last();
    if(!requestedEdge.isEmpty()) {
        QStringList actualEdges;
        for(const auto &ref:op.selectionEvidence.orderedTopologyRefs)actualEdges<<ref.id;
        std::cout<<"CONFIRMED_BOUNDARY "<<actualEdges.join(",").toStdString()<<std::endl;
        if(!actualEdges.contains(requestedEdge))return 96;
        if(requested=="topology:face:113" && requestedEdge=="edge:460" && actualEdges.size()!=18)return 97;
    }
    std::cout<<"CONFIRMED "<<op.id.toStdString()<<' '<<op.strategyId.toStdString()<<" L="<<op.contourFeature.length
             <<" W="<<op.contourFeature.width<<" D="<<op.contourFeature.depth<<" evidence="<<op.selectionEvidence.explicitUserSelection<<'\n';
    if(op.strategyId!=strategy) {std::cerr<<"BLOCKED selected feature is not a slot\n";return 12;}
    expectedOperations.append(op);
    }
    QStringList expectedIds;
    for(const auto &operation:expectedOperations)expectedIds.append(operation.id);
    auto *tree=window.findChild<OperationListPanel *>();
    QMetaObject::invokeMethod(tree,"onGenerateProgram",Qt::DirectConnection); settle();
    if(project->programs().isEmpty()) {std::cerr<<"BLOCKED final program generation\n";return 13;}
    const auto program=project->programs().last();
    if(program.sourceOperationIds!=expectedIds)return 43;
    const QString code=program.gcodeText;
    window.grab().save(out+"/before-export.png");
    if(!dialogs.exportProgram(&window,program,out+"/initial-export"))return 14;
    QVector<ExportBounds> initialBounds,editedBounds;
    if(!verifyExportedChain(out+"/initial-export",program,project->operations(),project->mesh(),false,&initialBounds))return 45;
    if(!GCodeSafetyValidator::validate(code).ok) return 15;
    SimulationController simulation; QVector<QVector3D> path; QVector<bool> rapid;
    QObject::connect(&simulation,&SimulationController::toolPathReady,[&](const QVector<QVector3D>&p,const QVector<bool>&r){path=p;rapid=r;});
    simulation.loadGCode(program.expandedGcodeText.isEmpty()?code:program.expandedGcodeText,true,0,program.modelOrigin);
    int cuts=0;for(bool r:rapid) if(!r) ++cuts;
    window.grab().save(out+"/generated.png");
    if(!cuts || !simulation.motionError().isEmpty()) return 16;
    const QString projectPath=out+"/saved-project.json";
    if(!controller.saveProject(projectPath) || !controller.loadProject(projectPath))return 25;
    settle();project=controller.projectManager();
    if(project->mesh().sourceShape.IsNull() || project->operations().size()!=2)return 26;
    for(int i=0;i<2;++i) {
        const auto &actual=project->operations()[i], &expected=expectedOperations[i];
        if(actual.id!=expected.id || actual.strategyId!=strategies[i] ||
           actual.params.values!=expected.params.values || !(actual.selectionEvidence==expected.selectionEvidence))return 26;
    }
    const int loadedProgramCount=project->programs().size();
    const QString loadedProgramId=project->programs().last().id;
    if(!QMetaObject::invokeMethod(tree,"onGenerateProgram",Qt::DirectConnection))return 27;
    settle();
    if(project->programs().size()!=loadedProgramCount+1 || project->programs().last().id==loadedProgramId)return 28;
    QStringList original=code.split('\n'),reloaded=project->programs().last().gcodeText.split('\n');
    if(!original.isEmpty())original.removeFirst();if(!reloaded.isEmpty())reloaded.removeFirst();
    if(original!=reloaded)return 29;
    if(!dialogs.exportProgram(&window,project->programs().last(),out+"/reloaded-export"))return 30;
    QVector<ExportBounds> reloadedBounds;
    if(!verifyExportedChain(out+"/reloaded-export",project->programs().last(),project->operations(),project->mesh(),false,&reloadedBounds))return 48;
    const auto beforeEdit=project->programs().last();
    auto edit=[&](int index,const QString &key,double value) {
        const bool accepted=editParameter(&window,expectedIds[index],key,value);
        settle();
        std::cout<<"EDIT "<<index<<" "<<key.toStdString()<<" accepted="<<accepted<<std::endl;
        return accepted;
    };
    if(!edit(0,"depth",49) || !edit(1,"depth",49))return 32;
    for(const auto &operation:project->operations())if(operation.toolpathState!=ToolpathState::Stale || operation.params.get("depth")!=49)return 33;
    if(!dialogs.rejectExport(&window,project,beforeEdit,out+"/stale-export"))return 34;
    phase("recalculate edited operations begin");
    for(const auto &id:expectedIds) {
        tree->selectOperationById(id);
        if(!QMetaObject::invokeMethod(tree,"onRecalculate",Qt::DirectConnection))return 35;
        settle();
    }
    for(const auto &operation:project->operations())if(operation.toolpathState!=ToolpathState::Valid)return 36;
    phase("edited program generation begin");
    const int beforeEditCount=project->programs().size();
    if(!QMetaObject::invokeMethod(tree,"onGenerateProgram",Qt::DirectConnection))return 37;
    settle();
    if(project->programs().size()!=beforeEditCount+1 || project->programs().last().id==beforeEdit.id
        || project->programs().last().gcodeText==beforeEdit.gcodeText)return 38;
    if(!dialogs.exportProgram(&window,project->programs().last(),out+"/edited-export"))return 39;
    phase("edited export completed");
    const auto edited=project->programs().last();
    if(!verifyExportedChain(out+"/edited-export",edited,project->operations(),project->mesh(),true,&editedBounds))return 46;
    for(int i=0;i<2;++i)if(std::abs(editedBounds[i].minZ-initialBounds[i].minZ-1)>1e-6)return 47;
    const auto finalOperation=project->operations().last();
    if(edited.sourceOperationIds!=expectedIds)return 44;
    const double target=finalOperation.contourFeature.center.z()-edited.modelOrigin.z()-finalOperation.params.get("depth")+finalOperation.params.get("floorStockToLeave",0);
    double outputZ=QString::number(target,'f',3).toDouble();if(outputZ<target)outputZ+=.001;
    const double editedBottom=outputZ+edited.modelOrigin.z();
    if(!verifyOutputDepth(edited,out+"/edited-export",editedBottom))return 41;
    if(dialogs.warnings.size()!=dialogs.expectedWarnings)return 40;
    std::cout<<"PASS real selected feature-board through-depth 50 mm rough -> wall (not sloped floor), ordered CQ8, source evidence save/reload, depth edits, stale refusal, recalculation and actual exports; nominal-path simulation only cuts="<<cuts<<'\n';
    return 0;
}
