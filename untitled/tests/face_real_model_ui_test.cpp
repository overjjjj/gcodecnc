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
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <iostream>
#include <algorithm>
#include <cmath>

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
    ToolEntry tool; tool.id=960; tool.type="end_mill"; tool.name="Face acceptance D10";
    tool.diameter=10;tool.fluteLen=60;tool.totalLen=100;
    MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1600,980);window.show();
    ToolLibrary::instance().removeTool(tool.id);if(!ToolLibrary::instance().addTool(tool))return 91;
    auto &controller=AppController::instance(); controller.importStep(model); settle();
    auto *project=controller.projectManager(); if(project->mesh().isEmpty()) return 3;
    QToolButton *setup=nullptr,*slot=nullptr;
    for(auto *b:window.findChildren<QToolButton *>()) {
        if(b->text()==QStringLiteral("加工设置")) setup=b;
        if(b->text()==QStringLiteral("铣平面")) slot=b;
    }
    if(!setup || !slot) return 4;
    setup->click(); settle();
    auto *overview=window.findChild<QDialog *>("setupOverviewDialog");
    overview->findChild<QPushButton *>("setupDirectionAction")->click();
    ViewportWidget *viewport=nullptr;
    for(auto *v:window.findChildren<ViewportWidget *>()) if(v->isVisible()) viewport=v;
    int face=-1; QVector3D point;
    for(const auto &t:project->mesh().triangles) if(t.normal.z()>.99f) { face=t.faceIndex; point=t.v0; break; }
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
    for(const auto &f:project->features()) if(f.kind==FeatureKind::FlatSurface)
        std::cout<<"RECOGNIZED "<<f.subType.toStdString()<<" L="<<f.length<<" W="<<f.width<<" D="<<f.depth
                 <<" axis="<<f.axis.x()<<','<<f.axis.y()<<','<<f.axis.z()<<" region="<<int(f.region)<<'\n';
    slot->click(); settle();
    SelectionChainDialog *chain=nullptr;
    for(auto *d:window.findChildren<SelectionChainDialog *>()) if(d->isVisible()) chain=d;
    if(!chain) {std::cerr<<"BLOCKED no slot selection dialog\n";return 7;}
    auto *list=chain->findChild<QListWidget *>("selectionCandidates");
    if(!list || !list->count()) {std::cerr<<"BLOCKED no real slot candidate\n";return 8;}
    for(int i=0;i<list->count();++i) std::cout<<"CANDIDATE "<<i<<' '<<list->item(i)->text().toStdString()<<'\n';
    list->setCurrentRow(argc>3?QString::fromLocal8Bit(argv[3]).toInt():0); settle();
    auto *fullBoundary=chain->findChild<QPushButton *>("selectionUseFullBoundary");
    if(!fullBoundary || !fullBoundary->isVisible() || !fullBoundary->isEnabled()) return 92;
    fullBoundary->click();settle();
    auto *next=chain->findChild<QPushButton *>("selectionNext");
    if(!next || !next->isVisible() || !next->isEnabled()) return 93;
    chain->grab().save(out+"/selection.png"); next->click(); settle();
    auto *panel=window.findChild<StrategyPanel *>();
    if(!panel || !panel->isVisible()) {std::cerr<<"BLOCKED selection not accepted\n";return 9;}
    // Use an actual selectable catalogue entry, keeping recognized geometry and strategy untouched.
    if(argc>4) panel->selectStrategy(QString::fromLocal8Bit(argv[4]));
    auto draft=panel->parameterDraft();
    draft.set("depth",1);draft.set("stockToLeave",0.2);draft.set("stepover",5);
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
    std::cout<<"CONFIRMED "<<op.id.toStdString()<<' '<<op.strategyId.toStdString()<<" L="<<op.contourFeature.length
             <<" W="<<op.contourFeature.width<<" D="<<op.contourFeature.depth<<" evidence="<<op.selectionEvidence.explicitUserSelection<<'\n';
    if(op.strategyId!="mill_face" && op.strategyId!="mill_surface_finish") {std::cerr<<"BLOCKED selected feature is not a slot\n";return 12;}
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
    const auto &f=op.contourFeature;const double r=tool.diameter/2;
    const double left=f.center.x()-f.length/2,right=f.center.x()+f.length/2;
    const double low=f.center.y()-f.width/2,high=f.center.y()+f.width/2;
    const double bottom=std::round((f.center.z()-program.modelOrigin.z()-1+0.2)*1000)/1000+program.modelOrigin.z();
    const auto stock=project->stockDefinition().resolvedBounds(project->mesh().bbMin,project->mesh().bbMax);
    QVector<double> bands;int plunges=0;
    for(int i=0;i<rapid.size();++i) {
        if(rapid[i]) continue;
        const auto a=path[i],b=path[i+1];
        if(std::abs(a.z()-b.z())>0.0001) {
            ++plunges;
            const double dx=std::max({double(stock.minimum.x())-b.x(),0.0,double(b.x())-stock.maximum.x()});
            const double dy=std::max({double(stock.minimum.y())-b.y(),0.0,double(b.y())-stock.maximum.y()});
            if(std::hypot(dx,dy)<=r || std::abs(a.x()-b.x())>0.0001 || std::abs(a.y()-b.y())>0.0001)
                {std::cerr<<"plunge disk intersects stock or entry is not vertical\n";return 20;}
        }
        if(std::abs(b.z()-bottom)>0.0001){std::cerr<<"single-layer depth mismatch\n";return 21;}
        if(b.x()<left-r-0.001 || b.x()>right+r+0.001 || b.y()<low-r-0.001 || b.y()>high+r+0.001)
            {std::cerr<<"cutting segment exceeds authorized raster extension\n";return 22;}
        if(std::abs(a.y()-b.y())<0.0001 && std::abs(a.x()-b.x())>f.length) bands.append(b.y());
    }
    std::sort(bands.begin(),bands.end());
    if(plunges!=1 || bands.isEmpty() || bands.first()-r>low || bands.last()+r<high) return 23;
    for(int i=1;i<bands.size();++i) if(bands[i]-bands[i-1]>tool.diameter+0.00001) return 24;
    std::cout<<"VERIFIED final NC single layer Z="<<bottom<<" raster bands="<<bands.size()
             <<" full rectangle coverage, bounded extensions, quantized plunge disk outside stock\n";
    const QString projectPath=out+"/saved-project.json";
    if(!controller.saveProject(projectPath) || !controller.loadProject(projectPath)) return 25;
    settle();project=controller.projectManager();
    if(project->operations().size()!=1 || project->operations()[0].id!=op.id
        || !project->operations()[0].contourFeature.planarBoundaryVerified
        || project->operations()[0].contourFeature.points!=f.points
        || project->operations()[0].contourFeature.depth!=1 || project->operations()[0].params.get("depth")!=1) return 26;
    if(project->mesh().sourceShape.IsNull())return 29;
    const int loadedProgramCount=project->programs().size();
    const QString loadedProgramId=project->programs().last().id;
    if(!QMetaObject::invokeMethod(tree,"onGenerateProgram",Qt::DirectConnection))return 30;
    settle();
    if(project->programs().size()!=loadedProgramCount+1 || project->programs().last().id==loadedProgramId)return 31;
    QStringList first=code.split('\n'),second=project->programs().last().gcodeText.split('\n');
    if(!first.isEmpty())first.removeFirst();if(!second.isEmpty())second.removeFirst();
    if(first!=second){std::cerr<<"save/reopen changed generated semantics\n";return 27;}
    if(!dialogs.exportProgram(&window,project->programs().last(),out+"/reloaded-export"))return 28;
    std::cout<<"VERIFIED save/reopen original boundary, removal intent and final NC semantics\n";
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
    const double editedBottom=std::round((project->operations().first().contourFeature.center.z()-edited.modelOrigin.z()-0.6+0.2)*1000)/1000+edited.modelOrigin.z();
    if(!verifyOutputDepth(edited,out+"/edited-export",editedBottom))return 41;
    if(dialogs.warnings.size()!=dialogs.expectedWarnings)return 40;
    std::cout<<"PASS real STEP Setup, face selection, save/reload, edited depth, stale output refusal, recalculation and actual exports cuts="<<cuts<<'\n';
    return 0;
}
