#include "src/workspaces/program_verification/safety/GCodeSafetyValidator.h"
#include <QCoreApplication>
#include <QThread>
#include <iostream>
class ReviewThread : public QThread {
public:
    QStringList inputs;
    QList<GCodeSafetyReport> reports;
    void run() override {
        for(int repeat=0;repeat<3;++repeat)
            for(const auto &input:inputs)reports.append(GCodeSafetyValidator::validate(input));
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const QString good="G17 G40 G49 G80\nG21\nG90\nG54\nT1 M6\nS2000 M3\nG0 Z50\nG0 X0 Y0\nG1 Z-1 F100\nG0 Z50\nM5\nM9\nM30\n";
    QString missing=good;missing.replace("M30","");
    QString unsafe=good;unsafe.replace("G90","G91");
    const QStringList inputs={good,missing,unsafe,good};
    QList<GCodeSafetyReport> expected;
    for(const auto &input:inputs)expected.append(GCodeSafetyValidator::validate(input));
    if(!expected[0].ok || expected[1].ok || expected[2].ok || !expected[3].ok)return 1;
    ReviewThread a,b;a.inputs=inputs;b.inputs=inputs;a.start();b.start();a.wait();b.wait();
    for(const auto *thread:{&a,&b}) {
        if(thread->reports.size()!=12)return 2;
        for(int i=0;i<12;++i)if(thread->reports[i].ok!=expected[i%4].ok || thread->reports[i].messages!=expected[i%4].messages)return 3;
    }
    std::cout<<"PASS changed input and concurrent reports match, including thread cleanup"<<std::endl;
    return 0;
}
