#include "../src/workspaces/process_design/ui/OperationEditDialog.h"
#include "../src/strategies/mill/PocketRoughingStrategy.h"
#include "../src/tool/ToolLibrary.h"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>

int main(int argc,char **argv)
{
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QApplication app(argc,argv);
    int failures=0;
    auto check=[&](bool ok,const char *message){if(!ok){++failures;std::cerr<<"FAIL "<<message<<'\n';}};
    ToolEntry longTool;longTool.id=901;longTool.name="Long reach fixture";longTool.type="end_mill";
    longTool.diameter=6;longTool.fluteLen=10;longTool.totalLen=40;
    ToolEntry shortTool=longTool;shortTool.id=902;shortTool.name="Short reach fixture";shortTool.fluteLen=5;
    ToolEntry wrongType=longTool;wrongType.id=903;wrongType.type="drill";
    auto &library=ToolLibrary::instance();
    for(const auto &tool:{longTool,shortTool,wrongType}){library.removeTool(tool.id);check(library.addTool(tool),"fixture tool added");}
    MachiningOperation operation;operation.id="slot-reach-edit";operation.opType=OperationType::Roughing;
    operation.strategyId="mill_pocket_rough";operation.toolId=longTool.id;
    auto &feature=operation.contourFeature;feature.subType="rectangular_pocket";
    feature.center=Coordinate3D(0,0,0);feature.axis=QVector3D(0,0,1);feature.region=FaceRegion::Front;
    feature.length=30;feature.width=20;feature.depth=7;
    operation.params=PocketRoughingStrategy().defaultParams();operation.params.set("depth",7);operation.params.set("entryMode",0);
    operation.params.values.remove("floorStockToLeave"); // Saved legacy operation.
    OperationEditDialog dialog(operation);dialog.setAttribute(Qt::WA_DontShowOnScreen);
    auto *tools=dialog.findChild<QComboBox *>("operationEditTool");
    auto *depth=dialog.findChild<QDoubleSpinBox *>("operationParameter_depth");
    auto *buttons=dialog.findChild<QDialogButtonBox *>();
    auto *floor=dialog.findChild<QDoubleSpinBox *>("operationParameter_floorStockToLeave");
    check(floor && floor->value()==0,"legacy operation exposes independent floor allowance defaulting to zero");
    if(floor)floor->setValue(.5);
    check(tools && depth && buttons,"actual edit controls available");
    if(!tools || !depth || !buttons)return 1;
    check(!operationEditError(operation,operation.params,shortTool.id).isEmpty(),"short cutter still blocked at original deep cut");
    depth->setValue(3);
    const int shortIndex=tools->findData(shortTool.id);
    check(shortIndex>=0,"shallower draft permits selecting newly suitable short cutter");
    check(tools->findData(wrongType.id)<0,"wrong cutter type remains excluded");
    if(shortIndex>=0){
        tools->setCurrentIndex(shortIndex);buttons->button(QDialogButtonBox::Ok)->click();
        check(dialog.result()==QDialog::Accepted && dialog.toolId()==shortTool.id && dialog.params().get("depth")==3,
              "native confirmation preserves shallower depth and explicitly selected short cutter");
        check(dialog.params().get("floorStockToLeave")==.5,"legacy editor persists new floor allowance");
        auto invalid=dialog.params();invalid.set("depth",7);
        check(!operationEditError(operation,invalid,shortTool.id).isEmpty(),"restoring excessive depth is still rejected");
    }
    operation.params.set("sideStockToLeave",.6);
    OperationEditDialog duplicate(operation);duplicate.setAttribute(Qt::WA_DontShowOnScreen);
    check(duplicate.findChild<QDoubleSpinBox *>("operationParameter_sideStockToLeave") &&
          !duplicate.findChild<QDoubleSpinBox *>("operationParameter_stockToLeave"),
          "only effective side allowance is editable when both keys exist");
    std::cout<<"FAILURES="<<failures<<'\n';return failures?1:0;
}
