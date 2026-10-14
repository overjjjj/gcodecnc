#pragma once

#include "MachiningCoordinates.h"
#include "SketchSelectionBinding.h"
#include "src/workspaces/program_verification/simulation/SketchVerificationContext.h"
#include "src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"
#include <functional>

inline SketchVerificationContext currentSketchContext(const ProjectManager &project,
    const QList<MachiningOperation> &operations, const std::function<ToolEntry(int)> &toolLookup)
{
    SketchVerificationContext context;
    context.document=project.sketchDocument();
    context.sourceFingerprint=project.sourceFileFingerprint();
    context.setupFingerprint=project.setupFingerprint();
    context.workOffset=project.workOffset();
    context.stock=project.stockDefinition();
    context.modelOrigin=machiningOrigin(&project);
    const auto rotation=project.setupRotation();
    if(project.sourceType()!=ProjectSourceType::Sketch || !project.setupOrigin().confirmed ||
       !context.stock.confirmed || project.activeRegion()!=FaceRegion::Front ||
       rotation.x()!=0 || rotation.y()!=0 || rotation.z()!=0 || std::abs(rotation.scalar())!=1) {
        context.error=QStringLiteral("草图加工需要已确认的固定 +Z 方向、原点和毛坯。");
        return context;
    }
    for(const auto &operation:operations) {
        if(!operation.enabled) continue;
        if(operation.strategyId!="mill_closed_contour" || operation.opType==OperationType::Hole) {
            context.error=QStringLiteral("草图仅支持完整保留轮廓的外形铣。");return context;
        }
        const auto binding=bindSketchSelection(context.document,operation.selectionEvidence,
            operation.params,context.setupFingerprint,context.workOffset);
        if(!binding.error.isEmpty()){context.error=binding.error;return context;}
        SketchOperationCheck checked;
        checked.id=operation.id;
        checked.selectionFingerprint=ProgramSnapshotFingerprint::calculate({operation},{operation.id});
        checked.parameterFingerprint=QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument(QJsonObject::fromVariantMap(operation.params.values)).toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256).toHex());
        checked.depth=operation.params.get("depth",operation.contourFeature.depth);
        checked.stockToLeave=operation.params.get("stockToLeave",0);
        checked.tool=toolLookup(operation.toolId);
        if(checked.tool.id!=operation.toolId){context.error=QStringLiteral("当前工序刀具身份无效。");return context;}
        context.operations.append(checked);
    }
    if(context.operations.isEmpty()) context.error=QStringLiteral("没有可检查的当前草图工序。");
    return context;
}
