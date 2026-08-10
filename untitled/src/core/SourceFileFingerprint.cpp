#include "SourceFileFingerprint.h"

#include <QCryptographicHash>
#include <QFile>

QString SourceFileFingerprint::calculate(const QString &filePath, QString *error)
{
    if (error) {
        error->clear();
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (error) {
            *error = file.errorString();
        }
        return QString();
    }

    return QStringLiteral("sha256:") + QString::fromLatin1(hash.result().toHex());
}
