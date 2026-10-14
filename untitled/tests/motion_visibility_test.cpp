#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <iostream>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#undef private
int main(int argc,char **argv){
 QApplication app(argc,argv);QTemporaryDir settings;
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
 MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);
 MachiningOperation a,b;a.id="a";b.id="b";window.m_operationPanel->setOperations({a,b});
 window.m_operationPanel->m_visibility.insert("a",false);
 window.m_simCtrl->loadGCode("(first [op:a])\nG0 Z50\nG0 X-5 Y0\nG1 Z9\n(second [op:b])\nG1 X5");
 window.refreshOperationPathVisibility();
 bool ok=window.m_simViewport->m_toolPath.size()==3&&window.m_simViewport->m_toolPathVisible==QVector<bool>{false,true};
 std::cout<<(ok?"PASS":"FAIL")<<" operation visibility aligns with rendered known segments"<<std::endl;
 QFile unknown(settings.filePath("unknown.nc"));if(!unknown.open(QIODevice::WriteOnly))return 2;unknown.write("G1 X1\nM30\n");unknown.close();
 QString error;if(!window.loadExternalGCodeFile(unknown.fileName(),&error))return 3;
 window.onSimPlay();const bool unavailable=!window.m_simCtrl->isRunning()&&window.m_simulationReviewState==MainWindow::SimulationReviewState::Unavailable;
 std::cout<<(unavailable?"PASS":"FAIL")<<" unknown-only program never advertises playing"<<std::endl;ok&=unavailable;
 return ok?0:1;
}
