#include "Cq8PositionReader.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <iostream>
#include <cstdlib>
static void check(bool ok,const char *message) {
    if(!ok){std::cerr<<"FAIL "<<message<<std::endl;std::exit(1);}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    Cq8PositionReader reader(nullptr,20);
    QStringList commands;quint64 id=0;int updates=0,errors=0,invalidated=0;
    QObject::connect(&reader,&Cq8PositionReader::readRequested,[&](quint64 request,const QString &command){id=request;commands<<command;});
    QObject::connect(&reader,&Cq8PositionReader::positionsUpdated,[&](quint32,quint32,quint32){++updates;});
    QObject::connect(&reader,&Cq8PositionReader::errorOccurred,[&](const QString &){++errors;});
    QObject::connect(&reader,&Cq8PositionReader::positionsInvalidated,[&]{++invalidated;});
    auto reply=[&](const QByteArray &text){auto request=id;reader.appendResponse(request,text);reader.finishResponse(request);};
    auto addresses=[&] {
        check(reader.start(),"startup accepted");check(commands.last()=="I103","first feedback address query");
        reply("I103=$2000F4A0\r\n");check(commands.last()=="I203","second address query");
        reply(QByteArray::number(quint32(0x2000f4a8)));check(commands.last()=="I303","third address query");
        reply("I303 = $2000F4B0\n");check(commands.last()=="I6920..6922","batch only after all addresses verified");
        check(reader.feedbackVerified(),"verified mapping");
    };
    check(!reader.requestPositions(),"no query before startup");
    addresses();
    check(!reader.requestPositions() && !reader.start(),"no overlapping requests");
    reader.appendResponse(id,"I6920=12\r\nI6921=$FFFF");
    check(updates==0,"partial response does not publish");
    reader.appendResponse(id,"FFFF\r\nI6922=0");
    check(updates==0,"complete values require explicit frame end");
    reader.finishResponse(id);
    check(reader.positions()==QVector<quint32>({12,0xffffffffu,0}) && updates==1,"batch parsed without signed reinterpretation");
    auto oldId=id;check(reader.requestPositions(),"next batch query");
    check(reader.positions().isEmpty(),"old sample invalidated while pending");
    reader.appendResponse(oldId,"1\n2\n3");reader.finishResponse(oldId);
    check(updates==1,"late previous response ignored");reply("1\n2\n3");
    check(updates==2 && reader.positions()==QVector<quint32>({1,2,3}),"bare decimal response");
    for(const auto &bad:QList<QByteArray>{"1\n2","1\n2\n3\n4","-1\n2\n3","4294967296\n2\n3","NaN\n2\n3","1.5\n2\n3","ERR007","I6921=1\nI6920=2\nI6922=3","I6920..6922\n1\n2\n3","1\n2\n$100000000"}) {
        check(reader.requestPositions(),"request before malformed response");int prior=updates;
        reply(bad);check(reader.positions().isEmpty() && !reader.feedbackVerified() && updates==prior,"bad batch fails closed");
        check(!reader.requestPositions(),"error requires fresh address verification");addresses();reply("1\n2\n3");
    }
    reader.stop();check(reader.positions().isEmpty() && !reader.feedbackVerified(),"stop invalidates data and mapping");
    reader.start();int count=commands.size();reply("$2000F4A8");
    check(commands.size()==count && !reader.feedbackVerified(),"address mismatch blocks position query");
    reader.start();reply("I203=$2000F4A0");check(!reader.feedbackVerified(),"wrong address label rejected");
    reader.start();reader.appendResponse(id,QByteArray(4097,'0'));check(!reader.feedbackVerified(),"oversized response rejected");
    addresses();oldId=id;int before=errors;
    QEventLoop loop;QTimer::singleShot(60,&loop,&QEventLoop::quit);loop.exec();
    check(errors==before+1 && !reader.feedbackVerified() && reader.positions().isEmpty(),"timeout invalidates session");
    reader.appendResponse(oldId,"1\n2\n3");reader.finishResponse(oldId);check(reader.positions().isEmpty(),"late timed-out reply ignored");
    addresses();reply("1\n2\n3");
    int priorInvalidated=invalidated;
    check(reader.requestPositions() && invalidated==priorInvalidated+1,"new request notifies sample invalidation");
    oldId=id;reader.stop();addresses();
    reader.appendResponse(oldId,"9\n9\n9");reader.finishResponse(oldId);
    check(reader.positions().isEmpty(),"stop/restart rejects old response");
    reply("4294967295\n0\n1");check(reader.positions().first()==0xffffffffu,"decimal uint32 maximum");
    int priorUpdates=updates;reader.finishResponse(id);check(updates==priorUpdates,"duplicate completion ignored");
    priorInvalidated=invalidated;reader.stop();check(invalidated==priorInvalidated+1,"stop notifies sample invalidation");
    for(const auto &command:commands)check(command=="I103" || command=="I203" || command=="I303" || command=="I6920..6922","only read-only whitelist commands emitted");
    std::cout<<"PASS startup mapping, batch parsing, chunks, stale replies, timeout and read-only command whitelist"<<std::endl;
}
