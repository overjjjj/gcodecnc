#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QFile>
#include <QLabel>
#include <QTranslator>
#include <iostream>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/workspaces/program_verification/ui/GCodeEditor.h"
#include "../src/workspaces/shared/ui/BottomBar.h"
#undef private
#include "../src/app/AppController.h"
#include "../src/tool/ToolLibrary.h"
#define CHECK(x) do { if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x<<std::endl;std::_Exit(1);} } while(0)
class HiddenWarnings : public QObject {
 bool eventFilter(QObject *object,QEvent *event) override {
  if(auto *box=qobject_cast<QMessageBox *>(object)) if(event->type()==QEvent::Polish){
   box->setAttribute(Qt::WA_DontShowOnScreen);
   QTimer::singleShot(0,box,[box]{std::cout<<box->text().toStdString()<<std::endl;box->accept();});
  }
  return false;
 }
};
int main(int argc,char **argv){
 QApplication app(argc,argv);QTemporaryDir temp;
 QTranslator translations;if(argc>2){CHECK(translations.load(QString::fromLocal8Bit(argv[2])));app.installTranslator(&translations);}
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
 QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,temp.path());
 HiddenWarnings warnings;app.installEventFilter(&warnings);
 auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1600,1000);w->show();
 CHECK(!IsWindowVisible(reinterpret_cast<HWND>(w->winId())));
 auto *project=AppController::instance().projectManager();
 MeshData mesh;mesh.bbMin={-2,-2,-1};mesh.bbMax={2,2,10};
 Triangle triangle;triangle.v0={-2,-2,10};triangle.v1={2,-2,10};triangle.v2={0,2,10};mesh.triangles={triangle};
 project->setLoadedMesh(mesh);project->setActiveRegion(FaceRegion::Front);
 SetupOrigin origin;origin.confirmed=true;project->setSetupOrigin(origin);
 StockDefinition stock;stock.confirmed=true;project->setStockDefinition(stock);
 ToolEntry tool;tool.id=963;tool.diameter=2;ToolLibrary::instance().addTool(tool);
 const QString header="G17 G40 G49 G80\nG21\nG90\nG54\nT963 M6\nS2000 M3\nG0 Z50\nG0 X-100 Y0\n";
 const QString footer="G0 Z50\nM5\nM9\nM30\n";
 auto add=[&](const QString &body){ProgramEntry p;p.name="check fixture";p.gcodeText=header+body+footer;p.coordinateVersion=1;
  p.setupFingerprint=project->setupFingerprint();p.coordinateEvidence=p.coordinateFingerprint();return project->upsertProgram(p);};
 const QString bad=add("G0 Z9\nG0 X100\n");
 const QString good=add("G1 Z40 F100\nG1 X100\n");
 w->loadProgramById(bad);CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Collision);
 CHECK(!w->validateCurrentGCodeForOutput("export"));
 w->onSimPlay();CHECK(w->m_bottomBar->m_label->text().contains(w->m_simCtrl->checkResult().message));
 w->onSimPause();CHECK(w->m_bottomBar->m_label->text().contains(w->m_simCtrl->checkResult().message));
 w->onSimStop();CHECK(w->m_bottomBar->m_label->text().contains(w->m_simCtrl->checkResult().message));
 w->m_simCtrl->simulationFinished();
 CHECK(w->m_simulationStateBadge->property("reviewState")=="blocked");
 CHECK(w->m_bottomBar->m_label->text().contains(w->m_simCtrl->checkResult().message.simplified()));
 if(argc>1)w->grab().save(QString::fromLocal8Bit(argv[1])+"/collision.png");
 w->loadProgramById(good);CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Limited);
 CHECK(w->validateCurrentGCodeForOutput("export"));
 // Export recomputes the current program, independent of a different player result.
 w->m_simCtrl->loadGCode(project->programById(bad).gcodeText);
 CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Collision);
 CHECK(w->validateCurrentGCodeForOutput("export"));
 w->loadProgramById(bad);w->m_simCtrl->loadGCode(project->programById(good).gcodeText);
 CHECK(!w->validateCurrentGCodeForOutput("export"));
 mesh.triangles.fill(triangle,13202);project->setLoadedMesh(mesh);w->loadProgramById(bad);
 CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Incomplete);
 w->m_simCtrl->simulationFinished();CHECK(w->m_simulationStateBadge->property("reviewState")=="warning");
 CHECK(!w->m_bottomBar->m_label->text().contains('\n'));
 CHECK(w->m_bottomBar->m_label->toolTip().contains(w->m_simCtrl->checkResult().message));
 CHECK(!w->validateCurrentGCodeForOutput("export"));
 if(argc>1)w->grab().save(QString::fromLocal8Bit(argv[1])+"/budget-incomplete.png");
 // Loop disclaimers cannot survive replacement by unmatched editor text.
 w->loadProgramById(good);w->m_simCtrl->loadGCode(header+"G73 X0 Y0 Z5 R20 Q1 F100\nG80\n"+footer);
 w->updateProgramReviewSummary();CHECK(!w->m_simulationStateBadge->text().contains("G73"));
 const QString external=temp.filePath("external.nc");{QFile file(external);CHECK(file.open(QIODevice::WriteOnly));file.write("G90 G17\nG0 Z50\nG0 X0 Y0\nG1 X10 F100\nM30\n");}
 QString error;CHECK(w->loadExternalGCodeFile(external,&error));
 CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Incomplete);
 w->onSimPlay();CHECK(w->m_simCtrl->isRunning());w->onSimStop();
 CHECK(!w->validateCurrentGCodeForOutput("export"));
 w->loadProgramById(good);CHECK(w->m_simCtrl->checkResult().state==SimulationCheckState::Limited);
 CHECK(w->validateCurrentGCodeForOutput("export"));
 std::cout<<"PASS persistent collision/incomplete UI, A/B binding, independent export recheck and external playback\n";std::_Exit(0);
}
