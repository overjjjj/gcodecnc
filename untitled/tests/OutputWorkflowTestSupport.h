#pragma once
#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QPixmap>
#include <QAbstractButton>
#include <QPushButton>
#include <QTreeWidget>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <cmath>
#include <limits>
#include "../src/gcode/ProgramSnapshotStatus.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/workspaces/program_verification/simulation/SimulationController.h"
#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/workspaces/process_design/ui/OperationListPanel.h"
#include <iostream>
#include "../src/core/ProjectManager.h"
#include "../src/core/Settings.h"

class OutputWorkflowDialogs : public QObject {
public:
    QString destination;
    QString evidenceDirectory;
    int chooserCount=0;
    QStringList warnings;
    int expectedWarnings=0;
    bool confirmCenterCuttingFixture=false;
    int confirmedEntries=0;
    bool eventFilter(QObject *object,QEvent *event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto *dialog=qobject_cast<QDialog *>(object))dialog->setAttribute(Qt::WA_DontShowOnScreen);
        if(event->type()!=QEvent::Polish)return false;
        if(auto *file=qobject_cast<QFileDialog *>(object)) {
            ++chooserCount;
            QTimer::singleShot(0,file,[this,file]{
                const QString suffix=QFileInfo(destination).suffix();
                for(const auto &filter:file->nameFilters())
                    if(!suffix.isEmpty() && filter.contains("*."+suffix,Qt::CaseInsensitive))file->selectNameFilter(filter);
                file->setDirectory(QFileInfo(destination).absolutePath());file->selectFile(destination);
                std::cout<<"EXPORT_CHOOSER "<<destination.toStdString()<<std::endl;
                QMetaObject::invokeMethod(file,"accept");
            });
        } else if(auto *box=qobject_cast<QMessageBox *>(object)) {
            QTimer::singleShot(0,box,[this,box]{
                warnings<<box->text();std::cerr<<"WARNING "<<box->text().toStdString()<<std::endl;
                if(!evidenceDirectory.isEmpty())box->grab().save(evidenceDirectory+QString("/warning-%1.png").arg(warnings.size()));
                const bool knownEntry=box->windowTitle()==QStringLiteral("确认垂直下刀条件") &&
                    (box->text()==QStringLiteral("异形型腔会在每个独立扫描段重新垂直下刀。请确认刀具能够中心切削，或已为每个生成的分段入口预钻。是否继续创建工序？") ||
                     box->text()==QStringLiteral("精加工使用垂直下刀。请确认已完成粗加工、下刀及连接区域已清空，并核对侧壁和底部剩余量。是否继续创建工序？"));
                if(confirmCenterCuttingFixture && confirmedEntries==0 && knownEntry && box->button(QMessageBox::Yes)) {
                    ++confirmedEntries;++expectedWarnings;
                    std::cout<<"CONFIRMED_FIXTURE_CENTER_CUTTING_ENTRY"<<std::endl;
                    box->button(QMessageBox::Yes)->click();return;
                }
                // Unknown confirmations must not silently authorize a successful test.
                if(auto *no=box->button(QMessageBox::No))no->click();
                else box->accept();
            });
        }
        return false;
    }
    bool exportProgram(QObject *window,const ProgramEntry &program,const QString &directory) {
        if(QFileInfo::exists(directory) || !QDir().mkpath(directory))return false;
        destination=program.packageFiles.isEmpty()?directory+"/program.nc":directory;
        if(program.packageFiles.size()==1 || program.mainProgramFileName=="CQ8_MAIN.NC")
            destination=directory+"/"+program.mainProgramFileName;
        const int before=chooserCount, warningBefore=warnings.size();
        if(!QMetaObject::invokeMethod(window,"onExportGCode",Qt::DirectConnection) || chooserCount!=before+1 || warnings.size()!=warningBefore)return false;
        if(program.packageFiles.isEmpty()) {
            QFile file(destination);
            if(!file.open(QIODevice::ReadOnly|QIODevice::Text) || QString::fromUtf8(file.readAll())!=program.gcodeText)return false;
        } else for(const auto &entry:program.packageFiles) {
            QFile file(directory+"/"+entry.fileName);
            if(!file.open(QIODevice::ReadOnly) || file.readAll()!=entry.content.toUtf8())return false;
        }
        std::cout<<"VERIFIED_ACTUAL_EXPORT "<<directory.toStdString()<<std::endl;
        return true;
    }
    bool rejectExport(QObject *window,ProjectManager *project,const ProgramEntry &program,const QString &directory) {
        const auto status=ProgramSnapshotStatus::evaluate(project->operations(),program.sourceOperationIds,
            program.sourceOperationFingerprint,project->setupFingerprint(),program.setupFingerprint,
            ProgramSnapshotFingerprint::calculateTools(project->operations(),program.sourceOperationIds,
                [](int id){return ToolLibrary::instance().tool(id);}),program.toolFingerprint,program.coordinatesVerified());
        if(status.state!=ProgramSnapshotState::Changed)return false;
        if(QFileInfo::exists(directory))return false;
        destination=directory+"/must-not-exist.nc";
        const int before=chooserCount, warningBefore=warnings.size();
        if(!QMetaObject::invokeMethod(window,"onExportGCode",Qt::DirectConnection))return false;
        if(chooserCount!=before || QFileInfo::exists(directory) || warnings.size()<=warningBefore)return false;
        if(warnings.size()!=warningBefore+1 ||
            (!warnings.last().contains("snapshot is no longer current") && !warnings.last().contains(QStringLiteral("快照已过期"))))return false;
        expectedWarnings+=warnings.size()-warningBefore;
        std::cout<<"VERIFIED_STALE_EXPORT_REJECTED "<<directory.toStdString()<<std::endl;
        return true;
    }
};

inline void isolateOutputWorkflowSettings(const QString &directory) {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory);
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,directory);
    Settings::instance().setLanguage("zh_CN");Settings::instance().setPostProcessorId("cq8");
}

inline bool editOutputWorkflowDepth(QObject *window,const QString &id,double value) {
    auto *panel=window->findChild<OperationListPanel *>();
    if(!panel)return false;
    panel->selectOperationById(id);
    auto *tree=panel->findChild<QTreeWidget *>("operationTree");
    if(!tree || !tree->currentItem())return false;
    emit tree->itemDoubleClicked(tree->currentItem(),0);
    auto *dialog=window->findChild<OperationEditDialog *>("operationEditDialog");
    if(!dialog || !dialog->isVisible())return false;
    auto *depth=dialog->findChild<QDoubleSpinBox *>("operationParameter_depth");
    auto *buttons=dialog->findChild<QDialogButtonBox *>();
    if(!depth || !buttons || !buttons->button(QDialogButtonBox::Ok))return false;
    depth->setValue(value);buttons->button(QDialogButtonBox::Ok)->click();
    return dialog->result()==QDialog::Accepted;
}

inline bool verifyOutputDepth(const ProgramEntry &program,const QString &directory,double expectedModelZ) {
    if(!program.generationContentMatches())return false;
    QString code=program.expandedGcodeText;
    if(code.isEmpty()) {
        QFile file(directory+"/program.nc");
        if(!file.open(QIODevice::ReadOnly|QIODevice::Text))return false;
        code=QString::fromUtf8(file.readAll());
    }
    SimulationController simulation;
    simulation.loadGCode(code,true,0,program.modelOrigin);
    if(!simulation.motionError().isEmpty())return false;
    double lowest=std::numeric_limits<double>::infinity();
    for(const auto &move:simulation.moves())if(!move.isRapid)lowest=std::min(lowest,move.to.z());
    std::cout<<"FINAL_OUTPUT_DEPTH actual="<<lowest<<" expected="<<expectedModelZ<<std::endl;
    return std::isfinite(lowest) && std::abs(lowest-expectedModelZ)<0.0001;
}
