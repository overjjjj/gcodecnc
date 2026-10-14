#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <QMessageBox>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QAction>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QTabWidget>
#include <iostream>
#include <cstdlib>
#include "../src/import/StepImporter.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <STEPControl_Writer.hxx>
#include "../src/workspaces/process_design/selection/CoplanarSelection.h"
#include <QListWidget>
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/process_design/ui/SelectionChainDialog.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/workspaces/process_design/ui/ContourMachiningChoiceDialog.h"
#include "../src/workspaces/process_design/ui/SetupOriginDialog.h"
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/workspaces/process_design/ui/OperationBatchEditDialog.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/core/Settings.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#include "../src/strategies/StrategyFactory.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/strategies/mill/PlanarMachiningValidation.h"

class HiddenMessages : public QObject {
    bool eventFilter(QObject *object, QEvent *event) override {
        if(event->type()==QEvent::Polish && qobject_cast<QDialog*>(object))
            static_cast<QWidget*>(object)->setAttribute(Qt::WA_DontShowOnScreen);
        return event->type() == QEvent::Show && qobject_cast<QMessageBox *>(object);
    }
};

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x << std::endl; std::_Exit(1); } } while(0)
int main(int argc, char **argv) {
    QApplication app(argc,argv);
    HiddenMessages messages;app.installEventFilter(&messages);
    bool stockEntryRejected=false;
    QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[&](){
        for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(widget)){
            stockEntryRejected|=box->text().contains(QStringLiteral("下刀圆盘"));
            std::cerr << box->text().toStdString() << std::endl;box->reject();
        }
    });dismiss.start(10);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
    TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
    builder.Add(compound,BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),40,20,10).Shape());
    builder.Add(compound,BRepPrimAPI_MakeBox(gp_Pnt(60,0,0),30,16,10).Shape());
    const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(140,10,0),gp_Dir(0,0,1)),3,10).Shape();
    builder.Add(compound,BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(120,0,0),40,20,10).Shape(),hole).Shape());
    STEPControl_Writer writer;writer.Transfer(compound,STEPControl_AsIs);
    const QString step=settings.filePath("same-plane-with-hole.step");
    CHECK(writer.Write(step.toLocal8Bit().constData())==IFSelect_RetDone);
    StepImporter importer;CHECK(importer.load(step));const auto mesh=importer.mesh();
    ToolEntry mill;mill.id=903;mill.type="end_mill";mill.diameter=4;mill.fluteLen=12;mill.totalLen=50;
    ToolLibrary::instance().addTool(mill);
    auto *window=new MainWindow;Settings::instance().setPostProcessorId("cq8");
    auto *project=AppController::instance().projectManager();project->setLoadedMesh(mesh);
    project->setFeatures(importer.features());project->setSourceFileFingerprint("coplanar-step");
    project->setActiveRegion(FaceRegion::Front);window->m_activeRegion=FaceRegion::Front;
    SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);
    StockDefinition stock;stock.confirmed=true;
    StockDefinition invalidStock=stock;invalidStock.minusY=20;project->setStockDefinition(invalidStock);
    window->setAttribute(Qt::WA_DontShowOnScreen);window->m_parameterDialog->setAttribute(Qt::WA_DontShowOnScreen);
    window->resize(1400,900);window->show();window->m_viewport->setMesh(mesh);
    window->beginOperationWorkflow("mill_face");
    auto *dialog=dynamic_cast<SelectionChainDialog*>(window->m_geometryDialog.data());CHECK(dialog);
    QString seed,unsupported;
    for(const auto &candidate:dialog->m_candidates){
        if(!candidate.geometryId.startsWith("topology:") || !candidate.sourcePlane.verified ||
           candidate.sourcePlane.normal.z()<.99 || std::abs(candidate.sourcePlane.offset)>1e-5)continue;
        if(candidate.boundaryLoops.size()>1)unsupported=candidate.geometryId;
        else if(seed.isEmpty())seed=candidate.geometryId;
    }
    CHECK(!seed.isEmpty()&&!unsupported.isEmpty());
    dialog->m_mode->setCurrentIndex(int(ChainSelectionMode::Face));
    QMetaObject::invokeMethod(dialog->m_mode,"activated",Q_ARG(int,int(ChainSelectionMode::Face)));
    dialog->selectGeometry(seed);dialog->selectTopology(seed.mid(9),0,{},0);
    CHECK(dialog->m_coplanar->isEnabled());dialog->m_coplanar->click();
    CHECK(dialog->m_regionSelections.size()==3);
    int unsupportedRow=-1;
    for(int i=0;i<dialog->m_regionSelections.size();++i){
        const auto &region=dialog->m_regionSelections.at(i);
        if(region.orderedTopologyRefs.first().id==unsupported.mid(9)){unsupportedRow=i;CHECK(region.boundaryLoops.size()==2);}
        else CHECK(region.boundaryLoops.size()==1);
    }
    CHECK(unsupportedRow>=0);
    dialog->accept();CHECK(dialog->result()!=QDialog::Accepted);CHECK(project->operations().isEmpty());
    CHECK(!dialog->m_status->text().isEmpty());
    if(argc>1){app.processEvents();CHECK(dialog->grab().save(QString::fromLocal8Bit(argv[1])+".unsupported.png"));}
    dialog->m_regions->setCurrentRow(unsupportedRow);dialog->m_removeRegion->click();
    CHECK(dialog->m_regionSelections.size()==2);
    if(argc>1){app.processEvents();CHECK(dialog->grab().save(QString::fromLocal8Bit(argv[1])+".png"));}
    dialog->accept();CHECK(window->m_workflowSelections.size()==2);CHECK(window->m_parameterDialog->isVisible());
    auto params=StrategyFactory::instance().strategy("mill_face")->defaultParams();
    params.set("depth",1);params.set("stepover",2);params.set("safeHeight",50);params.set("feedHeight",5);
    window->m_strategyPanel->restoreDraft(params,mill.id);window->m_strategyPanel->onConfirmOperation();
    CHECK(project->operations().isEmpty()&&stockEntryRejected);
    window->m_parameterDialog->reject();
    project->setStockDefinition(stock);
    window->beginOperationWorkflow("mill_face");
    dialog=dynamic_cast<SelectionChainDialog*>(window->m_geometryDialog.data());CHECK(dialog);
    dialog->m_mode->setCurrentIndex(int(ChainSelectionMode::Face));
    QMetaObject::invokeMethod(dialog->m_mode,"activated",Q_ARG(int,int(ChainSelectionMode::Face)));
    dialog->selectGeometry(seed);dialog->selectTopology(seed.mid(9),0,{},0);dialog->m_coplanar->click();
    CHECK(dialog->m_regionSelections.size()==3);
    for(int i=0;i<dialog->m_regionSelections.size();++i)
        if(dialog->m_regionSelections[i].orderedTopologyRefs.first().id==unsupported.mid(9)) {
            dialog->m_regions->setCurrentRow(i);dialog->m_removeRegion->click();break;
        }
    dialog->accept();CHECK(window->m_workflowSelections.size()==2);
    window->m_strategyPanel->restoreDraft(params,mill.id);window->m_strategyPanel->onConfirmOperation();
    CHECK(project->operations().size()==2);
    const auto operations=project->operations();
    const auto invalidBounds=invalidStock.resolvedBounds(mesh.bbMin,mesh.bbMax);
    CHECK(planarStockEntryError(operations[0].contourFeature,mill,params,invalidBounds).isEmpty());
    CHECK(!planarStockEntryError(operations[1].contourFeature,mill,params,invalidBounds).isEmpty());
    CHECK(!operations[0].selectionGroupId.isEmpty()&&operations[0].selectionGroupId==operations[1].selectionGroupId);
    CHECK(operations[0].selectionEvidence.orderedTopologyRefs!=operations[1].selectionEvidence.orderedTopologyRefs);
    CHECK(operations[0].contourFeature.points!=operations[1].contourFeature.points);
    CHECK(project->lastEstablishedSelections()==window->m_workflowSelections);
    const QString saved=settings.filePath("confirmed-group.json");
    CHECK(project->saveToFile(saved));CHECK(project->loadFromFile(saved));project->setLoadedMesh(mesh);
    CHECK(project->lastEstablishedSelections()==window->m_workflowSelections);
    CHECK(project->operations()[0].contourFeature.points==operations[0].contourFeature.points);
    CHECK(project->operations()[1].contourFeature.points==operations[1].contourFeature.points);
    window->m_operationPanel->generateProgramRequested(operations);CHECK(!project->programs().isEmpty());
    const auto program=project->programs().last();
    const QString nc=program.expandedGcodeText.isEmpty()?program.gcodeText:program.expandedGcodeText;
    if(argc>1){QFile output(QString::fromLocal8Bit(argv[1])+".nc");CHECK(output.open(QIODevice::WriteOnly));output.write(nc.toUtf8());}
    SimulationController parser;parser.loadGCode(nc,false);CHECK(parser.motionError().isEmpty());
    bool crossed=false,cutA=false,cutB=false;
    const double gap=(operations[0].contourFeature.center.x()+operations[0].contourFeature.length/2+
        operations[1].contourFeature.center.x()-operations[1].contourFeature.length/2)/2-program.modelOrigin.x();
    for(const auto &move:parser.moves()){
        if(move.fromKnownAxes!=7||move.toKnownAxes!=7)continue;
        if(!move.isRapid&&move.to.z()<0){cutA|=move.to.x()<gap;cutB|=move.to.x()>gap;}
        if((move.from.x()<gap&&move.to.x()>gap)||(move.to.x()<gap&&move.from.x()>gap)){
            CHECK(move.isRapid&&move.from.z()>=49.999&&move.to.z()>=49.999);crossed=true;
        }
    }
    CHECK(crossed&&cutA&&cutB);
    window->m_operationPanel->selectOperationById(operations.first().id);
    CHECK(QMetaObject::invokeMethod(window->m_operationPanel,"onCopy"));
    CHECK(project->operations().size()==3);
    CHECK(project->operations().last().selectionGroupId.isEmpty());
    CHECK(project->lastEstablishedSelections().size()==1);
    std::cout<<"PASS actual STEP batch UI, explicit unsupported-region removal, two atomic operations and safe final NC transfer"<<std::endl;
    std::_Exit(0);
}
