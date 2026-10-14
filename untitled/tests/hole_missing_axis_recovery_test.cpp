#include "../src/app/AppController.h"
#include "../src/tool/ToolLibrary.h"
#include "../src/workspaces/process_design/operations/OperationProposal.h"
#include "../src/strategies/hole/HoleStrategyUtils.h"
#include "../src/gcode/ProgramSnapshotStatus.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;QFile original(QString::fromLocal8Bit(argv[1]));if(!original.open(QIODevice::ReadOnly))return 3;
 auto json=QJsonDocument::fromJson(original.readAll()).object();auto features=json["features"].toArray();
 for(int i=0;i<features.size();++i){auto f=features[i].toObject();f.remove("ax");f.remove("ay");f.remove("az");features[i]=f;}json["features"]=features;
 auto ops=json["operations"].toArray();for(int i=0;i<ops.size();++i){auto op=ops[i].toObject();auto f=op["holeFeature"].toObject();f.remove("ax");f.remove("ay");f.remove("az");op["holeFeature"]=f;ops[i]=op;}json["operations"]=ops;
 QTemporaryDir dir;QFile file(dir.filePath("missing-axis.cnext"));if(!file.open(QIODevice::WriteOnly))return 4;file.write(QJsonDocument(json).toJson());file.close();
 auto &controller=AppController::instance();if(!controller.loadProject(file.fileName()))return 5;
 auto *project=controller.projectManager();if(project->operations().isEmpty())return 6;
 const auto old=project->operations().first();int failures=0;
 auto check=[&](bool ok,const char *label){if(!ok){++failures;std::cerr<<label<<'\n';}};
 check(old.holeFeature.axis.isNull(),"old operation must remain unproven");
 check(!holeAxisOutputError(old.holeFeature).isEmpty(),"unproven old operation must not regenerate");
 int matches=0;HoleFeature recovered;
 for(const auto &f:project->features())if(f.center==old.holeFeature.center&&f.radius==old.holeFeature.radius&&f.faceIndices==old.holeFeature.faceIndices){++matches;recovered=f;}
 check(matches==1,"current source feature must uniquely match old geometry");
 check(holeAxisOutputError(recovered).isEmpty(),"real reimport must restore selectable Front Z feature");
 ToolEntry tool;tool.id=old.toolId;tool.type="drill";tool.diameter=recovered.radius;tool.fluteLen=80;tool.totalLen=120;ToolLibrary::instance().removeTool(tool.id);ToolLibrary::instance().addTool(tool);
 OperationProposal proposal;proposal.kind=OperationProposalKind::Hole;proposal.strategyId="hole_g81";proposal.toolId=tool.id;proposal.params=old.params;proposal.holeFeature=recovered;
 check(OperationFactory::confirm(proposal,OperationConfirmationIntent::ExplicitUser,{}).ok,"explicitly selecting recovered source feature permits new operation");
 const auto p=project->programs().first();check(!ProgramSnapshotStatus::evaluate(project->operations(),p.sourceOperationIds,p.sourceOperationFingerprint,p.setupFingerprint,p.setupFingerprint,p.toolFingerprint,p.toolFingerprint).okForOutput(),"missing-axis old output remains stale");
 std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}
