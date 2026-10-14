#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
int main(int argc,char **argv) {
    QApplication app(argc,argv);QTemporaryDir dir;QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
    int failures=0;auto check=[&](bool ok,const char *label){QTextStream(stdout)<<(ok?"PASS ":"FAIL ")<<label<<Qt::endl;if(!ok)++failures;};
    MachiningFeature feature;feature.kind=FeatureKind::Pocket;feature.subType="verified_slot_opening";
    feature.center={20,15,10};feature.axis={0,0,1};feature.region=FaceRegion::Front;feature.depth=0;
    feature.length=40;feature.width=30;feature.planarBoundaryVerified=true;
    feature.boundaryPoints={{0,0,10},{40,0,10},{40,30,10},{0,30,10}};
    StrategyPanel panel;panel.setAttribute(Qt::WA_DontShowOnScreen);panel.setContourFeature(feature);panel.setActiveRegion(FaceRegion::Front);
    for(const QString &strategy:QStringList{"mill_pocket_finish","mill_pocket_floor_finish"}) {
        check(panel.selectStrategy(strategy),"finish strategy remains independently selectable");
        QComboBox *entry=nullptr;
        for(auto *combo:panel.findChildren<QComboBox *>())if(combo->count()==3 && combo->itemData(0).toInt()==-1 && combo->itemData(1).toInt()==0 && combo->itemData(2).toInt()==1)entry=combo;
        check(entry && !entry->isHidden(),"verified finish exposes explicit entry selection");
        auto params=panel.parameterDraft();params.set("entryMode",0);params.set("depth",4);
        panel.restoreDraft(params,panel.currentTool().id);
        check(panel.parameterDraft().values.contains("entryMode") && panel.parameterDraft().get("entryMode",-1)==0,
              "selected finish entry reaches submitted parameters");
    }
    QTextStream(stdout)<<"FAILURES="<<failures<<Qt::endl;return failures?1:0;
}
