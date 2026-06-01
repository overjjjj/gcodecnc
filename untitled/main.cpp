#include "src/ui/MainWindow.h"
#include "src/core/AppController.h"
#include "src/core/Settings.h"
#include "src/strategies/StrategyFactory.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QTranslator>
#include <QLibraryInfo>

int main(int argc, char *argv[])
{
    // Prefer software OpenGL for stability across mixed Qt/MinGW/driver setups.
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication a(argc, argv);
    a.setOrganizationName("JIANGJUREN");
    a.setApplicationName("CNEXT-CAM");
    a.setApplicationVersion("0.1.0");

    // Load translation
    QString lang = Settings::instance().language();  // "zh_CN" or "en_US"
    static QTranslator qtTr;
    static QTranslator appTr;

    if (qtTr.load("qt_" + lang,
                  QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        a.installTranslator(&qtTr);

    if (appTr.load(QDir(QCoreApplication::applicationDirPath()).filePath("translations/" + lang)) ||
        appTr.load(QDir(QCoreApplication::applicationDirPath()).filePath(lang)) ||
        appTr.load(":/translations/" + lang))
        a.installTranslator(&appTr);

    // Bootstrap singletons
    StrategyFactory::instance();   // registers all strategies

    MainWindow w;
    w.show();
    return a.exec();
}
