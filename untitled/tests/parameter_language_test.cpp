#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/core/Settings.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <iostream>
int main(int argc,char **argv) {
 QApplication app(argc,argv);
 ToolEntry tool;tool.id=921;tool.type="drill";tool.name="Manual drill";tool.diameter=5;tool.fluteLen=40;
 ToolLibrary::instance().addTool(tool);
 StrategyPanel panel;HoleFeature hole;hole.radius=3;hole.depth=12;hole.region=FaceRegion::Front;
 panel.setFeature(hole);panel.selectStrategy("hole_deephole");
 panel.restoreDraft(panel.parameterDraft(),tool.id);
 auto *peck=panel.findChild<QDoubleSpinBox *>("operationParameter_peckDepth");if(!peck)return 1;
 peck->setValue(1.375);
 const auto before=panel.parameterDraft().values;
 int confirmations=0;QObject::connect(&panel,&StrategyPanel::operationProposalConfirmed,[&](const OperationProposal &){++confirmations;});
 for(const auto &lang:{QString("en_US"),QString("zh_CN"),QString("en_US")}) {
  Settings::instance().setLanguage(lang);panel.retranslateUi();
  if(panel.currentTool().id!=921) {std::cerr<<"FAIL language refresh replaced manual tool\n";return 2;}
  if(panel.parameterDraft().values!=before) {std::cerr<<"FAIL language refresh changed edited parameters\n";return 3;}
  bool strategy=false;
  for(auto *combo:panel.findChildren<QComboBox *>()) if(combo->currentData().toString()=="hole_deephole")strategy=true;
  if(!strategy||confirmations)return 4;
 }
 std::cout<<"PASS language roundtrip preserves tool, edited parameters and strategy without creating an operation\n";
}
