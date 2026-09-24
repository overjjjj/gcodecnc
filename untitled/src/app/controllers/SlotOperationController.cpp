#include "SlotOperationController.h"
#include "../../core/ProjectManager.h"
#include "../../workspaces/process_design/ui/FeatureListPanel.h"
#include "../../workspaces/process_design/ui/StrategyPanel.h"
#include "../../workspaces/process_design/ui/OperationListPanel.h"
#include "../../workspaces/process_design/ui/SelectionChainDialog.h"
#include "../../workspaces/process_design/ui/ParameterEditorDialog.h"
#include "../../workspaces/process_design/ui/ContourFeatureGrouping.h"
#include "../../workspaces/process_design/ui/ToolOperationCompatibility.h"
#include "../../workspaces/process_design/operations/SlotOperationState.h"
#include "../../workspaces/process_design/selection/FeatureIdentity.h"
#include "../../strategies/StrategyFactory.h"
#include "../../tool/ToolLibrary.h"
#include "../MillingBatchCommit.h"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>

SlotOperationController::SlotOperationController(ProjectManager *project, FeatureListPanel *features,
    StrategyPanel *strategy, OperationListPanel *operations, ConvertFeature convert,
    ValidateProposal validate, QWidget *parent)
    : QObject(parent), m_project(project), m_features(features), m_strategy(strategy),
      m_operations(operations), m_parent(parent), m_convert(std::move(convert)), m_validate(std::move(validate))
{
    connect(strategy, &StrategyPanel::slotGeometryRequested, this, &SlotOperationController::selectGeometry);
    connect(strategy, &StrategyPanel::operationProposalConfirmed, this, &SlotOperationController::confirm);
    connect(strategy, &StrategyPanel::slotGeometryInvalidated, this, [this]() {
        m_draft.clear(); m_members.clear(); m_context.clear(); ++m_revision;
    });
    connect(features, &FeatureListPanel::featureSelected, this, &SlotOperationController::invalidate);
    connect(features, &FeatureListPanel::featureGroupSelected, this, &SlotOperationController::invalidate);
    connect(features, &FeatureListPanel::checkedFeaturesChanged, this, &SlotOperationController::invalidate);
    connect(project, &ProjectManager::projectChanged, this, &SlotOperationController::invalidate);
    connect(project, &ProjectManager::machiningContextChanged, this, &SlotOperationController::invalidate);
    connect(operations, &OperationListPanel::editSlotRequested, this, &SlotOperationController::edit);
}

void SlotOperationController::invalidate()
{
    m_strategy->invalidateSlotGeometry();
}

QString SlotOperationController::contextKey() const
{
    const auto rotation = m_project->setupRotation();
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(m_project->sourceFileFingerprint(), m_project->setupFingerprint(), m_project->workOffset())
        .arg(int(m_project->activeRegion())).arg(rotation.scalar(), 0, 'g', 17)
        .arg(rotation.x(), 0, 'g', 17).arg(rotation.y(), 0, 'g', 17).arg(rotation.z(), 0, 'g', 17);
}

QVector<int> SlotOperationController::selectedMembers() const
{
    QVector<int> indices;
    const auto &features = m_project->features();
    for (int index : m_features->checkedFeatureIndices()) {
        if (index >= 0 && index < features.size() && features[index].kind == FeatureKind::Slot) indices.append(index);
    }
    if (indices.isEmpty()) {
        const int index = m_features->currentFeatureIndex();
        if (index >= 0 && index < features.size() && features[index].kind == FeatureKind::Slot) indices.append(index);
    }
    std::sort(indices.begin(), indices.end());
    return indices;
}

bool SlotOperationController::chooseGeometry(OperationProposal &proposal)
{
    proposal.selectionEvidence = {};
    if (m_project->sourceFileFingerprint().isEmpty() || m_project->setupFingerprint().isEmpty()) {
        emit errorOccurred(tr("需要已确认的模型版本和 Setup。")); return false;
    }
    const auto &feature = proposal.contourFeature;
    SelectionCandidate candidate;
    candidate.geometryId = stableContourId(feature);
    candidate.label = tr("已识别槽 L%1 W%2 D%3").arg(feature.length).arg(feature.width).arg(feature.depth);
    candidate.referencePoint = feature.center; candidate.surfaceNormal = feature.axis;
    candidate.points = feature.points;
    if (candidate.points.isEmpty()) {
        const double a = feature.angle * std::acos(-1.) / 180.;
        for (const auto &p : {QVector3D(-feature.length/2,-feature.width/2,0), QVector3D(feature.length/2,-feature.width/2,0),
                              QVector3D(feature.length/2,feature.width/2,0), QVector3D(-feature.length/2,feature.width/2,0)}) {
            candidate.points.append(feature.center + QVector3D(p.x()*std::cos(a)-p.y()*std::sin(a),
                                                                p.x()*std::sin(a)+p.y()*std::cos(a), 0));
        }
    }
    candidate.closed = candidate.points.size() >= 3;
    candidate.outerLoopPointCount = candidate.points.size();
    candidate.islandCount = feature.islands.size(); candidate.freeEndCount = candidate.closed ? 0 : 2;
    SelectionChainDialog dialog(m_parent, true);
    dialog.setRequireClosedBoundary(proposal.strategyId != QStringLiteral("mill_slot"));
    dialog.setContext(m_project->sourceFileFingerprint(), m_project->setupFingerprint(), m_project->workOffset());
    dialog.setCandidates({candidate});
    if (dialog.exec() != QDialog::Accepted) return false;
    proposal.selectionEvidence = dialog.selection();
    return true;
}

bool SlotOperationController::locate(const ContourFeature &feature) const
{
    int matches = 0;
    for (const auto &source : m_project->features()) {
        if (source.kind == FeatureKind::Slot && sameSlotGeometry(feature, m_convert(source))) ++matches;
    }
    return matches == 1;
}

QString SlotOperationController::validate(const OperationProposal &proposal) const
{
    const auto &feature = proposal.contourFeature;
    const double depth = proposal.params.get(QStringLiteral("depth"), feature.depth);
    if (!std::isfinite(depth) || depth <= 0.0 || depth > feature.depth)
        return tr("加工目标深度必须大于 0 且不超过真实槽深度。");
    if (m_project->sourceFileFingerprint().isEmpty() || m_project->setupFingerprint().isEmpty() ||
        m_project->activeRegion() == FaceRegion::Unknown || feature.region != m_project->activeRegion())
        return tr("模型或 Setup 已失效，请重新确认几何。");
    const auto access = evaluateSelectionAccess(proposal.selectionEvidence,
        m_project->sourceFileFingerprint(), m_project->setupFingerprint(), m_project->workOffset(),
        proposal.strategyId != QStringLiteral("mill_slot"));
    if (!access.ok()) return access.issueCode;
    const auto tool = ToolLibrary::instance().tool(proposal.toolId);
    if (tool.id <= 0) return tr("原刀具不存在，请选择有效刀具。");
    const auto report = reviewToolCompatibility(proposal.strategyId, tool, feature, true, depth);
    if (report.hasBlockingIssues()) {
        QStringList errors;
        for (const auto &issue : report.issues) if (issue.severity == ToolCompatibilitySeverity::Blocking) errors << issue.message;
        return errors.join(QLatin1Char('\n'));
    }
    const QString error = m_validate(proposal, tool);
    if (!error.isEmpty()) return error;
    const auto strategy = StrategyFactory::instance().strategy(proposal.strategyId);
    if (!strategy) return tr("槽策略不可用。");
    const auto path = strategy->generate(feature, tool, slotCuttingParams(proposal.params));
    if (path.ok && !path.gcode.isEmpty()) return QString();
    return path.errorMsg.isEmpty() ? tr("槽刀路预生成失败或结果为空。") : path.errorMsg;
}

void SlotOperationController::selectGeometry(const QString &strategyId)
{
    if (!isSlotStrategy(strategyId)) return;
    invalidate();
    const quint64 revision = m_revision;
    const QString context = contextKey();
    const QVector<int> members = selectedMembers();
    QVector<MachiningFeature> sources;
    for (int index : members) sources.append(m_project->features().at(index));
    if (sources.isEmpty()) { emit errorOccurred(tr("请先选择已识别的槽候选。")); return; }
    if (sources.size() > 1 && !contourFeaturesShareGroup(sources)) {
        emit errorOccurred(tr("请一次只选择同一类型、尺寸、深度、角度和加工面的槽组。")); return;
    }
    QList<OperationProposal> selected;
    QList<ContourFeature> geometry;
    for (const auto &source : sources) {
        OperationProposal proposal; proposal.kind = OperationProposalKind::Contour;
        proposal.strategyId = strategyId; proposal.contourFeature = m_convert(source);
        if (!chooseGeometry(proposal)) return;
        if (revision != m_revision || context != contextKey() || members != selectedMembers()) return;
        selected.append(proposal); geometry.append(proposal.contourFeature);
    }
    m_draft = selected; m_members = members; m_context = context;
    m_strategy->setSlotGeometry(geometry);
}

void SlotOperationController::confirm(const OperationProposal &parameters)
{
    if (!isSlotStrategy(parameters.strategyId)) return;
    const auto selected = m_draft;
    const quint64 revision = m_revision;
    if (selected.isEmpty() || m_context != contextKey() || m_members != selectedMembers()) {
        invalidate(); emit errorOccurred(tr("槽几何草稿已失效，请重新选择。")); return;
    }
    QList<MachiningOperation> operations;
    for (int i = 0; i < selected.size(); ++i) {
        auto proposal = selected[i];
        if (parameters.strategyId != proposal.strategyId ||
            !sameSlotGeometry(proposal.contourFeature, m_convert(m_project->features().at(m_members[i])))) {
            invalidate(); emit errorOccurred(tr("原槽候选已变化，请重新选择。")); return;
        }
        proposal.toolId = parameters.toolId; proposal.params = slotCuttingParams(parameters.params);
        proposal.params.set(QStringLiteral("workOffset"), m_project->workOffset().mid(1).toInt());
        const QString error = validate(proposal);
        if (!error.isEmpty()) { emit errorOccurred(error); return; }
        const auto confirmed = OperationFactory::confirm(proposal, OperationConfirmationIntent::ExplicitUser,
            {m_project->sourceFileFingerprint(), m_project->setupFingerprint(), m_project->workOffset()});
        if (!confirmed.ok) { emit errorOccurred(confirmed.error); return; }
        operations.append(confirmed.operation);
    }
    if (revision != m_revision || m_context != contextKey()) {
        invalidate(); emit errorOccurred(tr("确认期间几何版本已变化。")); return;
    }
    const auto strategy = StrategyFactory::instance().strategy(parameters.strategyId);
    const auto tool = ToolLibrary::instance().tool(parameters.toolId);
    invalidate(); // Consume once, before any project signals can re-enter the UI.
    const auto batch = validateAndCommitMillingBatch(operations, *strategy, tool,
        [this](const QList<MachiningOperation> &ops) { return m_operations->addConfirmedOperations(ops); });
    if (!batch.ok) { emit errorOccurred(batch.error); return; }
    if (!batch.operationIds.isEmpty()) m_operations->selectOperationById(batch.operationIds.last());
    emit operationsConfirmed(batch.operationIds);
}

void SlotOperationController::edit(MachiningOperation original)
{
    if (!isSlotStrategy(original.strategyId)) return;
    auto working = original;
    const QString context = contextKey();
    ParameterEditorDialog dialog(m_parent);
    dialog.setChineseUi(true);
    dialog.setWindowTitle(tr("编辑槽工序 %1 · %2").arg(original.id, original.featureRef));
    auto params = slotCuttingParams(original.params);
    params.values.remove(QStringLiteral("workOffset"));
    if (!params.values.contains(QStringLiteral("depth"))) params.set(QStringLiteral("depth"), original.contourFeature.depth);
    dialog.setNonNegativeKeys({"stockToLeave", "finishStock", "compensation", "helixRadius", "helixPitch", "feedHeight", "coolantMode", "depthMode"});
    dialog.setAllowAnyValueKeys({"compensation", "slopeDirection"});
    dialog.setParams(params); dialog.setDefaultParams(params);
    auto *layout = qobject_cast<QVBoxLayout *>(dialog.layout());
    auto *summary = new QLabel(tr("只读几何：L%1 W%2 D%3 · (%4,%5,%6) · A%7")
        .arg(working.contourFeature.length).arg(working.contourFeature.width).arg(working.contourFeature.depth)
        .arg(working.contourFeature.center.x()).arg(working.contourFeature.center.y()).arg(working.contourFeature.center.z())
        .arg(working.contourFeature.angle), &dialog);
    summary->setWordWrap(true); layout->insertWidget(0, summary);
    auto *tools = new QComboBox(&dialog); tools->setObjectName(QStringLiteral("slotEditTool"));
    for (const auto &tool : ToolLibrary::instance().allTools()) tools->addItem(QStringLiteral("T%1 D%2 · %3").arg(tool.id).arg(tool.diameter).arg(tool.type), tool.id);
    tools->setCurrentIndex(tools->findData(original.toolId)); layout->insertWidget(1, tools);
    auto *reselect = new QPushButton(tr("重新选择原工序几何"), &dialog);
    reselect->setObjectName(QStringLiteral("reselectEditedSlot")); layout->insertWidget(2, reselect);
    connect(reselect, &QPushButton::clicked, &dialog, [&]() {
        working.selectionEvidence = {};
        if (!locate(original.contourFeature)) { emit errorOccurred(tr("无法唯一定位原槽对象，请重新创建工序。")); return; }
        OperationProposal proposal; proposal.strategyId = original.strategyId; proposal.contourFeature = original.contourFeature;
        const QString selectedContext = contextKey();
        if (chooseGeometry(proposal) && selectedContext == contextKey()) {
            working.selectionEvidence = proposal.selectionEvidence;
        }
    });
    auto *validationMessage = new QLabel(&dialog);
    validationMessage->setObjectName(QStringLiteral("slotEditValidationError"));
    validationMessage->setWordWrap(true);
    validationMessage->setProperty("warning", true);
    validationMessage->hide();
    layout->insertWidget(3, validationMessage);
    bool toolsChanged = false;
    connect(&ToolLibrary::instance(), &ToolLibrary::toolLibraryChanged, &dialog, [&]() { toolsChanged = true; });
    while (dialog.exec() == QDialog::Accepted) {
        if (toolsChanged || context != contextKey() || !locate(original.contourFeature)) {
            emit errorOccurred(tr("编辑期间模型、Setup、原对象或刀具已变化，请重新打开编辑窗口。")); return;
        }
        const auto &operations = m_operations->operations();
        const auto current = std::find_if(operations.cbegin(), operations.cend(),
            [&](const MachiningOperation &operation) { return operation.id == original.id; });
        if (current == operations.cend() || !sameSlotOperation(*current, original)) {
            emit errorOccurred(tr("工序已变化，请重新打开编辑窗口。")); return;
        }
        working.params = slotCuttingParams(dialog.params());
        working.params.set(QStringLiteral("workOffset"), m_project->workOffset().mid(1).toInt());
        working.toolId = tools->currentData().toInt();
        OperationProposal proposal; proposal.kind = OperationProposalKind::Contour;
        proposal.strategyId = working.strategyId; proposal.toolId = working.toolId; proposal.params = working.params;
        proposal.contourFeature = working.contourFeature; proposal.selectionEvidence = working.selectionEvidence;
        const QString error = validate(proposal);
        if (toolsChanged || context != contextKey() || !locate(original.contourFeature)) {
            emit errorOccurred(tr("确认期间模型、Setup、原对象或刀具已变化，请重新打开编辑窗口。")); return;
        }
        if (!error.isEmpty()) {
            validationMessage->setText(error);
            validationMessage->show();
            emit errorOccurred(error);
            if (toolsChanged || context != contextKey() || !locate(original.contourFeature)) return;
            continue;
        }
        if (dialog.params().values == params.values && working.toolId == original.toolId &&
            working.selectionEvidence == original.selectionEvidence) {
            QString conflict;
            if (!m_operations->replaceSlotOperation(original, original, &conflict)) emit errorOccurred(conflict);
            return;
        }
        working.featureRef = QStringLiteral("%1 L%2 W%3 D%4 / Z-%5").arg(working.strategyId)
            .arg(working.contourFeature.length, 0, 'f', 1).arg(working.contourFeature.width, 0, 'f', 1)
            .arg(working.contourFeature.depth, 0, 'f', 1).arg(working.params.get(QStringLiteral("depth")), 0, 'f', 1);
        working.markToolpathValid();
        QString conflict;
        if (!m_operations->replaceSlotOperation(original, working, &conflict)) emit errorOccurred(conflict);
        return;
    }
}
