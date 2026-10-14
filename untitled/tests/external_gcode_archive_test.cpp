#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QTemporaryDir>
#include <QSettings>
#include <QDir>
#include <iostream>
#include <cstdlib>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/program_verification/ui/GCodeEditor.h"
#undef private
static void check(bool ok,const char *message) {if(!ok){std::cerr<<"FAIL "<<message<<std::endl;std::_Exit(1);}}
static bool closeEnough(double a,double b){return std::abs(a-b)<.001;}
int main(int argc,char **argv) {
    QApplication app(argc,argv);check(argc==4,"two archives and screenshot directory required");
    QTemporaryDir temp;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,temp.path());
    QVector<ExternalProgramSection> archives[2];QString texts[2];QString error;
    const int kinds[]={73,73,76,76,81,81,83,83,86,86,81,81,81};
    auto *w=new MainWindow;w->setAttribute(Qt::WA_DontShowOnScreen);w->resize(1500,950);w->show();app.processEvents();
    check(!IsWindowVisible(reinterpret_cast<HWND>(w->winId())),"hidden Windows window");
    QDir().mkpath(QString::fromLocal8Bit(argv[3]));
    for(int f=0;f<2;++f) {
        QFile file(QString::fromLocal8Bit(argv[f+1]));check(file.open(QIODevice::ReadOnly),"read real input");
        QTextStream stream(&file);stream.setCodec("UTF-8");texts[f]=stream.readAll();texts[f].replace("\r\n","\n");texts[f].replace('\r','\n');
        archives[f]=SimulationController::externalPrograms(texts[f],&error);
        check(error.isEmpty(),qPrintable(error));check(archives[f].size()==13,"13 independent programs");
        const auto originalLines=texts[f].split('\n');
        for(int i=0;i<13;++i) {
            const auto &section=archives[f][i];
            double x=i%2?440:185,y=i%2?-40:120,z=-1,r=3;
            if(i==10){x=f?-328.99:-328.154;y=f?-28.7:-28.735;z=-1.683;r=f?3:1.817;}
            if(i==11){x=185;y=120;}if(i==12){x=440;y=-40;}
            SimulationController sim;sim.loadGCode(section.text,false,section.firstLine);
            check(sim.moves().size()==3,"one startup hole: approach, cutting, return; no G28 or positioning fiction");
            const auto moves=sim.moves();
            check(closeEnough(moves[0].from.z(),20)&&closeEnough(moves[0].to.z(),r)&&moves[0].isRapid,"initial20 to exact R");
            check(closeEnough(moves[1].from.z(),r)&&closeEnough(moves[1].to.z(),z)&&!moves[1].isRapid,"exact R to bottom feed");
            check(closeEnough(moves[2].to.z(),20)&&moves[2].isRapid,"G98 initial plane return");
            for(const auto &m:moves){
                check(closeEnough(m.from.x(),x)&&closeEnough(m.to.x(),x)&&closeEnough(m.from.y(),y)&&closeEnough(m.to.y(),y),"exact hole XY; no cross-section path");
                check(m.sourceLine>=section.firstLine&&m.sourceLine<=section.lastLine,"global source range");
                check(originalLines[m.sourceLine].contains(QString("G%1 ").arg(kinds[i])),"global cycle startup line");
                check(m.toolDiameter==0&&m.toolModelPath.isEmpty(),"external H does not use CAM tool");
            }
            check(sim.hasUnverifiedRetract()==(kinds[i]==73),"external G73 warning");
            check(sim.hasUnverifiedG76LateralShift()==(kinds[i]==76),"external G76 warning");
            check(sim.hasUnverifiedG86SpindleStopRetract()==(kinds[i]==86),"external G86 warning");
            std::cout<<"archive="<<f+1<<" section="<<i+1<<" XY="<<x<<","<<y<<" Z="<<z<<" R="<<r<<" return=20 line="<<moves[1].sourceLine+1<<std::endl;
        }
        check(w->loadExternalGCodeFile(QString::fromLocal8Bit(argv[f+1]),&error),qPrintable(error));
        check(w->m_gcodeEditor->toPlainText()==texts[f],"editor preserves entire archive");
        check(w->m_externalProgramCombo->count()==13&&w->m_externalProgramCombo->currentIndex()==0,"default section one");
        for(int index:{0,10,12}) {
            w->onSimPlay();check(w->m_simCtrl->isRunning(),"play selected section");
            if(index==0)w->m_externalProgramCombo->setCurrentIndex(1);
            w->m_externalProgramCombo->setCurrentIndex(index);
            check(!w->m_simCtrl->isRunning()&&w->m_simCtrl->m_step==0,"selection stops and resets playback");
            check(w->m_simCtrl->sourceText()==archives[f][index].text,"only selected text simulated");
            check(w->ensureSimulationLoaded()&&w->m_simCtrl->moves().size()==3,"playback never reloads whole archive");
            w->m_simCtrl->seekToGCodeLine(w->m_simCtrl->moves()[1].sourceLine);
            const int step=w->m_simCtrl->m_step;
            QMetaObject::invokeMethod(w->m_gcodeEditor,"lineSelected",Q_ARG(int,index==0?archives[f][12].firstLine:0));
            check(w->m_simCtrl->m_step==step,"outside-section editor line ignored");
            w->onSimStop();w->onSimPlay();check(w->m_simCtrl->isRunning(),"stop replay");w->onSimPause();
            app.processEvents();
            check(!IsWindowVisible(reinterpret_cast<HWND>(w->winId())),"still hidden before screenshot");
            check(w->grab().save(QString::fromLocal8Bit(argv[3])+QString("/archive-%1-section-%2.png").arg(f+1).arg(index+1)),"hidden screenshot");
        }
    }
    for(int i=0;i<13;++i)check((archives[0][i].text==archives[1][i].text)==(i!=10),"only section11 differs");
    QString invalid=texts[1];const int finalCycle=invalid.lastIndexOf("G98 G81");check(finalCycle>=0,"last cycle fixture");invalid.replace(finalCycle,7,"G98 G68");
    QFile bad(temp.filePath("bad.nc"));check(bad.open(QIODevice::WriteOnly),"write invalid archive");bad.write(invalid.toUtf8());bad.close();
    w->onSimPlay();const auto previous=w->m_simCtrl->toolPath();
    check(!w->loadExternalGCodeFile(bad.fileName(),&error)&&error.contains("13"),"late invalid section rejects whole import");
    check(w->m_simCtrl->isRunning()&&w->m_simCtrl->toolPath()==previous&&w->m_gcodeEditor->toPlainText()==texts[1]&&w->m_externalProgramCombo->currentIndex()==12,"failed import preserves playback editor selection");
    w->onSimPause();
    const QString broken="G0 X10\nG91 G28 Z0\nG90\nX50\nZ20\nG1 X60\nM30\n";
    check(SimulationController::externalGCodeError(broken).isEmpty(),"reference discontinuity supported");
    w->m_simCtrl->loadGCode(broken,false);
    check(w->m_simCtrl->moves().size()==2,"no reference or unknown-start moves");
    check(w->m_simViewport->m_toolPathStarts.isEmpty()&&w->m_simViewport->m_toolPath.isEmpty(),"missing Y never becomes an invented zero position");
    w->m_simCtrl->play();check(!w->m_simCtrl->isRunning(),"unknown-only program cannot play");
    const QString knownBreak="G0 X0 Y0 Z20\nG1 X10\nG91 G28 Z0\nG90\nG0 X50\nZ20\nG1 X60\nM30\n";
    w->m_simCtrl->loadGCode(knownBreak,false);
    check(w->m_simViewport->m_toolPathStarts.size()==2&&w->m_simViewport->m_toolPath.size()==3,"known segments on each side of G28 retained");
    check(w->m_simViewport->m_toolPathStarts[1]==QVector3D(50,0,20)&&w->m_simViewport->m_toolPath[2]==QVector3D(60,0,20),"renderer keeps actual second segment start; no false connector");
    SimulationController tiny; tiny.loadGCode("G1 X0.00001\nG1 X2\nM30",false);
    check(tiny.moves().size()==2&&tiny.moves().first().fromKnownAxes==0&&tiny.moves().first().toKnownAxes==1&&std::abs(tiny.moves().last().from.x()-.00001)<.000001,"tiny coordinate establishes only X and is not rewritten as an exponent token");
    check(tiny.toolPath().isEmpty(),"tiny X-only input cannot invent Y/Z for rendering");
    tiny.loadGCode("G0 X0 Y0 Z0\nG1 X0.00001\nG1 X2\nM30",false);
    check(tiny.toolPath().size()==3&&tiny.toolPath().first()==QVector3D(0,0,0)&&std::abs(tiny.toolPath()[1].x()-.00001)<1e-11&&tiny.toolPath().last()==QVector3D(2,0,0),"known XYZ retains all three tiny-decimal path points");
    check(tiny.moves().size()==3,"XYZ establishment plus both real motion segments retained");
    check(tiny.moves().first().fromKnownAxes==0&&tiny.moves().first().toKnownAxes==7&&tiny.moves().first().isRapid&&tiny.moves().first().sourceLine==0,"initial XYZ establishment is not an invented known-start segment");
    for(int i=0;i<2;++i) {
        const auto &move=tiny.moves()[i+1];
        check(move.fromKnownAxes==7&&move.toKnownAxes==7&&!move.isRapid&&move.sourceLine==i+1,"tiny segments preserve known axes, feed mode and source lines");
        check(std::abs(move.from.x()-(i==0?0.0:.00001))<1e-12&&std::abs(move.to.x()-(i==0?.00001:2.0))<1e-12&&move.from.y()==0&&move.to.y()==0&&move.from.z()==0&&move.to.z()==0,"tiny segments retain exact starts and ends");
    }
    const QString returns="G0 X1 Y2 Z20\nG99 G81 Z-1 R3\nG98 X2\nG80\nM30\n";
    check(SimulationController::externalGCodeError(returns).isEmpty(),"G99 to G98 accepted");
    SimulationController sim;sim.loadGCode(returns,false);check(closeEnough(sim.moves().last().to.z(),20),"G98 returns original plane after G99");
    const QStringList rejected={"G68 R10\nG1 X1", "G123\nG1 X1", "G91 G1 X1", "G91 G28 Z1\nG90\nG1 X1", "G28 Z0\nG1 X1", "G43 H1 X1\nG1 X2", "G91 G28 Z0\nG90\nG81 Z-1 R3", "G0 Z20\nG83 Z-1 R3 Q0", "G0 Z20\nG81 Z-1 R3\nZ-2", "G1 X1\nM30\nG1 X2"};
    for(const auto &text:rejected)check(!SimulationController::externalGCodeError(text).isEmpty(),qPrintable("must reject: "+text));
    for(const auto &text:QStringList{"%\nG1 X1\nM30\n", "%\nG1 X1\nM30\n%\nG1 X2", "%\nG1 X1\nM30\n%\n%\nG68 R1\nM30\n%"})
        check(SimulationController::externalPrograms(text,&error).isEmpty()&&!error.isEmpty(),"malformed archive rejected");
    std::cout<<"PASS all 26 actual programs, global lines, archive UI and G28 discontinuities; hidden Windows rendering only"<<std::endl;
    std::_Exit(0);
}
