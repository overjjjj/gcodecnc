#include "OutputWorkflowTestSupport.h"
#include "src/app/MainWindow.h"
#include "src/app/AppController.h"
#include "src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "src/workspaces/process_design/ui/StrategyPanel.h"
#include "src/workspaces/process_design/ui/SetupOriginDialog.h"
#include "src/workspaces/process_design/ui/StockDefinitionDialog.h"
#include "src/workspaces/process_design/ui/OperationListPanel.h"
#include "src/workspaces/shared/ui/ViewportWidget.h"
#include "src/workspaces/program_verification/simulation/SimulationController.h"
#include "src/workspaces/program_verification/safety/GCodeSafetyValidator.h"
#include "src/tool/ToolLibrary.h"
#include <QApplication>
#include <QTimer>
#include <QEventLoop>
#include <QToolButton>
#include <QListWidget>
#include <QLabel>
#include <QComboBox>
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
        if(b->text()==QStringLiteral("底面精加工")) slot=b;
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
        const int requestedFace=113;
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
    auto *selectionMode=chain->findChild<QComboBox *>("selectionMode");
    if(!selectionMode)return 92;
    selectionMode->setCurrentIndex(static_cast<int>(ChainSelectionMode::Edge));
    QMetaObject::invokeMethod(selectionMode,"activated",Qt::DirectConnection,
        Q_ARG(int,static_cast<int>(ChainSelectionMode::Edge)));
    if(!requestedEdge.isEmpty()) {
        bool found=false;
        for(const auto &edge:project->mesh().pickEdges)if(edge.id==requestedEdge && !edge.points.isEmpty()) {
            chain->selectTopology(edge.id,1,edge.points.first(),edge.parameters.isEmpty()?0:edge.parameters.first());
            found=true;break;
        }
        if(!found)return 95;
        settle();
    }
    for(const QString &id:QStringList{"edge:899","edge:470","edge:900"}) {
        bool found=false;
        for(const auto &edge:project->mesh().pickEdges)if(edge.id==id && !edge.points.isEmpty()) {
            chain->selectTopology(id,1,edge.points.first(),edge.parameters.isEmpty()?0:edge.parameters.first());
            settle();found=true;break;
        }
        if(!found)return 96;
    }
    auto *next=chain->findChild<QPushButton *>("selectionNext");
    if(!next)return 93;
    std::cout<<"REJECTION "<<next->toolTip().toStdString()<<std::endl;
    QString visible;
    for(auto *label:chain->findChildren<QLabel *>())if(label->isVisible())visible+=label->text()+"\n";
    std::cout<<"VISIBLE "<<visible.toStdString()<<std::endl;
    chain->grab().save(out+"/selection.png");
    auto *chainStatus=chain->findChild<QLabel *>("selectionChainStatus");
    auto *selectionStatus=chain->findChild<QLabel *>("selectionStatus");
    const bool target=chain->selectedCandidateGeometryId()=="topology:face:366" &&
        chainStatus && chainStatus->text().contains(QStringLiteral("闭合 · 已选 4 段")) &&
        selectionStatus && selectionStatus->text().contains(QStringLiteral("edge:881 → edge:899 → edge:470 → edge:900")) &&
        next->toolTip().contains("REGION_INNER_WIRE_REQUIRED");
    std::cout<<(target?"PASS":"FAIL")<<" exact screenshot face, four-edge chain and rejection"<<std::endl;
    const bool safe=!next->isEnabled() && project->operations().isEmpty();
    const bool explained=chainStatus && chainStatus->toolTip().contains("REGION_INNER_WIRE_REQUIRED") &&
        visible.contains(QStringLiteral("请选择槽口的完整闭合边界")) &&
        !visible.contains("REGION_INNER_WIRE_REQUIRED") && !visible.contains("Setup");
    std::cout<<(safe?"PASS":"FAIL")<<" unsupported source remains blocked"<<std::endl;
    std::cout<<(explained?"PASS":"FAIL")<<" Chinese body excludes internal code; original diagnostic remains available"<<std::endl;
    auto *validation=chain->findChild<QLabel *>("selectionValidationStatus");
    const bool separated=validation && validation->text().contains(QStringLiteral("底面：选区检查未通过")) &&
        validation->text().contains(QStringLiteral("粗加工：未检查")) &&
        validation->text().contains(QStringLiteral("侧壁：未检查")) &&
        validation->text().contains(QStringLiteral("最终 NC：未检查"));
    std::cout<<(separated?"PASS":"FAIL")<<" floor rejection does not reject unchecked rough/wall operations"<<std::endl;
    if(!target || !safe || !explained || !separated)return 1;
    chain->reject();settle();
    QToolButton *rough=nullptr;
    for(auto *button:window.findChildren<QToolButton *>())if(button->text()==QStringLiteral("挖槽"))rough=button;
    if(!rough)return 97;
    rough->click();settle();chain=nullptr;
    for(auto *dialog:window.findChildren<SelectionChainDialog *>())if(dialog->isVisible())chain=dialog;
    if(!chain)return 98;
    chain->selectGeometry("topology:face:113");
    for(const auto &edge:project->mesh().pickEdges)if(edge.id=="edge:452") {
        chain->selectTopology(edge.id,1,edge.points.first(),edge.parameters.first());break;
    }
    settle();
    auto *full=chain->findChild<QPushButton *>("selectionUseFullBoundary");
    if(full && full->isVisible())full->click();
    else {
        for(const QString &id:QStringList{"edge:453","edge:454","edge:455","edge:456","edge:457","edge:458","edge:459"})
            for(const auto &edge:project->mesh().pickEdges)if(edge.id==id) {
                chain->selectTopology(id,1,edge.points.first(),edge.parameters.first());break;
            }
        auto *finish=chain->findChild<QPushButton *>("selectionFinishChain");
        if(finish && finish->isVisible())finish->click();
    }
    settle();next=chain->findChild<QPushButton *>("selectionNext");
    chainStatus=chain->findChild<QLabel *>("selectionChainStatus");
    const bool recovered=next && next->isEnabled() && chainStatus &&
        !chainStatus->text().contains("REGION_INNER_WIRE_REQUIRED") && chainStatus->toolTip().isEmpty();
    std::cout<<(recovered?"PASS":"FAIL")<<" real valid slot opening remains available and rejection clears"<<std::endl;
    chain->grab().save(out+"/recovered.png");
    return recovered?0:1;
}
