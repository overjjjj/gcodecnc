#include "../operations/MachiningInput.h"
#include "../selection/PocketOpening.h"
#include "../../../app/AppController.h"
#include "../../../core/ProjectManager.h"
#include "../../../core/MachiningCoordinates.h"
#include "OperationEditDialog.h"
#include "OperationParameterGroups.h"
#include "ToolOperationCompatibility.h"
#include "../../../tool/ToolLibrary.h"
#include "../../../strategies/hole/HoleStrategyUtils.h"
#include "../../../strategies/mill/PlanarMachiningValidation.h"
#include "../../../strategies/mill/ContourSideGeometry.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
QString parameterName(const QString &key)
{
    static const QMap<QString, QString> names = {
        {"depth", "加工深度"}, {"peckDepth", "单次深度 Q"}, {"stepDown", "每层切深"},
        {"stepover", "步距"}, {"safeHeight", "安全高度"}, {"feedHeight", "进给起始高度"},
        {"retractHeight", "退刀高度"}, {"spindleSpeed", "主轴转速"}, {"feedRate", "进给速度"},
        {"plungeRate", "下刀速度"}, {"dwellTime", "底部停留"}, {"pitch", "螺距"},
        {"chamferMode", "倒角方式"}, {"axialDepth", "轴向 G81 深度（圆周方式不使用，用户确认）"},
        {"chamferWidth", "圆周倒角宽度（轴向方式不使用）"},
        {"chamferAngle", "圆周倒角角度（轴向方式不使用）"}, {"compensation", "刀补方向"},
        {"camOffsetSide", "CAM偏置侧（由选择方向确定）"},
        {"sideStockToLeave", "侧壁余量"}, {"floorStockToLeave", "底面余量"}, {"stockToLeave", "余量"},
        {"leadLength", "圆周引入引出长度（轴向方式不使用）"}, {"slotLength", "槽长"}, {"slotWidth", "槽宽"},
        {"angle", "方向角"}, {"helixRadius", "螺旋下刀半径"}, {"helixPitch", "螺旋节距"},
        {"finishStock", "斜面精修余量"}, {"slopeDirection", "斜面方向"},
        {"coolantMode", "冷却方式"}, {"depthMode", "深度模式"}, {"threadHandedness", "螺纹旋向"},
        {"tapRetract", "攻丝回退量"}, {"chipBreakRetract", "断屑回退量"}, {"toolTipRadius", "倒角刀尖半径"},
        {"cutDirection", "平面走刀方向"}, {"linkMode", "连接方式"}, {"boundingRectangle", "外接矩形范围"},
        {"pathPattern", "型腔路径方式"}, {"keepIslands", "保留岛屿"}, {"overcut", "轮廓过切量"},
        {"startAtMidpoint", "长直线中点起刀"}, {"retractDelta", "回退量"}, {"fullRetractEvery", "全退间隔"}
    };
    return names.value(key, key);
}

QString processName(const QString &id)
{
    static const QMap<QString, QString> names = {
        {"hole_spot", "定点钻"}, {"hole_peck", "排屑钻"}, {"hole_deephole", "深孔钻 G83"},
        {"hole_g73", "高速断屑钻 G73"},
        {"hole_circular_mill", "圆插补扩孔"}, {"hole_reaming", "铰孔"}, {"hole_chamfer", "孔口倒角"},
        {"hole_tapping", "攻丝"}, {"mill_face", "铣平面"}, {"mill_surface_finish", "精面铣"},
        {"mill_pocket_rough", "型腔开粗"}, {"mill_pocket_finish", "型腔侧壁精加工"},
        {"mill_pocket_floor_finish", "型腔底面精加工"}, {"mill_closed_contour", "封闭轮廓铣"},
        {"mill_open_contour", "开放轮廓铣"}, {"mill_slot", "开口槽铣"},
        {"mill_blind_slot", "盲槽铣"}, {"mill_tapered_slot", "斜底槽铣"}
    };
    return names.value(id, id);
}
}

OperationEditDialog::OperationEditDialog(const MachiningOperation &operation, QWidget *parent)
    : QDialog(parent), m_original(operation), m_resultParams(operation.params), m_resultToolId(operation.toolId)
{
    setObjectName(QStringLiteral("operationEditDialog"));
    setWindowTitle(processName(operation.strategyId) + QStringLiteral(" · 编辑工序参数"));
    setModal(false);
    resize(490, 640);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);
    auto *title = new QLabel(windowTitle(), this);
    QFont font = title->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 2);
    title->setFont(font);
    layout->addWidget(title);
    auto *geometry = new QLabel(QStringLiteral("几何：%1\n工件坐标系：G%2")
        .arg(operation.featureRef.isEmpty() ? operation.geometryRefs.join(QStringLiteral("、")) : operation.featureRef)
        .arg(operation.params.get(QStringLiteral("workOffset"), 54)), this);
    geometry->setWordWrap(true);
    layout->addWidget(geometry);
    auto *form = new QFormLayout;
    m_tools = new QComboBox(this);
    m_tools->setObjectName(QStringLiteral("operationEditTool"));
    // Show real catalogue entries. Compatibility is checked again with the edited depth on acceptance.
    for (const ToolEntry &tool : ToolLibrary::instance().allTools()) {
        const auto report = operation.opType == OperationType::Hole
            ? reviewToolCompatibility(operation.strategyId, tool, operation.holeFeature, true,
                                      holeCuttingDepth(operation.strategyId, operation.holeFeature, operation.params, tool))
            : reviewToolCompatibility(operation.strategyId, tool, operation.contourFeature, true,
                                      operation.strategyId == "mill_pocket_rough" ? 0.0 : -1.0);
        // Pocket reach is checked against the edited depth on acceptance.
        // A different cone angle can become compatible after editing the angle.
        if (!report.hasBlockingIssues() || tool.id == operation.toolId ||
            (operation.strategyId == "hole_chamfer" && tool.type == "chamfer_mill"))
            m_tools->addItem(QStringLiteral("T%1 %2 Ø%3").arg(tool.id).arg(tool.name).arg(tool.diameter), tool.id);
    }
    m_tools->setCurrentIndex(m_tools->findData(operation.toolId));
    form->addRow(QStringLiteral("刀具"), m_tools);
    layout->addLayout(form);
    m_table = new QTableWidget(0, 2, this);
    m_table->hide();
    auto editableValues = operation.params.values;
    if (operation.strategyId == "mill_pocket_rough") {
        if (!editableValues.contains("floorStockToLeave")) editableValues.insert("floorStockToLeave", 0.0);
        if (editableValues.contains("sideStockToLeave")) editableValues.remove("stockToLeave");
    }
    for (auto it = editableValues.cbegin(); it != editableValues.cend(); ++it) {
        if (it.key() == QStringLiteral("entryMode")) continue;
        bool numeric = false;
        it.value().toDouble(&numeric);
        if (!numeric) continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *name = new QTableWidgetItem(operation.strategyId == "mill_pocket_rough" && it.key() == "stockToLeave"
            ? QStringLiteral("侧壁余量") : parameterName(it.key()));
        name->setData(Qt::UserRole, it.key());
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, new QTableWidgetItem(QString::number(it.value().toDouble(), 'g', 17)));
    }
    auto *tabs = new QTabWidget(this);
    rebuildOperationParameterGroups(tabs, m_table, operation.strategyId);
    layout->addWidget(tabs, 1);
    if (operation.strategyId == "hole_peck") {
        auto *peck = new QLabel(this);
        peck->setObjectName("effectivePeckSummary");
        peck->setWordWrap(true);
        const auto updatePeck = [this, peck] { peck->setText(peckDepthSummary(m_original.holeFeature, draftParams())); };
        connect(m_table, &QTableWidget::cellChanged, this, updatePeck);
        updatePeck();
        layout->addWidget(peck);
    }
    // Entry is retained because changing it can require a separate plunge confirmation.
    if (operation.params.values.contains(QStringLiteral("entryMode"))) {
        layout->addWidget(new QLabel(operation.params.get(QStringLiteral("entryMode")) == 0
            ? QStringLiteral("下刀方式：垂直下刀（保留原设置）")
            : QStringLiteral("下刀方式：螺旋下刀（保留原设置）"), this));
    }
    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("operationEditError"));
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: #b42318;"));
    layout->addWidget(m_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认修改"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &OperationEditDialog::accept);
    layout->addWidget(buttons);
}

StrategyParams OperationEditDialog::draftParams() const
{
    StrategyParams params = m_original.params;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QString key = m_table->item(row, 0)->data(Qt::UserRole).toString();
        const double value = m_table->item(row, 1)->text().toDouble();
        if (value != m_original.params.get(key)) params.set(key, value);
    }
    return params;
}

QString operationEditError(const MachiningOperation &operation, const StrategyParams &params, int toolId)
{
    const QString spindleError = holeSpindleOutputError(operation.strategyId, params);
    if (!spindleError.isEmpty()) return spindleError;
    const QSet<QString> positive = {"depth", "peckDepth", "stepDown", "stepover", "spindleSpeed", "feedRate",
        "plungeRate", "pitch", "axialDepth", "chamferWidth", "chamferAngle", "slotLength", "slotWidth"};
    const QSet<QString> nonnegative = {"sideStockToLeave", "floorStockToLeave", "stockToLeave", "finishStock",
        "leadLength", "helixRadius", "helixPitch", "dwellTime"};
    for (auto it = params.values.cbegin(); it != params.values.cend(); ++it) {
        bool numeric = false;
        const double value = it.value().toDouble(&numeric);
        const auto contract = operationParameterContract(operation.strategyId, it.key());
        if (it.value() != operation.params.values.value(it.key()) && !contract.editable)
            return QStringLiteral("%1 当前不可编辑。").arg(parameterName(it.key()));
        if (contract.editable && ((!contract.options.isEmpty() && !numeric)
            || (numeric && !contract.accepts(value))))
            return QStringLiteral("%1 的数值或选项无效。").arg(parameterName(it.key()));
        if (((positive.contains(it.key()) || nonnegative.contains(it.key())) && !numeric) ||
            (numeric && (!std::isfinite(value) || (positive.contains(it.key()) && value <= 0)
            || (nonnegative.contains(it.key()) && value < 0))))
            return QStringLiteral("%1 的数值无效。").arg(parameterName(it.key()));
    }
    const ToolEntry tool = ToolLibrary::instance().tool(toolId);
    const auto feature=applyMachiningParameters(operation.contourFeature,params);
    if(operation.opType != OperationType::Hole) {
        const QString sideError=contourSideOperationError(operation.selectionEvidence,feature,params,operation.strategyId);
        if(!sideError.isEmpty())return sideError;
        const auto *project=AppController::instance().projectManager();
        if(operation.selectionEvidence.explicitUserSelection && project){
            const auto access=evaluateSelectionAccess(operation.selectionEvidence,project->sourceFileFingerprint(),project->setupFingerprint(),
                QString("G%1").arg(params.get("workOffset",54)),machiningRequirements(operation.strategyId).closedBoundary);
            if(!access.ok())return access.issueCode;
        }
        const QString error=pocketOpeningOperationError(operation.selectionEvidence,feature,params,
            operation.strategyId,project ? &project->mesh() : nullptr);
        if(!error.isEmpty())return error;
    }
    const auto origin=machiningOrigin(AppController::instance().projectManager());
    const auto heightError=operation.opType==OperationType::Hole
        ? workHeightError(inWorkCoordinates(operation.holeFeature,origin),params,operation.strategyId)
        : workHeightError(inWorkCoordinates(feature,origin),params);
    if (!heightError.isEmpty()) return heightError;
    if (isPlanarMillingStrategy(operation.strategyId)) {
        return planarMachiningError(inWorkCoordinates(feature,
            machiningOrigin(AppController::instance().projectManager())),tool,params);
    }
    const double chamferMode = params.get(QStringLiteral("chamferMode"), 0.0);
    if (operation.strategyId == QStringLiteral("hole_chamfer") &&
        chamferMode != 0.0 && chamferMode != 1.0)
        return QStringLiteral("倒角方式只能选择圆周 G1/G2 或轴向 G81。");
    const auto report = operation.opType == OperationType::Hole
        ? reviewToolCompatibility(operation.strategyId, tool, operation.holeFeature, true,
                                  holeCuttingDepth(operation.strategyId, operation.holeFeature, params, tool))
        : reviewToolCompatibility(operation.strategyId, tool, feature, true, params.get("depth", -1));
    for (const auto &issue : report.issues)
        if (issue.severity == ToolCompatibilitySeverity::Blocking) return issue.message;
    return {};
}

QString operationEditParameterLabel(const QString &key)
{
    return parameterName(key);
}

QString OperationEditDialog::validationError(const StrategyParams &params) const
{
    return operationEditError(m_original, params, m_tools->currentData().toInt());
}

void OperationEditDialog::accept()
{
    const StrategyParams draft = draftParams();
    const QString error = validationError(draft);
    m_error->setText(error);
    if (!error.isEmpty()) return;
    m_resultParams = draft;
    m_resultToolId = m_tools->currentData().toInt();
    QDialog::accept();
}
