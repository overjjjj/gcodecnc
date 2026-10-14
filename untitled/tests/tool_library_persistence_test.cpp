#define NOMINMAX
#include <windows.h>
#include "../src/tool/ToolLibrary.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <climits>
#include <limits>
#include <iostream>

static int failures=0;
static void check(bool value,const char *message) {
    if(!value){++failures;std::cerr<<"FAIL "<<message<<std::endl;}
}
static QByteArray read(const QString &path) { QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return file.readAll(); }
static void write(const QString &path,const QByteArray &bytes) { QFile file(path);check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"fixture write");file.write(bytes); }
static QString state(ToolLibrary &library) {
    QString value;for(const auto &tool:library.allTools())value+=ProgramSnapshotFingerprint::toolFingerprint(tool);return value;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);auto &library=ToolLibrary::instance();
    if(argc==4 && QString::fromLocal8Bit(argv[1])=="--read") {
        check(library.loadFromFile(QString::fromLocal8Bit(argv[2])),"child load");
        check(ProgramSnapshotFingerprint::toolFingerprint(library.tool(41))==QString::fromLocal8Bit(argv[3]),"all tool evidence survives process restart");
        check(library.tool(41).shankDia==12.25,"shank diameter survives restart");
        check(library.tool(41).coating=="AlTiN","coating survives restart");
        check(library.tool(41).extra.value("angle").toDouble()==90.125,"extra angle survives restart");
        return failures?1:0;
    }
    QTemporaryDir temporary;check(temporary.isValid(),"temporary directory");
    const QString path=temporary.filePath("tools.json"),invalid=temporary.filePath("invalid.json");
    for(const auto &tool:library.allTools())library.removeTool(tool.id);
    ToolEntry tool;tool.id=41;tool.name="precise cutter";tool.type="end_mill";tool.diameter=6.125;
    tool.fluteLen=23.5;tool.totalLen=68.75;tool.shankDia=12.25;tool.flutes=4;
    tool.pointAngle=119.5;tool.pitch=1.25;tool.material="carbide";tool.coating="AlTiN";
    tool.modelPath="tool_models/custom.stl";tool.extra={{"angle",90.125},{"note","fixture"}};
    library.addTool(tool);
    check(library.saveToFile(path),"save complete library");const auto saved=read(path);
    QProcess child;child.start(QCoreApplication::applicationFilePath(),{"--read",path,ProgramSnapshotFingerprint::toolFingerprint(tool)});
    check(child.waitForFinished(30000),"child completes");
    std::cout<<child.readAllStandardError().toStdString();
    check(child.exitStatus()==QProcess::NormalExit && child.exitCode()==0,"cross-process dimensions and fingerprint");
    int changeCount=0;QObject::connect(&library,&ToolLibrary::toolLibraryChanged,[&]{++changeCount;});
    const QList<QByteArray> malformed={"[]","{}","{\"tools\":{}}","{\"tools\":[null]}",
        "{\"tools\":[{\"id\":1},{\"id\":1}]}","{\"tools\":[{\"id\":0}]}",
        "{\"tools\":[{\"id\":-1}]}","{\"tools\":[{\"id\":1.5}]}",
        "{\"tools\":[{\"id\":2147483648}]}","{\"tools\":[{\"id\":\"4\"}]}",
        "{\"tools\":[{\"id\":3,\"shankDia\":-1}]}","{\"tools\":[{\"id\":3,\"diameter\":\"bad\"}]}",
        "{\"tools\":[{\"id\":3,\"extra\":[]}]}","{bad"};
    for(const auto &bytes:malformed) {
        // Restore the same valid state before each independent malformed input.
        check(library.loadFromFile(path),"restore valid fixture");const QString before=state(library);const int previous=changeCount;
        write(invalid,bytes);check(!library.loadFromFile(invalid),"malformed file rejected");
        check(state(library)==before,"failed load preserves current tools");
        check(changeCount==previous,"failed load does not invalidate dependents");
    }
    check(library.loadFromFile(path),"restore before mutations");
    auto duplicate=tool;duplicate.diameter=10;const auto beforeDuplicate=state(library);const int beforeSignals=changeCount;
    library.addTool(duplicate);check(state(library)==beforeDuplicate,"duplicate explicit ID is rejected, not replaced");
    check(changeCount==beforeSignals,"duplicate add emits no change");
    check(library.loadFromFile(path),"restore before negative ID");
    const auto beforeNegative=state(library);
    ToolEntry negative=tool;negative.id=-5;library.addTool(negative);check(state(library)==beforeNegative,"negative ID rejected");
    check(library.loadFromFile(path),"restore before missing update");
    const auto beforeUpdate=state(library);
    ToolEntry unknown=tool;unknown.id=543;library.updateTool(unknown);check(state(library)==beforeUpdate,"update does not add missing ID");
    check(!library.saveToFile(temporary.path()),"write failure is reported");check(read(path)==saved,"failed save preserves prior file");
    HANDLE locked=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                              nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"lock existing file against replacement");
    auto edited=tool;edited.name="unsaved change";library.updateTool(edited);
    check(!library.saveToFile(path),"atomic replacement failure is reported");
    check(!library.lastError().isEmpty(),"save failure has a reason");
    check(read(path)==saved,"failed commit leaves original bytes intact");
    if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);
    check(library.loadFromFile(path),"restore before legacy");
    write(invalid,"{\"tools\":[{\"id\":81,\"diameter\":6,\"fluteLen\":20,\"totalLen\":60}]}");
    check(library.loadFromFile(invalid),"legacy absent optional fields accepted");
    check(library.tool(81).shankDia==0 && library.tool(81).extra.isEmpty(),"legacy missing shank remains unknown");
    write(invalid,"{\"tools\":[{\"id\":2147483647,\"diameter\":6}]}");
    check(library.loadFromFile(invalid),"maximum valid tool ID loads");
    ToolEntry automatic;automatic.name="automatic";automatic.diameter=3;library.addTool(automatic);
    QSet<int> ids;for(const auto &entry:library.allTools()) {check(entry.id>0,"auto ID never overflows");check(!ids.contains(entry.id),"auto ID is unique");ids.insert(entry.id);}
    check(library.allTools().size()==2,"auto add finds an unused ID after INT_MAX");
    ToolEntry explicitNext=tool;explicitNext.id=2;library.addTool(explicitNext);
    library.addTool(automatic);
    ids.clear();for(const auto &entry:library.allTools()){check(entry.id>0&&!ids.contains(entry.id),"interleaved explicit and automatic IDs are unique");ids.insert(entry.id);}
    check(library.allTools().size()==4,"interleaved add succeeds");
    write(invalid,"{\"tools\":[]}");check(library.loadFromFile(invalid)&&library.allTools().isEmpty(),"explicit empty library is valid");
    std::cout<<"FAILURES="<<failures<<std::endl;return failures?1:0;
}

