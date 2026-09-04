#include "SourceFileFingerprint.h"

#include <QCryptographicHash>
#include <QFile>

// 读取源文件并返回带 sha256: 前缀的内容指纹；失败返回空串并填写可选错误信息。
// 读取文件内容并计算 SHA-256 指纹，失败时返回错误信息。
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
