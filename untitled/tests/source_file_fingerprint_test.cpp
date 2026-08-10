#include "../src/core/SourceFileFingerprint.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <iostream>

static int expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    const QString path = QDir::temp().filePath(QStringLiteral("cnext_source_fingerprint_test.step"));
    QFile::remove(path);
    {
        QFile file(path);
        if (expect(file.open(QIODevice::WriteOnly), "fingerprint fixture should open")) {
            return 1;
        }
        file.write("abc");
    }

    QString error;
    const QString fingerprint = SourceFileFingerprint::calculate(path, &error);
    if (expect(error.isEmpty(), "existing source should not report an error")) {
        return 1;
    }
    if (expect(fingerprint
                   == QStringLiteral("sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
               "fingerprint should be a stable lowercase SHA-256 value")) {
        return 1;
    }

    const QString missing = SourceFileFingerprint::calculate(path + QStringLiteral(".missing"), &error);
    if (expect(missing.isEmpty(), "missing source should not produce a fingerprint")) {
        return 1;
    }
    if (expect(!error.isEmpty(), "missing source should report an error")) {
        return 1;
    }

    QFile::remove(path);
    return 0;
}
