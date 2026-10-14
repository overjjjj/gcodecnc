#include "../src/workspaces/program_verification/generation/ProgramGenerationService.h"
#include "../src/postprocessor/Cq8PostProcessor.h"
#include "../src/postprocessor/SiemensPostProcessor.h"
#include "../src/strategies/mill/BlindSlotMillingStrategy.h"
#include "../src/strategies/mill/PocketRoughingStrategy.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>
#include <memory>
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x << std::endl; return 1; } } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    const auto slot = std::make_shared<BlindSlotMillingStrategy>();
    const auto pocket = std::make_shared<PocketRoughingStrategy>();
    ProgramGenerationService generator(
        [=](const QString &id) -> std::shared_ptr<StrategyBase> {
            if (id == slot->id()) return slot;
            if (id == pocket->id()) return pocket;
            return {};
        }, [](int id) {
            ToolEntry tool; tool.id=id; tool.type="end_mill";
            tool.diameter=6; tool.fluteLen=20; tool.totalLen=60; return tool;
        });
    PostProcessorOptions options;
    options.programNumber="O2401"; options.workOffset="G54";
    options.safeStartBlocks=QStringList{"G17 G40 G49 G80", "G21", "G90", "G54", "G94"};
    ProgramGenerationSnapshotOptions snapshotOptions;
    snapshotOptions.name="content binding";
    Cq8PostProcessor cq8;
    SiemensPostProcessor siemens;
    for (int mode=0; mode<3; ++mode) {
        std::shared_ptr<StrategyBase> strategy = mode==0 ? std::static_pointer_cast<StrategyBase>(slot)
                                                                     : std::static_pointer_cast<StrategyBase>(pocket);
        MachiningOperation operation;
        operation.id="binding-operation"; operation.opType=OperationType::Contour;
        operation.stage=OperationStage::RoughCut; operation.strategyId=strategy->id(); operation.toolId=6;
        operation.params=strategy->defaultParams();
        operation.contourFeature.subType=mode==0 ? "blind_slot" : "rectangular_pocket";
        operation.contourFeature.length=40; operation.contourFeature.width=mode==0 ? 12 : 20;
        operation.contourFeature.depth=4; operation.contourFeature.axis={0,0,1};
        operation.contourFeature.region=FaceRegion::Front;
        operation.params.set("depth",4); operation.params.set("slotLength",40); operation.params.set("slotWidth",12);
        operation.params.set("stockToLeave",1); operation.params.set("stepover",4);
        operation.params.set("entryMode",0); operation.params.set("helixRadius",0);
        auto generated = mode==2 ? generator.generate({operation},siemens,options,snapshotOptions)
                                : generator.generate({operation},cq8,options,snapshotOptions);
        if (!generated.ok) std::cerr<<generated.errors.join('\n').toStdString()<<std::endl;
        CHECK(generated.ok);
        const auto original=generated.snapshot;
        CHECK(original.generationContentMatches());
        CHECK((mode==0)==original.packageFiles.isEmpty());
        if(mode==1) { CHECK(original.packageFiles.size()==2); CHECK(!original.expandedGcodeText.isEmpty()); }
        ProjectManager project; project.setOperations({operation}); project.setPrograms({original});
        const QString path=temporary.filePath(QString::number(mode)+".json");
        CHECK(project.saveToFile(path));
        ProjectManager restored; CHECK(restored.loadFromFile(path));
        CHECK(restored.programById(original.id).generationContentMatches());
        auto changed=original; changed.name="renamed"; changed.createdAt=QDateTime::currentDateTime();
        CHECK(changed.generationContentMatches());
        changed=original; changed.gcodeText.replace("G90","G91"); CHECK(!changed.generationContentMatches());
        changed=original; changed.expandedGcodeText+="\nG0 X100\n"; CHECK(!changed.generationContentMatches());
        changed=original; changed.sourceOperationIds<<"other"; CHECK(!changed.generationContentMatches());
        changed=original; changed.toolFingerprint+="changed"; CHECK(!changed.generationContentMatches());
        changed=original; changed.modelOrigin.setX(1); CHECK(!changed.generationContentMatches());
        for(int index=0;index<original.packageFiles.size();++index) {
            changed=original; changed.packageFiles[index].content+="\nG0 X100\n";
            CHECK(!changed.generationContentMatches());
            project.setPrograms({changed}); CHECK(project.saveToFile(path));
            CHECK(restored.loadFromFile(path)); CHECK(!restored.programById(original.id).generationContentMatches());
        }
        project.setPrograms({original}); CHECK(project.saveToFile(path));
        QFile file(path); CHECK(file.open(QIODevice::ReadOnly));
        auto document=QJsonDocument::fromJson(file.readAll()); file.close();
        auto root=document.object(); auto programs=root["programs"].toArray(); CHECK(programs.size()==1);
        auto legacy=programs[0].toObject(); legacy.remove("generationContentEvidence"); programs[0]=legacy;
        root["programs"]=programs; CHECK(file.open(QIODevice::WriteOnly|QIODevice::Truncate));
        file.write(QJsonDocument(root).toJson()); file.close();
        CHECK(restored.loadFromFile(path)); CHECK(!restored.programById(original.id).generationContentMatches());
        std::cout<<"PASS real generation/save/reload/mutations mode="<<mode<<std::endl;
    }
    return 0;
}
