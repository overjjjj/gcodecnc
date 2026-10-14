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
#include <QToolButton>
#include <QListWidget>
#include <QLabel>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <iostream>

static void settle() { QEventLoop loop; QTimer::singleShot(200,&loop,&QEventLoop::quit); loop.exec(); }
int main(int argc,char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    QTemporaryDir settings;isolateOutputWorkflowSettings(settings.path());
    OutputWorkflowDialogs dialogs;app.installEventFilter(&dialogs);
    QTimer::singleShot(240000,&app,[]{std::cerr<<"FAIL workflow timeout"<<std::endl;std::_Exit(99);});
    if(argc<3) return 1;
    const QString model=QString::fromLocal8Bit(argv[1]), out=QString::fromLocal8Bit(argv[2]);
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
        if(b->text()==QStringLiteral("挖槽")) slot=b;
    }
    if(!setup || !slot) return 4;
    setup->click(); settle();
    auto *overview=window.findChild<QDialog *>("setupOverviewDialog");
    overview->findChild<QPushButton *>("setupDirectionAction")->click();
    ViewportWidget *viewport=nullptr;
    for(auto *v:window.findChildren<ViewportWidget *>()) if(v->isVisible()) viewport=v;
    int face=-1; QVector3D point;
    for(const auto &t:project->mesh().triangles) if(t.normal.z()>.99f) { face=t.faceIndex; point=t.v0; break; }
    // Prefer an actual planar model face aligned with a recognized slot's axis, as an operator would.
    if(argc>4) {
        bool aligned=false;
        for(const auto &f:project->features()) {
            if(f.kind!=FeatureKind::Slot || f.axis.normalized().z()<0.65f) continue;
            for(const auto &t:project->mesh().triangles) {
                if((t.normal.normalized()-f.axis.normalized()).length()<1e-6f) {
                    face=t.faceIndex;point=t.v0;aligned=true;break;
                }
            }
            if(aligned) break;
        }
        if(!aligned) {std::cerr<<"BLOCKED no real face aligned to recognized slot axis\n";return 17;}
    }
    if(argc>3 && QString::fromLocal8Bit(argv[3]).startsWith("topology:face:")) {
        const int requestedFace=QString::fromLocal8Bit(argv[3]).section('@',0,0).section(':',2,2).toInt();
        face=-1;
        for(const auto &triangle:project->mesh().triangles)if(triangle.faceIndex==requestedFace) {
            face=triangle.faceIndex;point=triangle.v0;break;
        }
        std::cout<<"SETUP_REAL_FACE "<<face<<std::endl;
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
    slot->click(); settle();
    SelectionChainDialog *chain=nullptr;
    for(auto *d:window.findChildren<SelectionChainDialog *>()) if(d->isVisible()) chain=d;
    if(!chain) {std::cerr<<"BLOCKED no slot selection dialog\n";return 7;}
    auto *list=chain->findChild<QListWidget *>("selectionCandidates");
    if(!list || !list->count()) {std::cerr<<"BLOCKED no real slot candidate\n";return 8;}
    for(int i=0;i<list->count();++i) std::cout<<"CANDIDATE "<<i<<' '<<list->item(i)->text().toStdString()<<'\n';
    const QString specification=argc>3?QString::fromLocal8Bit(argv[3]):QStringLiteral("0");
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
    auto *panel=window.findChild<StrategyPanel *>();
    if(!panel || !panel->isVisible()) {
        chain->grab().save(out+"/selection-rejected.png");
        auto *status=chain->findChild<QLabel *>("selectionStatus");
        std::cerr<<"BLOCKED selection not accepted: "<<(status?status->text().toStdString():"no status")<<std::endl;return 9;
    }
    // Use an actual selectable catalogue entry, keeping recognized geometry and strategy untouched.
    auto draft=panel->parameterDraft();
    draft.set("depth",1.0);
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
    if(!project->operations().isEmpty()) return 10;
    QObject::connect(panel,&StrategyPanel::operationProposalConfirmed,[&](const OperationProposal &proposal) {
        const auto &f=proposal.contourFeature;
        std::cout<<"PROPOSAL "<<proposal.strategyId.toStdString()<<" axis="<<f.axis.x()<<','<<f.axis.y()<<','<<f.axis.z()
                 <<" L="<<f.length<<" W="<<f.width<<" D="<<f.depth<<'\n';
    });
    QMetaObject::invokeMethod(panel,"onConfirmOperation",Qt::DirectConnection); settle();
    if(project->operations().size()!=1) {std::cerr<<"BLOCKED operation confirmation\n";return 11;}
    const auto op=project->operations().first();
    if(!requestedEdge.isEmpty()) {
        QStringList actualEdges;
        for(const auto &ref:op.selectionEvidence.orderedTopologyRefs)actualEdges<<ref.id;
        std::cout<<"CONFIRMED_BOUNDARY "<<actualEdges.join(",").toStdString()<<std::endl;
        if(!actualEdges.contains(requestedEdge))return 96;
        if(requested=="topology:face:113" && requestedEdge=="edge:452" && actualEdges.size()!=8)return 97;
    }
    std::cout<<"CONFIRMED "<<op.id.toStdString()<<' '<<op.strategyId.toStdString()<<" L="<<op.contourFeature.length
             <<" W="<<op.contourFeature.width<<" D="<<op.contourFeature.depth<<" evidence="<<op.selectionEvidence.explicitUserSelection<<'\n';
    if(!op.strategyId.contains("slot") && op.strategyId!="mill_pocket_rough") {std::cerr<<"BLOCKED selected feature is not a slot\n";return 12;}
    auto *tree=window.findChild<OperationListPanel *>();
    QMetaObject::invokeMethod(tree,"onGenerateProgram",Qt::DirectConnection); settle();
    if(project->programs().isEmpty()) {std::cerr<<"BLOCKED final program generation\n";return 13;}
    const auto program=project->programs().last();
    const QString code=program.gcodeText;
    window.grab().save(out+"/before-export.png");
    if(!dialogs.exportProgram(&window,program,out+"/initial-export"))return 14;
    if(!GCodeSafetyValidator::validate(code).ok) return 15;
    SimulationController simulation; QVector<QVector3D> path; QVector<bool> rapid;
    QObject::connect(&simulation,&SimulationController::toolPathReady,[&](const QVector<QVector3D>&p,const QVector<bool>&r){path=p;rapid=r;});
    simulation.loadGCode(program.expandedGcodeText.isEmpty()?code:program.expandedGcodeText,true,0,program.modelOrigin);
    int cuts=0;for(bool r:rapid) if(!r) ++cuts;
    window.grab().save(out+"/generated.png");
    if(!cuts) return 16;
    const QString projectPath=out+"/saved-project.json";
    if(!controller.saveProject(projectPath) || !controller.loadProject(projectPath))return 25;
    settle();project=controller.projectManager();
    if(project->mesh().sourceShape.IsNull() || project->operations().size()!=1
        || project->operations().first().id!=op.id || project->operations().first().params.values!=op.params.values)return 26;
    const int loadedProgramCount=project->programs().size();
    const QString loadedProgramId=project->programs().last().id;
    if(!QMetaObject::invokeMethod(tree,"onGenerateProgram",Qt::DirectConnection))return 27;
    settle();
    if(project->programs().size()!=loadedProgramCount+1 || project->programs().last().id==loadedProgramId)return 28;
    QStringList original=code.split('\n'),reloaded=project->programs().last().gcodeText.split('\n');
    if(!original.isEmpty())original.removeFirst();if(!reloaded.isEmpty())reloaded.removeFirst();
    if(original!=reloaded)return 29;
    if(!dialogs.exportProgram(&window,project->programs().last(),out+"/reloaded-export"))return 30;
    const auto beforeEdit=project->programs().last();
    if(!editOutputWorkflowDepth(&window,op.id,0.6))return 32;
    settle();
    if(project->operations().first().toolpathState!=ToolpathState::Stale || project->operations().first().params.get("depth")!=0.6)return 33;
    if(!dialogs.rejectExport(&window,project,beforeEdit,out+"/stale-export"))return 34;
    if(!QMetaObject::invokeMethod(tree,"onRecalculate",Qt::DirectConnection))return 35;
    settle();
    if(project->operations().first().toolpathState!=ToolpathState::Valid)return 36;
    const int beforeEditCount=project->programs().size();
    if(!QMetaObject::invokeMethod(tree,"onGenerateProgram",Qt::DirectConnection))return 37;
    settle();
    if(project->programs().size()!=beforeEditCount+1 || project->programs().last().id==beforeEdit.id
        || project->programs().last().gcodeText==beforeEdit.gcodeText)return 38;
    if(!dialogs.exportProgram(&window,project->programs().last(),out+"/edited-export"))return 39;
    const auto edited=project->programs().last();
    const double editedBottom=std::round((project->operations().first().contourFeature.center.z()-edited.modelOrigin.z()-0.6)*1000)/1000+edited.modelOrigin.z();
    if(!verifyOutputDepth(edited,out+"/edited-export",editedBottom))return 41;
    if(dialogs.warnings.size()!=dialogs.expectedWarnings)return 40;
    std::cout<<"PASS real STEP Setup, slot selection, save/reload, edited depth, stale output refusal, recalculation and actual exports cuts="<<cuts<<'\n';
    return 0;
}
