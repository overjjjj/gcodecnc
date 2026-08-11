#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct ParametricToolpathCall
{
    QMap<QString, QString> arguments;
};

struct ParametricToolpathProgram
{
    QStringList sourceOperationIds;
    QString routineName;
    QStringList parameterNames;
    QStringList prefixLines;
    QStringList bodyTemplateLines;
    QList<ParametricToolpathCall> calls;
    QStringList suffixLines;

    bool isEmpty() const
    {
        return routineName.trimmed().isEmpty() ||
               parameterNames.isEmpty() ||
               bodyTemplateLines.isEmpty() ||
               calls.isEmpty();
    }
};
