#pragma once

#include <QString>

class SourceFileFingerprint
{
public:
    static QString calculate(const QString &filePath, QString *error = nullptr);
};
