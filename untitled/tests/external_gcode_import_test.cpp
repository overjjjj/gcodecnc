#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QGroupBox>
#include <QListWidget>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <iostream>
#include <cstdlib>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/workspaces/program_verification/ui/GCodeEditor.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/tool/ToolLibrary.h"
static void check(bool ok,const char *message) {
    if(!ok){std::cerr<<"FAIL "<<message<<std::endl;std::_Exit(1);}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);QTemporaryDir temp;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,temp.path());
    auto write=[&](QString name,QString text){QString path=temp.filePath(name);QFile f(path);check(f.open(QIODevice::WriteOnly),"write fixture");f.write(text.toUtf8());return path;};
    const QString a="G21 G90 G17 G69\nG0 X10 Y0 Z0 (X999 G91)\nG3 X0 Y10 I-10 J0\nG1 X0 Y0\nM30\n";
    const QString b="; program B\n\nG21 G90 G17\nG0 X20000 Y12000 Z0\nG1 X22000\nY14000\nX20000\nY12000\nM30\n";
    auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1500,950);w->show();app.processEvents();
    if(QApplication::platformName()=="windows")check(!IsWindowVisible(reinterpret_cast<HWND>(w->winId())),"test window never occupies desktop");
    auto *project=AppController::instance().projectManager();
    check(project->mesh().isEmpty()&&project->setupFingerprint().isEmpty(),"fixture starts without model or Setup");
    auto *action=w->findChild<QAction*>("importGCodeAction");
    auto *button=w->findChild<QToolButton*>("importGCodeButton");
    check(action&&button&&button->defaultAction()==action&&w->m_fileMenu->actions().contains(action),"discoverable menu and program-page import");
    QString error;
    check(w->loadExternalGCodeFile(write("A.nc",a),&error),qPrintable(error));
    check(w->m_gcodeEditor->toPlainText()==a&&w->m_simCtrl->sourceText()==a,"original final text is simulated");
    check(w->m_pageNav->currentRow()==1&&w->m_simViewport->m_mesh.isEmpty(),"no-model import opens verification");
    check(w->m_finalProgramPanel->title().contains(QString::fromUtf8("导入"))||w->m_finalProgramPanel->title().contains("Imported"),"external text is not labelled as generated CQ8 output");
    check(w->m_macroLibraryEditor->isHidden()&&!w->m_simViewport->m_toolGeometryVisible,"external hides CQ8 macro panel and undefined solid tool");
    check(w->m_simCtrl->moves().size()>13,"IJ arc is segmented");
    check(w->m_simCtrl->moves().first().to==QVector3D(10,0,0),"comment ignored");
    check(!w->m_actSendToMachine->isEnabled()&&!w->m_actExportGCode->isEnabled(),"external output disabled");
    for(auto *badge:{w->m_programSnapshotBadge,w->m_programSafetyBadge,w->m_outputReadinessBadge})
        check(badge->property("reviewState")!="ready","external cannot inherit passed/ready badges");
    w->onSimPlay();check(w->m_simCtrl->isRunning(),"play starts");
    w->m_simCtrl->onTick();check(w->m_simViewport->m_hasToolPos&&w->m_simViewport->m_toolPos==w->m_simCtrl->moves().first().to,"external point marker follows playback");
    w->onSimPause();check(!w->m_simCtrl->isRunning(),"pause stops timer");
    const auto pathA=w->m_simCtrl->toolPath();
    for(const QString &path:QStringList{QString(),temp.filePath("missing.nc"),write("blank.nc"," \n\t"),write("still.nc","G21 G90 G17\nM30"),write("bad.nc","G91\nG1 X10")}) {
        check(!w->loadExternalGCodeFile(path,&error),"cancel/failed/empty/unsupported rejected");
        check(w->m_gcodeEditor->toPlainText()==a&&w->m_simCtrl->toolPath()==pathA&&!w->m_simCtrl->isRunning(),"failure preserves paused A");
    }
    w->m_simViewport->setToolDiameter(40);w->m_simViewport->setToolModelPath("old-tool.step");
    check(w->loadExternalGCodeFile(write("B.tap",b),&error),qPrintable(error));
    check(!w->m_simCtrl->isRunning()&&w->m_simCtrl->m_step==0&&w->m_gcodeEditor->m_executionLine==-1,"paused A to B resets playback and execution line");
    check(w->m_simViewport->m_toolModelPath.isEmpty()&&w->m_simViewport->m_toolDiameter!=40,"old tool cleared");
    check(w->m_simCtrl->moves().first().sourceLine==3&&w->m_simCtrl->moves().first().to==QVector3D(20000,12000,0),"B path and source lines replace A");
    for(const auto &point:w->m_simCtrl->toolPath()) {
        const auto clip=(w->m_simViewport->m_proj*w->m_simViewport->currentViewMatrix()).map(point);
        check(std::abs(clip.x())<1&&std::abs(clip.y())<1&&std::abs(clip.z())<1,"whole path fits viewport");
    }
    w->m_gcodeEditor->lineSelected(4);check(w->m_gcodeEditor->m_executionLine==4,"source-line seek uses B");
    w->m_simCtrl->setSpeed(4);check(w->m_simCtrl->m_timer->interval()==12,"speed control reused");
    w->onSimPlay();w->onSimStop();w->onSimPlay();
    check(w->m_simCtrl->isRunning()&&w->m_simViewport->m_toolPath==w->m_simCtrl->toolPath(),"stop then replay retains complete displayed path");
    check(!w->loadExternalGCodeFile(temp.filePath("missing.nc"),&error)&&w->m_simCtrl->isRunning(),"failed import preserves playing B");
    check(w->loadExternalGCodeFile(temp.filePath("A.nc"),&error)&&!w->m_simCtrl->isRunning(),"successful import stops playing B");
    MeshData camMesh;camMesh.bbMin={100,100,0};camMesh.bbMax={200,200,10};
    Triangle triangle;triangle.v0={100,100,0};triangle.v1={200,100,0};triangle.v2={100,200,0};triangle.normal={0,0,1};camMesh.triangles.append(triangle);
    project->setLoadedMesh(camMesh);
    ToolEntry tool;tool.id=900;tool.name="CAM tool fixture";tool.diameter=40;ToolLibrary::instance().addTool(tool);
    ProgramEntry old;old.name="CAM fixture";old.gcodeText="T900\nG0 X5\nG1 X8\n";old.macroText="old macro";old.expandedGcodeText="T900\nG0 X6\nG1 X9\n";
    const auto id=project->upsertProgram(old);w->loadProgramById(id);
    check(w->m_simCtrl->sourceText()==old.expandedGcodeText,"CAM expanded text preserved");
    check(!w->m_simViewport->m_mesh.isEmpty(),"CAM mesh present before external import");
    const auto saved=project->programById(id);
    check(w->loadExternalGCodeFile(temp.filePath("B.tap"),&error),"CAM to external");
    w->syncCurrentProgramSnapshot();w->syncProgramList();
    check(w->m_currentProgramId.isEmpty()&&project->currentProgramId().isEmpty()&&w->m_programList->currentRow()==-1,"CAM association cleared");
    check(w->m_macroLibraryEditor->toPlainText().isEmpty(),"macro display cleared");
    check(w->m_simViewport->m_mesh.isEmpty()&&!project->mesh().isEmpty(),"external viewport isolated without deleting CAM model");
    const auto after=project->programById(id);
    check(after.gcodeText==saved.gcodeText&&after.macroText==saved.macroText&&after.expandedGcodeText==saved.expandedGcodeText,"old CAM snapshot not overwritten");
    w->m_gcodeEditor->setGCode("G91\nG1 X100");w->onSimPlay();
    check(!w->m_simCtrl->isRunning()&&w->m_simViewport->m_toolPath.isEmpty()&&w->m_simulationReviewState!=MainWindow::SimulationReviewState::Running,"changed unsupported text cannot replay stale path");
    w->loadProgramById(id);check(w->m_externalGCodePath.isEmpty()&&w->m_simCtrl->sourceText()==saved.expandedGcodeText&&!w->m_simViewport->m_mesh.isEmpty(),"return to CAM restores source mode and model");
    check(!w->m_macroLibraryEditor->isHidden()&&w->m_simViewport->m_toolGeometryVisible,"return to CAM restores macro panel and tool geometry");
    check(w->loadExternalGCodeFile(temp.filePath("A.nc"),&error),"restore sample for screenshot");
    QEventLoop settle;QTimer::singleShot(250,&settle,&QEventLoop::quit);settle.exec();
    if(argc>1)check(w->grab().save(QString::fromLocal8Bit(argv[1])),"offscreen window screenshot");
    std::cout<<"PASS external import transaction, no model, arc, A/B playback and line mapping, fit, CAM isolation, stop/replay"<<std::endl;
    std::_Exit(0);
}
