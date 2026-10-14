#include "../src/app/AppController.h"
#include "../src/core/SourceFileFingerprint.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir directory;
    int failures=0;
    auto check=[&](bool ok,const char *message){if(!ok){++failures;std::cerr<<"FAIL "<<message<<std::endl;}};
    const QJsonObject contour{{"id","ac7c7ca3-7a5c-40f0-9610-501ff029007d"},{"kind","rectangle"},
        {"points",QJsonArray{QJsonArray{0.0,0.0},QJsonArray{40.0,0.0},QJsonArray{40.0,20.0},QJsonArray{0.0,20.0}}}};
    const QJsonObject document{{"version",1},{"id","f7c542a4-eb97-4307-9bbe-60cffd7f7e30"},
        {"revision",1},{"planeZ",0.0},{"contour",contour}};
    const QString fingerprint="sketch-v1:"+QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(document).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    const QJsonObject fixture{{"version","2.0"},{"sourceType","sketch"},{"sourceFilePath",""},
        {"sourceFileFingerprint",fingerprint},{"sketchDocument",document},
        {"features",QJsonArray{}},{"operations",QJsonArray{}},{"programs",QJsonArray{}}};
    auto write=[&](const QString &name,const QJsonObject &root){const QString path=directory.filePath(name);QFile file(path);
        check(file.open(QIODevice::WriteOnly),"fixture opens");const auto bytes=QJsonDocument(root).toJson();
        check(file.write(bytes)==bytes.size(),"fixture written completely");return path;};
    const QString path=write("rectangle.cnext",fixture);
    ProjectManager stored;
    check(stored.loadFromFile(path),"embedded sketch project parses");
    const QString saved=directory.filePath("resaved.cnext");check(stored.saveToFile(saved),"embedded sketch resaves");
    QFile savedFile(saved);check(savedFile.open(QIODevice::ReadOnly),"resaved project readable");
    const auto savedRoot=QJsonDocument::fromJson(savedFile.readAll()).object();
    check(savedRoot.value("sourceType").toString()=="sketch"&&savedRoot.value("sketchDocument").toObject()==document,
          "sketch identity and full geometry survive persistence");
    auto &controller=AppController::instance();
    check(controller.loadProject(path),"embedded sketch restores without a STEP file");
    check(controller.projectManager()->sourceFileFingerprint()==fingerprint,"restored sketch keeps verified document fingerprint");
    check(controller.projectManager()->mesh().triangles.isEmpty()&&controller.projectManager()->mesh().pickEdges.size()==4,
          "restored source is four real wire edges, not a fabricated part mesh");
    const auto before=controller.projectManager()->sourceFileFingerprint();
    auto unknown=fixture;unknown["sourceType"]="invented";
    check(!controller.loadProject(write("unknown.cnext",unknown)),"unknown source type rejected");
    check(controller.projectManager()->sourceFileFingerprint()==before,"invalid load preserves current project");
    auto tampered=fixture;auto changed=document;auto changedContour=contour;changedContour["points"]=QJsonArray{QJsonArray{0,0},QJsonArray{41,0},QJsonArray{41,20},QJsonArray{0,20}};
    changed["contour"]=changedContour;tampered["sketchDocument"]=changed;
    check(!controller.loadProject(write("tampered.cnext",tampered)),"changed embedded geometry cannot retain old fingerprint");
    check(controller.projectManager()->sourceFileFingerprint()==before,"tampered load preserves current project");
    auto legacy=fixture;legacy.remove("sourceType");legacy.remove("sketchDocument");
    check(!controller.loadProject(write("legacy-missing-step.cnext",legacy)),"legacy empty STEP path does not become a sketch");
    auto *project=controller.projectManager();
    MachiningOperation operation;operation.id="preserved-operation";operation.params.set("depth",2);
    project->setOperations({operation});
    ProgramEntry program;program.id="preserved-program";program.gcodeText="G90\nG0 Z50\n";
    project->setPrograms({program});project->setCurrentProgramId(program.id);
    const QString preserved=directory.filePath("preserved.cnext");
    check(project->saveToFile(preserved),"preserved state saves");
    QFile preservedFile(preserved);check(preservedFile.open(QIODevice::ReadOnly),"preserved state readable");const auto preservedBytes=preservedFile.readAll();
    const auto originalWire=project->mesh().pickEdges;
    auto rejectedWithoutMutation=[&](QJsonObject bad,const QString &name){
        project->setSourceFileFingerprint(before);
        check(project->isModified(),"test starts with dirty project");
        check(!project->loadFromFile(write(name,bad)),"manager rejects invalid source before committing members");
        check(project->isModified(),"failed manager load preserves dirty flag");
        check(!originalWire.isEmpty()&&project->mesh().pickEdges.size()==originalWire.size()&&project->mesh().pickEdges.first().id==originalWire.first().id,
              "failed manager load preserves wire geometry");
        const QString afterPath=directory.filePath("after-rejection.cnext");check(project->saveToFile(afterPath),"state after rejected load saves");
        QFile afterFile(afterPath);check(afterFile.open(QIODevice::ReadOnly),"state after rejected load readable");
        check(afterFile.readAll()==preservedBytes,"failed manager load preserves complete serialized state");
        project->setSourceFileFingerprint(before);
        check(!controller.loadProject(write("app-"+name,bad))&&controller.lastProjectLoadIssue()==ProjectLoadIssue::ProjectFileInvalid,
              "invalid sketch is a project error, not a STEP relink request");
        check(project->isModified()&&project->operations().size()==1&&project->programs().size()==1&&project->currentProgramId()==program.id,
              "failed application load preserves dirty operations and current program");
    };
    rejectedWithoutMutation(tampered,"wider-old-fingerprint.cnext");
    for(const QString &key:{QString("planeZ"),QString("revision"),QString("id")}){
        auto bad=fixture;auto doc=document;
        doc[key]=key=="id"?QJsonValue("724c431c-6f7b-44ea-a527-4d27faf91a33"):QJsonValue(2);
        bad["sketchDocument"]=doc;rejectedWithoutMutation(bad,"changed-"+key+".cnext");
    }
    auto missing=fixture;missing.remove("sourceFileFingerprint");rejectedWithoutMutation(missing,"missing-fingerprint.cnext");
    rejectedWithoutMutation(unknown,"unknown-source.cnext");
    auto malformed=fixture;auto malformedDoc=document;malformedDoc["planeZ"]="0";malformed["sketchDocument"]=malformedDoc;
    rejectedWithoutMutation(malformed,"string-coordinate.cnext");
    auto empty=fixture;auto emptyDoc=document;emptyDoc["contour"]=QJsonValue(QJsonValue::Null);empty["sketchDocument"]=emptyDoc;
    empty["sourceFileFingerprint"]="sketch-v1:"+QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(emptyDoc).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    const QString emptyPath=write("empty-sketch.cnext",empty);
    MeshData oldMesh;Triangle triangle;triangle.v0={0,0,0};triangle.v1={1,0,0};triangle.v2={0,1,0};oldMesh.triangles.append(triangle);
    project->setMesh(oldMesh);project->setFeatures({MachiningFeature{}});
    check(controller.loadProject(emptyPath),"empty sketch restores");
    check(project->mesh().triangles.isEmpty()&&project->mesh().pickEdges.isEmpty()&&project->features().isEmpty()&&project->operations().isEmpty()&&project->programs().isEmpty(),
          "empty sketch replaces all prior geometry and derived state");
    const QString emptyResaved=directory.filePath("empty-resaved.cnext");
    check(project->saveToFile(emptyResaved)&&controller.loadProject(emptyResaved),"empty sketch saves and reopens");
    if(argc>1){
        const QString model=QString::fromLocal8Bit(argv[1]);
        auto step=fixture;step.remove("sourceType");step.remove("sketchDocument");step["sourceFilePath"]=model;
        step["sourceFileFingerprint"]=SourceFileFingerprint::calculate(model);
        const QString stepPath=write("legacy-real-step.cnext",step);
        check(controller.loadProject(stepPath)&&!project->mesh().isEmpty(),"legacy real STEP still restores after sketch");
        check(controller.loadProject(emptyPath)&&project->mesh().triangles.isEmpty()&&project->mesh().pickEdges.isEmpty(),"real STEP to empty sketch clears geometry");
        const QString changedPath=directory.filePath("changed.step");check(QFile::copy(model,changedPath),"copy model for source-change check");
        auto changedStep=step;changedStep["sourceFilePath"]=changedPath;
        const auto prior=QJsonDocument::fromJson(preservedBytes).object();changedStep["operations"]=prior["operations"];changedStep["programs"]=prior["programs"];changedStep["currentProgramId"]=prior["currentProgramId"];
        const QString changedProject=write("changed-step.cnext",changedStep);
        QFile changedFile(changedPath);check(changedFile.open(QIODevice::Append),"changed source opens");changedFile.write("\n");changedFile.close();
        check(!controller.loadProject(changedProject)&&controller.lastProjectLoadIssue()==ProjectLoadIssue::SourceChanged,
              "changed STEP still requires explicit replacement");
        check(controller.loadProject(changedProject,changedPath)&&project->operations().isEmpty()&&project->programs().isEmpty(),
              "explicit changed STEP replacement restores and clears derived data");
    }
    std::cout<<"FAILURES="<<failures<<std::endl;
    return failures?1:0;
}
