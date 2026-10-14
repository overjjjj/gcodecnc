#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QAbstractButton>
#include <QTimer>
#include <QDir>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTextEdit>
#include "../src/workspaces/process_design/ui/ParameterEditorDialog.h"
#include <iostream>
#include "../src/app/MainWindow.h"

class DialogProbe : public QObject {
public:
    QString expected, directory;
    int failures=0,count=0;
    bool eventFilter(QObject *object,QEvent *event) override {
        if(event->type()!=QEvent::Polish)return false;
        if(auto *dialog=qobject_cast<QDialog *>(object))dialog->setAttribute(Qt::WA_DontShowOnScreen);
        if(auto *box=qobject_cast<QMessageBox *>(object))QTimer::singleShot(0,box,[this,box]{
            ++count;
            const bool readable=box->windowTitle().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")) &&
                box->text().contains(QStringLiteral("请")) && !box->text().contains("UNPROVEN") &&
                !box->text().contains("UNEXPECTED_TEST_FAILURE") && !box->text().contains(QChar(0xfffd));
            const bool detail=box->detailedText().contains(expected);
            if(!readable || !detail || !box->button(QMessageBox::Ok) || box->button(QMessageBox::Ok)->text()!=QStringLiteral("确定")) {
                ++failures;std::cerr<<"FAIL operator text/details: "<<box->text().toStdString()<<std::endl;
            }
            if(IsWindowVisible(reinterpret_cast<HWND>(box->winId())))++failures;
            box->grab().save(directory+QString("/error-%1.png").arg(count));
            for(auto *button:box->buttons())if(button->text()==QStringLiteral("查看详情")) {
                button->click();QApplication::processEvents();
                auto *details=box->findChild<QTextEdit *>();
                if(!details || details->toPlainText()!=expected)++failures;
                box->grab().save(directory+QString("/details-%1.png").arg(count));break;
            }
            box->accept();
        });
        return false;
    }
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,settings.path());
    DialogProbe probe;probe.directory=QString::fromLocal8Bit(argv[1]);QDir().mkpath(probe.directory);app.installEventFilter(&probe);
    MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);
    for(const auto &raw:QStringList{"POCKET_SINGLE_FLAT_FLOOR_UNPROVEN",QStringLiteral("程序生成已中止：\nPOCKET_OPENING_SOURCE_PROOF_MISMATCH"),"UNEXPECTED_TEST_FAILURE: kernel detail <tag>"}) {
        probe.expected=raw;
        if(!QMetaObject::invokeMethod(&window,"onErrorOccurred",Qt::DirectConnection,Q_ARG(QString,raw)))return 2;
    }
    ParameterEditorDialog parameters;parameters.setAttribute(Qt::WA_DontShowOnScreen);parameters.setChineseUi(true);
    parameters.setLabelMap({{"depth",QStringLiteral("深度")}});StrategyParams values;values.set("depth",4);parameters.setParams(values);
    auto *table=parameters.findChild<QTableWidget *>();if(!table || table->rowCount()!=1)return 4;
    table->item(0,1)->setText("-1");probe.expected=QStringLiteral("深度 必须大于 0。");
    QMetaObject::invokeMethod(&parameters,"onAccept",Qt::DirectConnection);
    if(parameters.result()==QDialog::Accepted || parameters.params().get("depth")!=4)return 5;
    if(probe.count!=4)return 3;
    std::cout<<"dialogs="<<probe.count<<" failures="<<probe.failures<<std::endl;
    return probe.failures?1:0;
}
