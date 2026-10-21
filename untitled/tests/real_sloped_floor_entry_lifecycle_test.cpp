#define main previousThroughLifecycleMain
#include "real_remaining_lifecycle.cpp"
#undef main
#include "../src/workspaces/process_design/selection/SlopedFloorOperation.h"
#include <QComboBox>

static bool verifySlopeDisk(const QString &directory,const ProgramEntry &program,const MeshData &mesh,
                            const MachiningOperation &operation,const ToolEntry &tool,double *minZ)
{
    QFile file(directory+"/"+(program.mainProgramFileName.isEmpty()?QStringLiteral("program.nc"):program.mainProgramFileName));
    if(!file.open(QIODevice::ReadOnly))return false;
    const auto text=QString::fromUtf8(file.readAll());
    if(text!=program.gcodeText || !text.contains("OCCLUDED STRIPS RETAINED"))return false;
    SimulationController parser;parser.loadGCode(text,false);
    if(!parser.motionError().isEmpty())return false;
    int cuts=0,retracts=0;*minZ=1e100;
    const auto binding=bindSlopedFloor(mesh,operation.selectionEvidence);if(!binding.error.isEmpty())return false;
    for(const auto &move:parser.moves()) {
        const auto from=move.from+program.modelOrigin,to=move.to+program.modelOrigin;
        if(move.isRapid && ((move.fromKnownAxes!=7) || (from.z()>=binding.sourceTopZ+3 && to.z()>=binding.sourceTopZ+3)))continue;
        if(move.fromKnownAxes!=7 || move.toKnownAxes!=7 || move.chordDeviation!=0)return false;
        if(move.isRapid && (from.x()!=to.x() || from.y()!=to.y() || to.z()<from.z()))return false;
        const auto error=slopedFloorMoveError(mesh,binding.faceIndex,from,to,tool);
        if(!error.isEmpty()){std::cerr<<"DISK_MOVE_ERROR "<<error.toStdString()<<std::endl;return false;}
        if(move.isRapid)++retracts;
        else {++cuts;*minZ=std::min(*minZ,to.z());}
    }
    std::cout<<"VERIFIED_SLOPED_DISK cuts="<<cuts<<" retracts="<<retracts<<" minZ="<<*minZ<<std::endl;
    return cuts>20 && retracts>10 && slopedFloorOperationError(operation.selectionEvidence,operation.contourFeature,operation.strategyId,&mesh).isEmpty();
}

int main(int argc,char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);QTemporaryDir settings;isolateOutputWorkflowSettings(settings.path());
    OutputWorkflowDialogs dialogs;app.installEventFilter(&dialogs);
    QTimer::singleShot(1800000,&app,[]{std::cerr<<"FAIL workflow timeout"<<std::endl;std::_Exit(99);});
    if(argc<3)return 1;const int targetFace=QString::fromLocal8Bit(argv[2]).toInt();if(targetFace!=340 && targetFace!=344)return 28;const QString out=QString::fromLocal8Bit(argv[1]);
    if(QFileInfo::exists(out))return 2;QDir().mkpath(out);dialogs.evidenceDirectory=out;
    MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1600,980);window.show();
    ToolEntry tool;tool.id=962;tool.type="end_mill";tool.name="Real curved ledge D1";
    tool.diameter=1;tool.shankDia=1;tool.fluteLen=5;tool.totalLen=20;
    ToolLibrary::instance().removeTool(tool.id);if(!ToolLibrary::instance().addTool(tool))return 3;
    auto &controller=AppController::instance();
    controller.importStep(QString::fromUtf8("D:/codex/home/worktrees/5a59/gcodecnc/untitled/测试模型/验收/特征板.STEP"));settle();
    QString error;
    settle();auto *project=controller.projectManager();
    QToolButton *setup=nullptr,*command=nullptr;
    for(auto *button:window.findChildren<QToolButton *>()) {
        if(button->text()==QStringLiteral("加工设置"))setup=button;
        if(button->text()==QStringLiteral("斜面可达区"))command=button;
    }
    if(!setup || !command)return 5;
    setup->click();settle();auto *overview=window.findChild<QDialog *>("setupOverviewDialog");
    overview->findChild<QPushButton *>("setupDirectionAction")->click();
    ViewportWidget *viewport=nullptr;for(auto *v:window.findChildren<ViewportWidget *>())if(v->isVisible())viewport=v;
    if(!viewport)return 6;QVector3D point;
    for(const auto &triangle:project->mesh().triangles)if(triangle.faceIndex==113){point=triangle.v0;break;}
    emit viewport->facePointPicked(113,point);emit viewport->facePointPicked(113,point);settle();
    QTimer::singleShot(0,[&]{for(auto *d:window.findChildren<SetupOriginDialog *>())if(d->isVisible()){d->setWorkOffset("G54");static_cast<QDialog *>(d)->accept();}});
    QMetaObject::invokeMethod(&window,"onEditSetupOrigin",Qt::DirectConnection);
    QTimer::singleShot(0,[&]{for(auto *d:window.findChildren<StockDefinitionDialog *>())if(d->isVisible())d->accept();});
    QMetaObject::invokeMethod(&window,"onEditStockDefinition",Qt::DirectConnection);
    if(!project->setupOrigin().confirmed || !project->stockDefinition().confirmed)return 7;
    command->click();settle();SelectionChainDialog *chain=nullptr;
    for(auto *d:window.findChildren<SelectionChainDialog *>())if(d->isVisible())chain=d;
    if(!chain)return 8;
    if(chain->findChild<QComboBox *>("selectionMode")->currentIndex()!=int(ChainSelectionMode::Face)) {
        std::cerr<<"FAIL sloped-floor workflow starts in edge-chain mode instead of selecting the actual face"<<std::endl;return 29;
    }
    chain->selectGeometry(QString("topology:face:%1").arg(targetFace));


    chain->selectTopology(QString("face:%1").arg(targetFace),int(SelectionTopologyKind::Face),{},0);settle();
    const auto next=chain->findChild<QPushButton *>("selectionNext");
    if(!next || !next->isEnabled()){std::cerr<<"SELECTION "<<chain->findChild<QLabel *>("selectionStatus")->text().toStdString()<<std::endl;return 9;}
    chain->grab().save(out+"/selection.png");next->click();settle();
    auto *panel=window.findChild<StrategyPanel *>();if(!panel || !panel->isVisible()) {
        std::cerr<<"REJECTED "<<chain->findChild<QLabel *>("selectionStatus")->text().toStdString()<<std::endl;
        const auto selected=chain->selection();
        std::cerr<<"BIND "<<bindSlopedFloor(project->mesh(),selected).error.toStdString()<<" axis "<<selected.toolAxis.x()<<","<<selected.toolAxis.y()<<","<<selected.toolAxis.z()<<std::endl;
        chain->grab().save(out+"/rejected.png");return 10;
    }
    const auto *preview=window.findChild<QLabel *>("slopedFloorAccessPreview");
    if(!preview || !preview->isVisible() || !preview->pixmap() || preview->pixmap()->isNull())return 24;
    const auto previewImage=preview->pixmap()->toImage();int red=0,blue=0;
    for(int y=0;y<previewImage.height();++y)for(int x=0;x<previewImage.width();++x) {
        const auto color=previewImage.pixelColor(x,y);
        if(color==QColor("#d73c35"))++red;if(color==QColor("#d9eaf7"))++blue;
    }
    if(blue<100 || !preview->toolTip().contains(QStringLiteral("候选")))return 25;
    preview->pixmap()->save(out+"/retained-strip.png");
    auto draft=panel->parameterDraft();draft.values.remove("entryMode");draft.set("safeHeight",10);draft.set("feedHeight",3);
    draft.set("stepover",.2);draft.set("floorStockToLeave",.01);draft.set("sideStockToLeave",.05);
    panel->restoreDraft(draft,tool.id);dialogs.confirmCenterCuttingFixture=true;
    QComboBox *entry=nullptr;
    for(auto *combo:panel->findChildren<QComboBox *>())if(combo->isVisible() && combo->count()==3 && combo->itemData(0).toInt()==-1 && combo->itemData(1).toInt()==0 && combo->itemData(2).toInt()==1)entry=combo;
    auto *confirm=panel->findChild<QPushButton *>("strategyPrimaryAction");
    if(!entry || !confirm || confirm->isEnabled())return 26;
    entry->setCurrentIndex(1);settle();if(!confirm->isEnabled())return 27;
    panel->window()->grab().save(out+"/parameters.png");
    confirm->click();settle();
    if(project->operations().size()!=1)return 11;const auto expected=project->operations().first();
    if(expected.strategyId!="mill_sloped_floor" || expected.selectionEvidence.boundaryLoops.size()!=1)return 12;
    auto *tree=window.findChild<OperationListPanel *>();
    std::cout<<"PHASE generate initial"<<std::endl;
    QMetaObject::invokeMethod(tree,"onGenerateProgram",Qt::DirectConnection);settle();
    if(project->programs().isEmpty())return 13;const auto initial=project->programs().last();double firstZ=0,reloadZ=0,editedZ=0;
    if(!dialogs.exportProgram(&window,initial,out+"/initial-export") || !verifySlopeDisk(out+"/initial-export",initial,project->mesh(),expected,tool,&firstZ))return 14;
    const QString path=out+"/saved-project.json";
    if(!controller.saveProject(path) || !controller.loadProject(path))return 15;settle();project=controller.projectManager();
    if(project->operations().size()!=1 || !(project->operations().first().selectionEvidence==expected.selectionEvidence))return 16;
    const int count=project->programs().size();std::cout<<"PHASE generate reloaded"<<std::endl;
    QMetaObject::invokeMethod(tree,"onGenerateProgram",Qt::DirectConnection);settle();
    if(project->programs().size()!=count+1)return 17;const auto loaded=project->programs().last();
    if(!dialogs.exportProgram(&window,loaded,out+"/reloaded-export") || !verifySlopeDisk(out+"/reloaded-export",loaded,project->mesh(),project->operations().first(),tool,&reloadZ) || firstZ!=reloadZ)return 18;
    if(!editParameter(&window,expected.id,"floorStockToLeave",.11))return 19;settle();
    if(project->operations().first().toolpathState!=ToolpathState::Stale || !dialogs.rejectExport(&window,project,loaded,out+"/stale-export"))return 20;
    tree->selectOperationById(expected.id);QMetaObject::invokeMethod(tree,"onRecalculate",Qt::DirectConnection);settle();
    if(project->operations().first().toolpathState!=ToolpathState::Valid)return 21;
    std::cout<<"PHASE generate edited"<<std::endl;QMetaObject::invokeMethod(tree,"onGenerateProgram",Qt::DirectConnection);settle();
    const auto edited=project->programs().last();
    if(edited.id==loaded.id || !dialogs.exportProgram(&window,edited,out+"/edited-export") || !verifySlopeDisk(out+"/edited-export",edited,project->mesh(),project->operations().first(),tool,&editedZ))return 22;
    if(std::abs(editedZ-firstZ-.1)>.0011 || dialogs.warnings.size()!=dialogs.expectedWarnings)return 23;
    std::cout<<"PASS real curved ledge floor GUI, actual NC sweep including retracts, save/reload, edit/stale refusal and recalculation; occluded strips retained"<<std::endl;
    return 0;
}
