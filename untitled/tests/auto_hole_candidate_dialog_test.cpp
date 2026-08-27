#include "../src/ui/AutoHoleCandidateDialog.h"

#include <QApplication>
#include <QCheckBox>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

AutoHoleCandidate candidate(AutoCandidateState state, const QString &reason = {})
{
    AutoHoleCandidate value;
    value.id = QStringLiteral("candidate-1");
    value.geometryRef = QStringLiteral("feature:stable-hole");
    value.holeFeature.radius = 3.0;
    value.holeFeature.depth = 12.0;
    value.state = state;
    if (!reason.isEmpty()) {
        value.reasons.append(reason);
    }
    AutoHolePlan plan;
    plan.id = QStringLiteral("blind-drill@1");
    plan.strategyId = QStringLiteral("hole_peck");
    plan.toolId = 7;
    value.plans.append(plan);
    return value;
}

AutoHoleCandidate countersinkCandidate()
{
    AutoHoleCandidate value = candidate(AutoCandidateState::Draft);
    value.compoundHole.geometryRef = value.geometryRef;
    value.compoundHole.layers = {
        {HoleLayerKind::ConicalCountersink, 10.0, 0.0, -2.0, false},
        {HoleLayerKind::Cylindrical, 6.0, -2.0, -12.0, false}
    };
    return value;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Q_UNUSED(app);

    AutoHoleCandidateDialog dialog;
    dialog.setChineseUi(true);
    dialog.setCandidates({
        candidate(AutoCandidateState::Draft),
        candidate(AutoCandidateState::Rejected, QStringLiteral("无匹配方案"))
    });
    if (!expect(dialog.candidateCount() == 2 &&
                    dialog.rejectionText(1).contains(QStringLiteral("无匹配方案")),
                "candidate and rejection evidence should be visible")) {
        return 1;
    }
    if (!expect(dialog.selectCandidate(1) && !dialog.canConfirm(),
                "rejected candidates must disable confirmation")) {
        return 1;
    }
    if (!expect(dialog.selectCandidate(0) && !dialog.canConfirm(),
                "draft candidates require an explicit plan selection")) {
        return 1;
    }
    if (!expect(dialog.selectPlan(QStringLiteral("blind-drill@1")) &&
                    dialog.canConfirm() && dialog.confirm(),
                "a current draft with an explicit plan should be confirmable")) {
        return 1;
    }
    if (!expect(dialog.selectedCandidate().selectedPlanId ==
                    QStringLiteral("blind-drill@1"),
                "the selected plan should be returned to the service layer")) {
        return 1;
    }

    dialog.setCandidates({countersinkCandidate()});
    auto *cancelCountersink = dialog.findChild<QCheckBox*>(
        QStringLiteral("cancelCountersinkCheck"));
    if (!expect(cancelCountersink != nullptr && !cancelCountersink->isHidden() &&
                    cancelCountersink->isEnabled(),
                "countersink candidates should expose an enabled cancel option")) {
        return 1;
    }
    cancelCountersink->setChecked(true);
    if (!expect(dialog.selectedCandidate().cancelCountersink,
                "the cancel option should remain attached to the selected draft")) {
        return 1;
    }

    return 0;
}
