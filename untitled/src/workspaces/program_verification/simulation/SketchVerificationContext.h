#pragma once

#include "src/core/SketchDocument.h"
#include "src/core/StockDefinition.h"
#include "src/tool/ToolEntry.h"

struct SketchOperationCheck
{
    QString id;
    QString selectionFingerprint;
    QString parameterFingerprint;
    double depth = 0;
    double stockToLeave = 0;
    ToolEntry tool;
};

// A freshly rebuilt snapshot of current project evidence, never persisted as authority.
struct SketchVerificationContext
{
    SketchDocument document;
    QString sourceFingerprint;
    QString setupFingerprint;
    QString workOffset = "G54";
    StockDefinition stock;
    Coordinate3D modelOrigin;
    QVector<SketchOperationCheck> operations;
    QString error;

    QString fingerprint() const
    {
        QJsonArray entries;
        for (const auto &operation : operations) {
            const auto &tool = operation.tool;
            entries.append(QJsonObject{{"id",operation.id},{"selection",operation.selectionFingerprint},
                {"parameters",operation.parameterFingerprint},{"depth",operation.depth},
                {"stockToLeave",operation.stockToLeave},{"toolId",tool.id},{"type",tool.type},
                {"diameter",tool.diameter},{"fluteLen",tool.fluteLen},{"totalLen",tool.totalLen},
                {"shankDia",tool.shankDia},{"modelPath",tool.modelPath},
                {"extra",QJsonObject::fromVariantMap(tool.extra)}});
        }
        const QJsonObject value{{"version",1},{"document",document.toJson()},
            {"source",sourceFingerprint},{"setup",setupFingerprint},{"workOffset",workOffset},
            {"origin",QJsonArray{modelOrigin.x(),modelOrigin.y(),modelOrigin.z()}},
            {"stock",QJsonArray{stock.minusX,stock.plusX,stock.minusY,stock.plusY,stock.minusZ,stock.plusZ}},
            {"stockConfirmed",stock.confirmed},{"operations",entries},{"error",error}};
        return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(value).toJson(QJsonDocument::Compact),
                                                         QCryptographicHash::Sha256).toHex());
    }
};
