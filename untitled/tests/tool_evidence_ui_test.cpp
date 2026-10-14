#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QTimer>
#include <iostream>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x<<std::endl;std::_Exit(1);}}while(0)
class Warnings : public QObject {
 bool eventFilter(QObject *object,QEvent *event) override {
  if(auto *box=qobject_cast<QMessageBox *>(object))if(event->type()==QEvent::Polish){box->setAttribute(Qt::WA_DontShowOnScreen);QTimer::singleShot(0,box,[box]{box->accept();});}
  return false;
 }
};
int main(int argc,char **argv){
 QApplication app(argc,argv);QTemporaryDir temp;QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,temp.path());
 Warnings warnings;app.installEventFilter(&warnings);
 auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->show();CHECK(!IsWindowVisible(reinterpret_cast<HWND>(w->winId())));
 auto *project=AppController::instance().projectManager();MeshData mesh;mesh.bbMin={-2,-2,-1};mesh.bbMax={2,2,10};
 Triangle t;t.v0={-2,-2,10};t.v1={2,-2,10};t.v2={0,2,10};mesh.triangles={t};project->setLoadedMesh(mesh);
 SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
 ToolEntry tool;tool.id=985;tool.diameter=2;tool.fluteLen=20;tool.totalLen=40;tool.shankDia=3;
 ToolLibrary::instance().removeTool(tool.id);ToolLibrary::instance().addTool(tool);
 ProgramEntry p;p.name="tool evidence";p.gcodeText="G17 G40 G49 G80\nG21\nG90\nG54\nT985 M6\nS2000 M3\nG0 Z50\nG0 X0 Y0\nG1 X1 F100\nG0 Z50\nM5\nM9\nM30\n";
 p.coordinateVersion=1;p.setupFingerprint=project->setupFingerprint();p.coordinateEvidence=p.coordinateFingerprint();
 const auto id=project->upsertProgram(p);w->loadProgramById(id);CHECK(!w->m_simCtrl->checkResult().blocksOutput());
 const auto originalFingerprint=ProgramSnapshotFingerprint::toolFingerprint(tool);
 for(int field=0;field<5;++field){auto changed=tool;if(field==0)changed.diameter+=.1;if(field==1)changed.fluteLen+=1;if(field==2)changed.totalLen+=1;if(field==3)changed.shankDia+=.1;if(field==4)changed.modelPath="custom.step";CHECK(ProgramSnapshotFingerprint::toolFingerprint(changed)!=originalFingerprint);}
 w->onSimPlay();CHECK(w->m_simCtrl->isRunning());tool.fluteLen=10;tool.shankDia=6;ToolLibrary::instance().updateTool(tool);
 CHECK(!w->m_simCtrl->isRunning());CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::NotChecked);CHECK(w->m_simCtrl->moves().isEmpty());
 CHECK(w->ensureSimulationLoaded());CHECK(w->m_simCtrl->moves().last().fluteLength==10&&w->m_simCtrl->moves().last().shankDiameter==6);
 CHECK(w->validateCurrentGCodeForOutput("export"));
 ToolLibrary::instance().removeTool(tool.id);CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::NotChecked);
 CHECK(w->ensureSimulationLoaded());CHECK(w->m_simCtrl->moves().last().toolDiameter==0);CHECK(w->m_simCtrl->checkResult().blocksOutput());
 CHECK(!w->validateCurrentGCodeForOutput("export"));
 std::cout<<"PASS tool geometry fingerprint, active playback invalidation, reload dimensions and final output recheck\n";std::_Exit(0);
}
