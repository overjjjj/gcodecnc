#include <QApplication>
#include <QMessageBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QEvent>
#include <iostream>
#include "../src/app/MainWindow.h"

class HiddenDialogs : public QObject {
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Polish)
            if (auto *widget = qobject_cast<QWidget *>(object))
                if (widget->isWindow()) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    HiddenDialogs hidden;
    app.installEventFilter(&hidden);
    QTimer::singleShot(30000, &app, []() { std::_Exit(99); });
    app.setApplicationVersion(QStringLiteral("0.1.0-test-build"));
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    int failures = 0;
    auto check = [&](bool ok, const char *message) {
        std::cout << (ok ? "PASS " : "FAIL ") << message << std::endl;
        failures += !ok;
    };
    check(window.windowTitle().contains(app.applicationVersion()), "title identifies running build");
    bool inspected = false;
    QTimer::singleShot(0, &window, [&]() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                box->setAttribute(Qt::WA_DontShowOnScreen);
                check(box->text().contains(app.applicationVersion()), "about uses application version");
                inspected = true;
                box->accept();
            }
        }
    });
    QMetaObject::invokeMethod(&window, "onAbout", Qt::DirectConnection);
    check(inspected, "actual about dialog inspected");
    QMetaObject::invokeMethod(&window, "onLanguageEnglish", Qt::DirectConnection);
    check(window.windowTitle().contains(app.applicationVersion()), "language change retains build version");
    std::cout << "FAILURES=" << failures << std::endl;
    return failures ? 1 : 0;
}
