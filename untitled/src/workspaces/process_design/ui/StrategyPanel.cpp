#include "../../shared/ui/OperationMessageBox.h"
#include "../operations/MachiningInput.h"
#include "StrategyPanel.h"
#include "../../../app/AppController.h"
#include "../../../core/MachiningCoordinates.h"

#include "CircleMillDialog.h"
#include "ParameterEditorDialog.h"
#include "OperationParameterGroups.h"
#include "ProcessParameterTemplateStore.h"
#include "MaterialLibraryDialog.h"
#include "ToolOperationCompatibility.h"
#include "../../../core/Settings.h"
#include "../../../core/SetupOrientation.h"
#include "../../../strategies/StrategyFactory.h"
#include "../../../strategies/mill/PlanarMachiningValidation.h"
#include "../../../strategies/hole/HoleStrategyUtils.h"
#include "../../../tool/ToolLibrary.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QListWidget>
#include <QSet>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>
#include <cmath>

namespace {

static bool requiresPocketEntry(const QString &strategy, const MachiningFeature &feature)
{
    return strategy=="mill_sloped_floor" || strategy=="mill_pocket_rough" || (feature.subType=="verified_slot_opening" &&
        (strategy=="mill_pocket_finish" || strategy=="mill_pocket_floor_finish"));
}

// 函数说明：isChineseUi负责当前模块的核心处理。
static bool isChineseUi()
{
    return Settings::instance().language() == QStringLiteral("zh_CN");
}

// 函数说明：isSlotFeature负责当前模块的核心处理。
static bool isSlotFeature(const MachiningFeature &feature)
{
    return feature.kind == FeatureKind::Slot ||
           feature.subType == QStringLiteral("open_slot") ||
           feature.subType == QStringLiteral("blind_slot") ||
           feature.subType == QStringLiteral("straight_slot") ||
           feature.subType == QStringLiteral("arc_slot");
}

// 函数说明：isFlatSurfaceFeature负责当前模块的核心处理。
static bool isFlatSurfaceFeature(const MachiningFeature &feature)
{
    return feature.kind == FeatureKind::FlatSurface;
}

// 函数说明：isPocketFeature负责当前模块的核心处理。
static bool isPocketFeature(const MachiningFeature &feature)
{
    return feature.kind == FeatureKind::Pocket;
}

// 函数说明：isSupportedPocketFeature负责当前模块的核心处理。
static bool isSupportedPocketFeature(const MachiningFeature &feature)
{
    return isPocketFeature(feature)
        && (feature.subType == QStringLiteral("rectangular_pocket")
            || feature.subType == QStringLiteral("circular_pocket")
            || feature.boundaryPoints.size() >= 3);
}

// 函数说明：preferredSlotStrategy负责当前模块的核心处理。
static QString preferredSlotStrategy(const MachiningFeature &feature)
{
    if (feature.region == FaceRegion::Front &&
        std::abs(feature.axis.z()) < 0.5f &&
        feature.subType == QStringLiteral("straight_slot")) {
        return QStringLiteral("mill_tapered_slot");
    }
    return feature.subType == QStringLiteral("open_slot")
               ? QStringLiteral("mill_slot")
               : QStringLiteral("mill_blind_slot");
}

// 函数说明：strategyDisplayName负责当前模块的核心处理。
static QString strategyDisplayName(const QString &strategyId, bool zh)
{
    if(strategyId=="mill_sloped_floor")return zh?QStringLiteral("斜面底面可达区"):QStringLiteral("Accessible sloped floor");
    if (zh) {
        if (strategyId == QStringLiteral("hole_spot")) return QStringLiteral("定点钻");
        if (strategyId == QStringLiteral("hole_g81")) return QStringLiteral("钻孔 G81");
        if (strategyId == QStringLiteral("hole_g76")) return QStringLiteral("镗孔 G76");
        if (strategyId == QStringLiteral("hole_g86")) return QStringLiteral("停主轴镗孔 G86");
        if (strategyId == QStringLiteral("hole_peck")) return QStringLiteral("排屑钻");
        if (strategyId == QStringLiteral("hole_deephole")) return QStringLiteral("深孔钻");
        if (strategyId == QStringLiteral("hole_g73")) return QStringLiteral("高速断屑钻 G73");
        if (strategyId == QStringLiteral("hole_circular_mill")) return QStringLiteral("圆插补扩孔");
        if (strategyId == QStringLiteral("hole_reaming")) return QStringLiteral("铰孔");
        if (strategyId == QStringLiteral("hole_chamfer")) return QStringLiteral("孔口倒角");
        if (strategyId == QStringLiteral("hole_tapping")) return QStringLiteral("攻丝");
        if (strategyId == QStringLiteral("mill_face")) return QStringLiteral("铣平面");
        if (strategyId == QStringLiteral("mill_surface_finish")) return QStringLiteral("精面铣");
        if (strategyId == QStringLiteral("mill_pocket_rough")) return QStringLiteral("型腔开粗");
        if (strategyId == QStringLiteral("mill_pocket_finish")) return QStringLiteral("型腔侧壁精加工");
        if (strategyId == QStringLiteral("mill_pocket_floor_finish")) return QStringLiteral("型腔底面精加工");
        if (strategyId == QStringLiteral("mill_closed_contour")) return QStringLiteral("封闭轮廓铣");
        if (strategyId == QStringLiteral("mill_open_contour")) return QStringLiteral("开放轮廓铣");
        if (strategyId == QStringLiteral("mill_slot")) return QStringLiteral("开口槽铣");
        if (strategyId == QStringLiteral("mill_blind_slot")) return QStringLiteral("盲槽铣");
        if (strategyId == QStringLiteral("mill_tapered_slot")) return QStringLiteral("斜底槽铣");
    } else {
        if (strategyId == QStringLiteral("hole_spot")) return QStringLiteral("Spot Drilling");
        if (strategyId == QStringLiteral("hole_g81")) return QStringLiteral("G81 Drilling");
        if (strategyId == QStringLiteral("hole_g76")) return QStringLiteral("G76 Boring");
        if (strategyId == QStringLiteral("hole_g86")) return QStringLiteral("G86 Boring");
        if (strategyId == QStringLiteral("hole_peck")) return QStringLiteral("Peck Drilling");
        if (strategyId == QStringLiteral("hole_deephole")) return QStringLiteral("Deep-Hole Drilling");
        if (strategyId == QStringLiteral("hole_g73")) return QStringLiteral("High-Speed Peck Drilling G73");
        if (strategyId == QStringLiteral("hole_circular_mill")) return QStringLiteral("Circular Hole Milling");
        if (strategyId == QStringLiteral("hole_reaming")) return QStringLiteral("Reaming");
        if (strategyId == QStringLiteral("hole_chamfer")) return QStringLiteral("Hole Chamfering");
        if (strategyId == QStringLiteral("hole_tapping")) return QStringLiteral("Tapping");
        if (strategyId == QStringLiteral("mill_face")) return QStringLiteral("Face Milling");
        if (strategyId == QStringLiteral("mill_surface_finish")) return QStringLiteral("Surface Finish");
        if (strategyId == QStringLiteral("mill_pocket_rough")) return QStringLiteral("Pocket Roughing");
        if (strategyId == QStringLiteral("mill_pocket_finish")) return QStringLiteral("Pocket Wall Finish");
        if (strategyId == QStringLiteral("mill_pocket_floor_finish")) return QStringLiteral("Pocket Floor Finish");
        if (strategyId == QStringLiteral("mill_closed_contour")) return QStringLiteral("Closed Contour Milling");
        if (strategyId == QStringLiteral("mill_open_contour")) return QStringLiteral("Open Contour Milling");
        if (strategyId == QStringLiteral("mill_slot")) return QStringLiteral("Open Slot Milling");
        if (strategyId == QStringLiteral("mill_blind_slot")) return QStringLiteral("Blind Slot Milling");
        if (strategyId == QStringLiteral("mill_tapered_slot")) return QStringLiteral("Tapered Slot Milling");
    }

    auto strategy = StrategyFactory::instance().strategy(strategyId);
    return strategy ? strategy->displayName() : strategyId;
}

// 函数说明：formatParamValue负责当前模块的核心处理。
static QString formatParamValue(double value)
{
    const double rounded = std::round(value);
    if (std::abs(value - rounded) <= 1.0e-4) {
        return QString::number(rounded, 'f', 0);
    }
    return QString::number(value, 'f', 3);
}

// 函数说明：basicParameterKeys负责当前模块的核心处理。
static QSet<QString> basicParameterKeys()
{
    return {
        QStringLiteral("depth"),
        QStringLiteral("axialDepth"),
        QStringLiteral("chamferMode"),
        QStringLiteral("safeHeight"),
        QStringLiteral("feedHeight"),
        QStringLiteral("stepDown"),
        QStringLiteral("spindleSpeed"),
        QStringLiteral("feedRate"),
        QStringLiteral("slotLength"),
        QStringLiteral("slotWidth")
    };
}

// 函数说明：nonNegativeParameterKeys负责当前模块的核心处理。
static QSet<QString> nonNegativeParameterKeys()
{
    return {
        QStringLiteral("sideStockToLeave"),
        QStringLiteral("floorStockToLeave"),
        QStringLiteral("stockToLeave"),
        QStringLiteral("finishStock"),
        QStringLiteral("leadLength"),
        QStringLiteral("helixPitch"),
        QStringLiteral("helixRadius"),
        QStringLiteral("compensation"),
        QStringLiteral("dwellTime"),
        QStringLiteral("angle")
    };
}

// 函数说明：unconstrainedParameterKeys负责当前模块的核心处理。
static QSet<QString> unconstrainedParameterKeys()
{
    return {
        QStringLiteral("angle"),
        QStringLiteral("compensation"),
        QStringLiteral("slopeDirection"),
        QStringLiteral("chamferMode")
    };
}

// 函数说明：applyManualProcessParameters负责当前模块的核心处理。
static void applyManualProcessParameters(const QString &strategyId, StrategyParams &params)
{
    // These values are retained with the operation as process-setting metadata.
    // They make the Chapter 7 review fields visible before all controller outputs
    // consume them; existing toolpath-specific parameters remain authoritative.
    if (!params.values.contains(QStringLiteral("workOffset"))) {
        params.set(QStringLiteral("workOffset"), 54.0);
    }
    if (!params.values.contains(QStringLiteral("coolantMode"))) {
        params.set(QStringLiteral("coolantMode"), 1.0);
    }
    if (!params.values.contains(QStringLiteral("depthMode"))) {
        params.set(QStringLiteral("depthMode"), 0.0);
    }

    if (strategyId == QStringLiteral("hole_tapping")) {
        params.set(QStringLiteral("threadHandedness"), 1.0);
        params.set(QStringLiteral("tapRetract"), 0.0);
        params.set(QStringLiteral("chipBreakRetract"), 0.0);
    } else if (strategyId == QStringLiteral("hole_chamfer")) {
        params.set(QStringLiteral("toolTipRadius"), 0.0);
    } else if (strategyId == QStringLiteral("hole_peck") ||
               strategyId == QStringLiteral("hole_deephole")) {
        params.set(QStringLiteral("retractDelta"), 0.0);
        params.set(QStringLiteral("fullRetractEvery"), 1.0);
    } else if (strategyId == QStringLiteral("mill_face") ||
               strategyId == QStringLiteral("mill_surface_finish")) {
        params.set(QStringLiteral("cutDirection"), 0.0);
        params.set(QStringLiteral("linkMode"), 0.0);
        params.set(QStringLiteral("boundingRectangle"), 0.0);
    } else if (strategyId == QStringLiteral("mill_pocket_rough") ||
               strategyId == QStringLiteral("mill_pocket_finish") ||
               strategyId == QStringLiteral("mill_pocket_floor_finish")) {
        params.set(QStringLiteral("pathPattern"), 0.0);
        params.set(QStringLiteral("keepIslands"), 1.0);
    } else if (strategyId == QStringLiteral("mill_closed_contour") ||
               strategyId == QStringLiteral("mill_open_contour")) {
        params.set(QStringLiteral("overcut"), 0.0);
        params.set(QStringLiteral("startAtMidpoint"), 0.0);
    }
}

} // namespace

StrategyPanel::StrategyPanel(QWidget *parent)
    : QWidget(parent)
    , m_proposalTitleLabel(new QLabel(this))
    , m_proposalStateLabel(new QLabel(this))
    , m_featureLabel(new QLabel(this))
    , m_strategyLabel(new QLabel(this))
    , m_toolLabel(new QLabel(this))
    , m_entryModeLabel(new QLabel(this))
    , m_hintLabel(new QLabel(this))
    , m_toolFitLabel(new QLabel(this))
    , m_paramSummaryLabel(new QLabel(this))
    , m_strategyCombo(new QComboBox(this))
    // 函数说明：m_toolCombo负责当前模块的核心处理。
    , m_toolCombo(new QComboBox(this))
    // 函数说明：m_entryModeCombo负责当前模块的核心处理。
    , m_entryModeCombo(new QComboBox(this))
    // 函数说明：m_paramTable负责当前模块的核心处理。
    , m_paramTable(new QTableWidget(this))
    // 函数说明：m_editHoleParamsBtn负责当前模块的核心处理。
    , m_editHoleParamsBtn(new QPushButton(this))
    // 函数说明：m_confirmOperationBtn负责当前模块的核心处理。
    , m_confirmOperationBtn(new QPushButton(this))
    // 函数说明：m_millingGroup负责当前模块的核心处理。
    , m_millingGroup(new QGroupBox(this))
    // 函数说明：m_circleMillBtn负责当前模块的核心处理。
    , m_circleMillBtn(new QPushButton(this))
{
    setObjectName(QStringLiteral("strategyPanel"));
    setStyleSheet(QStringLiteral(
        "#strategyPanel { background: #ffffff; }"
        "#strategyPanel QLabel { color: #172033; }"
        "#strategyPanel QComboBox, #strategyPanel QPushButton { min-height: 34px; }"
        "#strategyPanel QComboBox { border: 1px solid #cfd7e6; border-radius: 6px; padding: 4px 8px; background: #ffffff; }"
        "#strategyPanel QPushButton { border: 1px solid #c6d0df; border-radius: 6px; padding: 6px 12px; background: #ffffff; color: #27364d; font-weight: 500; }"
        "#strategyPanel QPushButton:hover { background: #f3f6fb; border-color: #9fb0c8; }"
        "#strategyPanel QPushButton#strategyPrimaryAction { border-color: #2f6fec; background: #2f6fec; color: #ffffff; font-weight: 600; }"
        "#strategyPanel QPushButton#strategyPrimaryAction:hover { background: #245fd0; }"
        "#strategyPanel QPushButton#strategyPrimaryAction:disabled { background: #b8c6e3; border-color: #b8c6e3; color: #f8fbff; }"
        "#strategyPanel QLabel#proposalTitleLabel { color: #172033; font-size: 15px; font-weight: 700; }"
        "#strategyPanel QLabel#proposalStateLabel { background: #eef4ff; border: 1px solid #cbd9f4; border-radius: 6px; color: #294f91; padding: 7px 9px; }"
        "#strategyPanel QLabel#proposalStateLabel[warning=\"true\"] { background: #fff5e8; border-color: #efc789; color: #7b4900; }"
        "#strategyPanel QLabel#strategyFeatureLabel { background: #f4f7fb; border: 1px solid #dde5f0; border-radius: 8px; padding: 10px 12px; }"
        "#strategyPanel QLabel#strategyHintLabel { color: #5c6b82; padding: 2px 2px 6px 2px; }"
        "#strategyPanel QLabel#toolFitLabel { background: #eef8f2; border: 1px solid #b8ddc5; border-radius: 6px; color: #24633a; padding: 7px 9px; }"
        "#strategyPanel QLabel#toolFitLabel[state=warning] { background: #fff5e8; border-color: #efc789; color: #7b4900; }"
        "#strategyPanel QLabel#toolFitLabel[state=blocking] { background: #fff0f0; border-color: #e4aaaa; color: #8c2626; }"
        "#strategyPanel QWidget#paramSummaryPanel { background: #f8faff; border: 1px solid #dbe4f2; border-radius: 8px; }"
        "#strategyPanel QLabel#paramSummaryLabel { color: #42526b; padding: 6px 0; }"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    m_proposalTitleLabel->setObjectName(QStringLiteral("proposalTitleLabel"));
    m_proposalStateLabel->setObjectName(QStringLiteral("proposalStateLabel"));
    m_proposalStateLabel->setWordWrap(true);
    layout->addWidget(m_proposalTitleLabel);
    layout->addWidget(m_proposalStateLabel);
    m_featureLabel->setObjectName(QStringLiteral("strategyFeatureLabel"));
    layout->addWidget(m_featureLabel);

    auto *formLayout = new QHBoxLayout;
    formLayout->addWidget(m_strategyLabel);
    formLayout->addWidget(m_strategyCombo, 1);
    layout->addLayout(formLayout);

    auto *templateLayout = new QHBoxLayout;
    m_templateLabel = new QLabel(this);
    m_templateCombo = new QComboBox(this);
    m_templateCombo->setObjectName(QStringLiteral("processTemplateCombo"));
    m_templateCombo->setMinimumContentsLength(12);
    m_templateCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_applyTemplateButton = new QPushButton(this);
    m_applyTemplateButton->setObjectName(QStringLiteral("applyProcessTemplateButton"));
    m_saveTemplateButton = new QPushButton(this);
    m_saveTemplateButton->setObjectName(QStringLiteral("saveProcessTemplateButton"));
    templateLayout->addWidget(m_templateLabel);
    templateLayout->addWidget(m_templateCombo, 1);
    templateLayout->addWidget(m_applyTemplateButton);
    templateLayout->addWidget(m_saveTemplateButton);
    layout->addLayout(templateLayout);
    m_templateStatus = new QLabel(this);
    m_templateStatus->setWordWrap(true);
    layout->addWidget(m_templateStatus);
    connect(m_applyTemplateButton, &QPushButton::clicked, this, &StrategyPanel::applySelectedTemplate);
    connect(m_saveTemplateButton, &QPushButton::clicked, this, &StrategyPanel::saveCurrentTemplate);
    connect(m_templateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_applyTemplateButton->setEnabled((m_hasFeature || m_hasContourFeature)
                                         && !m_templateCombo->currentData().toString().isEmpty());
    });

    m_hintLabel->setObjectName(QStringLiteral("strategyHintLabel"));
    m_hintLabel->setWordWrap(true);
    layout->addWidget(m_hintLabel);

    auto *toolLayout = new QHBoxLayout;
    toolLayout->addWidget(m_toolLabel);
    toolLayout->addWidget(m_toolCombo, 1);
    m_materialSuggestionButton = new QPushButton(this);
    m_materialSuggestionButton->setObjectName(QStringLiteral("materialSuggestionButton"));
    toolLayout->addWidget(m_materialSuggestionButton);
    connect(m_materialSuggestionButton, &QPushButton::clicked, this, &StrategyPanel::reviewMaterialSuggestion);
    layout->addLayout(toolLayout);
    m_toolFitLabel->setObjectName(QStringLiteral("toolFitLabel"));
    m_toolFitLabel->setWordWrap(true);
    layout->addWidget(m_toolFitLabel);

    auto *entryLayout = new QHBoxLayout;
    entryLayout->addWidget(m_entryModeLabel);
    entryLayout->addWidget(m_entryModeCombo, 1);
    layout->addLayout(entryLayout);
    m_entryModeCombo->addItem(QString(), -1);
    m_entryModeCombo->addItem(QString(), 0);
    m_entryModeCombo->addItem(QString(), 1);
    m_entryModeLabel->setVisible(false);
    m_entryModeCombo->setVisible(false);

    m_paramTable->setColumnCount(2);
    m_paramTable->horizontalHeader()->setStretchLastSection(true);
    m_paramTable->verticalHeader()->setVisible(false);
    m_paramTable->setVisible(false);

    auto *paramSummaryPanel = new QWidget(this);
    paramSummaryPanel->setObjectName(QStringLiteral("paramSummaryPanel"));
    auto *paramSummaryLayout = new QVBoxLayout(paramSummaryPanel);
    paramSummaryLayout->setContentsMargins(12, 10, 12, 10);
    paramSummaryLayout->setSpacing(8);
    m_paramSummaryLabel->setObjectName(QStringLiteral("paramSummaryLabel"));
    m_paramSummaryLabel->setWordWrap(true);
    paramSummaryLayout->addWidget(m_paramSummaryLabel);
    paramSummaryLayout->addWidget(m_editHoleParamsBtn, 0, Qt::AlignRight);
    layout->addWidget(paramSummaryPanel);

    m_operationTabs = new QTabWidget(this);
    m_operationTabs->setObjectName(QStringLiteral("operationParameterTabs"));
    m_operationTabs->setMinimumHeight(280);
    m_operationTabs->hide();
    layout->addWidget(m_operationTabs, 1);
    m_effectivePeckSummary = new QLabel(this);
    m_effectivePeckSummary->setObjectName(QStringLiteral("effectivePeckSummary"));
    m_effectivePeckSummary->setWordWrap(true);
    m_effectivePeckSummary->setContentsMargins(0, 8, 0, 8);
    m_effectivePeckSummary->setMinimumHeight(48);
    m_effectivePeckSummary->hide();
    layout->addWidget(m_effectivePeckSummary);
    m_reselectButton = new QPushButton(QStringLiteral("返回重选"), this);
    m_reselectButton->setObjectName(QStringLiteral("reselectGeometryButton"));
    m_reselectButton->hide();
    layout->addWidget(m_reselectButton);
    connect(m_reselectButton, &QPushButton::clicked, this, [this]() {
        cacheCurrentParams();
        emit reselectGeometryRequested();
    });

    m_confirmOperationBtn->setObjectName(QStringLiteral("strategyPrimaryAction"));
    layout->addWidget(m_confirmOperationBtn);

    auto *millingLayout = new QVBoxLayout(m_millingGroup);
    millingLayout->setContentsMargins(6, 6, 6, 6);
    millingLayout->addWidget(m_circleMillBtn);
    layout->addWidget(m_millingGroup);
    layout->addStretch(1);

    connect(m_strategyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StrategyPanel::onStrategyChanged);
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_toolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
                updateToolCompatibilityPreview();
                updateProposalState();
            });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_entryModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
                updateParamSummary();
                updateProposalState();
            });
    connect(m_editHoleParamsBtn, &QPushButton::clicked,
            this, &StrategyPanel::onEditHoleParams);
    connect(m_confirmOperationBtn, &QPushButton::clicked,
            this, &StrategyPanel::onConfirmOperation);
    connect(m_circleMillBtn, &QPushButton::clicked,
            this, &StrategyPanel::onCircleMillClicked);

    populateStrategies();
    retranslateUi();
}

// 函数说明：strategyIdsForCurrentContext负责当前模块的核心处理。
QStringList StrategyPanel::strategyIdsForCurrentContext() const
{
    if (m_hasContourFeature) {
        if(m_contourFeature.subType=="verified_sloped_floor")return {QStringLiteral("mill_sloped_floor")};
        if (isFlatSurfaceFeature(m_contourFeature)) {
            return {
                QStringLiteral("mill_face"),
                QStringLiteral("mill_surface_finish"),
                QStringLiteral("mill_closed_contour")
            };
        }

        if (isSlotFeatureSelected()) {
            return {
                QStringLiteral("mill_slot"),
                QStringLiteral("mill_blind_slot"),
                QStringLiteral("mill_tapered_slot")
            };
        }

        if (isPocketFeature(m_contourFeature)) {
            if (!isSupportedPocketFeature(m_contourFeature)) {
                return {};
            }
            return {
                QStringLiteral("mill_pocket_rough"),
                QStringLiteral("mill_pocket_finish"),
                QStringLiteral("mill_pocket_floor_finish"),
                QStringLiteral("mill_closed_contour")
            };
        }

        return {
            QStringLiteral("mill_closed_contour"),
            QStringLiteral("mill_open_contour")
        };
    }

    return {
        QStringLiteral("hole_spot"),
        QStringLiteral("hole_g81"),
        QStringLiteral("hole_g76"),
        QStringLiteral("hole_g86"),
        QStringLiteral("hole_peck"),
        QStringLiteral("hole_deephole"),
        QStringLiteral("hole_g73"),
        QStringLiteral("hole_circular_mill"),
        QStringLiteral("hole_reaming"),
        QStringLiteral("hole_chamfer"),
        QStringLiteral("hole_tapping")
    };
}

// 函数说明：displayNameForStrategy负责当前模块的核心处理。
QString StrategyPanel::displayNameForStrategy(const QString &strategyId) const
{
    return strategyDisplayName(strategyId, isChineseUi());
}

// 函数说明：isSlotFeatureSelected负责当前模块的核心处理。
bool StrategyPanel::isSlotFeatureSelected() const
{
    return m_hasContourFeature && isSlotFeature(m_contourFeature);
}

// 函数说明：populateStrategies负责当前模块的核心处理。
void StrategyPanel::populateStrategies()
{
    const QString previousStrategyId = m_strategyCombo->currentData().toString();
    const QStringList strategyIds = strategyIdsForCurrentContext();

    QSignalBlocker blocker(m_strategyCombo);
    m_strategyCombo->clear();

    for (const QString &id : strategyIds) {
        if (!StrategyFactory::instance().strategy(id)) {
            continue;
        }
        m_strategyCombo->addItem(displayNameForStrategy(id), id);
    }

    selectPreferredStrategyForCurrentContext();
    if (!previousStrategyId.isEmpty() && !isSlotFeatureSelected()) {
        const int previousIndex = m_strategyCombo->findData(previousStrategyId);
        if (previousIndex >= 0) {
            m_strategyCombo->setCurrentIndex(previousIndex);
        }
    }

    if (m_strategyCombo->currentIndex() < 0 && m_strategyCombo->count() > 0) {
        m_strategyCombo->setCurrentIndex(0);
    }

    m_currentStrategyId = m_strategyCombo->currentData().toString();
    blocker.unblock();
    populateTools();
    refreshParamTable();
    updatePocketEntryUi();
    updateStrategyHint();
}

// 函数说明：populateTools负责当前模块的核心处理。
void StrategyPanel::populateTools()
{
    m_toolCombo->clear();
    const QString toolType = preferredToolType(m_strategyCombo->currentData().toString());
    for (const ToolEntry &tool : ToolLibrary::instance().allTools()) {
        if (!toolType.isEmpty() && tool.type != toolType) {
            continue;
        }
        m_toolCombo->addItem(toolDisplayName(tool), tool.id);
    }
    selectPreferredToolForFeature();
    updateToolCompatibilityPreview();
}

// 函数说明：selectPreferredStrategyForCurrentContext负责当前模块的核心处理。
void StrategyPanel::selectPreferredStrategyForCurrentContext()
{
    QString preferred;
    if (m_hasContourFeature) {
        if(m_contourFeature.subType=="verified_sloped_floor") {
            preferred=QStringLiteral("mill_sloped_floor");
        } else if (isFlatSurfaceFeature(m_contourFeature)) {
            preferred = QStringLiteral("mill_face");
        } else if (isSlotFeatureSelected()) {
            preferred = preferredSlotStrategy(m_contourFeature);
        } else if (isSupportedPocketFeature(m_contourFeature)) {
            preferred = QStringLiteral("mill_pocket_rough");
        } else {
            preferred = m_contourFeature.subType == QStringLiteral("open_contour")
                ? QStringLiteral("mill_open_contour")
                : QStringLiteral("mill_closed_contour");
        }
    } else if (m_hasFeature) {
        preferred = QStringLiteral("hole_peck");
        if (m_feature.kind == FeatureKind::Thread ||
            m_feature.subType == QStringLiteral("tapped_hole")) {
            preferred = QStringLiteral("hole_tapping");
        } else if (m_feature.depth > 0.0 && m_feature.radius > 0.0 &&
                   m_feature.depth > m_feature.radius * 8.0) {
            preferred = QStringLiteral("hole_deephole");
        }
    }

    if (preferred.isEmpty()) {
        return;
    }

    const int index = m_strategyCombo->findData(preferred);
    if (index >= 0) {
        m_strategyCombo->setCurrentIndex(index);
    }
}

// 函数说明：onStrategyChanged负责当前模块的核心处理。
void StrategyPanel::onStrategyChanged(int)
{
    cacheCurrentParams();
    m_currentStrategyId = m_strategyCombo->currentData().toString();
    populateTools();
    refreshParamTable();
    updatePocketEntryUi();
    updateStrategyHint();
}

// 中文说明：按默认值、模板值、自动规则、用户修改的优先级解析策略参数，
// 同时保留参数来源，便于程序快照追溯和依赖变化后的失效判断。
StrategyParams StrategyPanel::paramsForStrategy(const QString &strategyId, bool preferUserParams) const
{
    auto strategy = StrategyFactory::instance().strategy(strategyId);
    if (!strategy) {
        return StrategyParams();
    }

    StrategyParams params = (preferUserParams && m_userParams.contains(strategyId))
        ? m_userParams.value(strategyId)
        : strategy->defaultParams();

    applyManualProcessParameters(strategyId, params);
    params.set(QStringLiteral("workOffset"), m_workOffset);

    if (m_hasFeature &&
        (strategyId == QStringLiteral("hole_peck") ||
         strategyId == QStringLiteral("hole_deephole") ||
         strategyId == QStringLiteral("hole_tapping") ||
         strategyId == QStringLiteral("hole_reaming") ||
         strategyId == QStringLiteral("hole_g76") ||
         strategyId == QStringLiteral("hole_g86")) &&
        !params.values.contains(QStringLiteral("depth"))) {
        const double defaultDepth = m_feature.depth > 0.0
            ? m_feature.depth
            : (strategyId == QStringLiteral("hole_deephole") ? 50.0
               : strategyId == QStringLiteral("hole_reaming") ? 20.0
               : strategyId == QStringLiteral("hole_tapping") ? 15.0 : 20.0);
        params.set(QStringLiteral("depth"), defaultDepth);
    }

    if ((!preferUserParams || !m_userParams.contains(strategyId)) &&
        m_hasFeature &&
        strategyId != QStringLiteral("hole_spot") &&
        m_feature.depth > 0.0 &&
        params.values.contains(QStringLiteral("depth"))) {
        params.set(QStringLiteral("depth"), m_feature.depth);
    }

    if ((!preferUserParams || !m_userParams.contains(strategyId)) && m_hasContourFeature) {
        const double contourDepth = contourMachiningDepth(m_contourFeature);
        if (!isPlanarMillingStrategy(strategyId) && params.values.contains(QStringLiteral("depth")) && contourDepth > 0.0) {
            params.set(QStringLiteral("depth"), contourDepth);
        }
        if (isSlotFeatureSelected()) {
            params.set(QStringLiteral("slotLength"),
                       m_contourFeature.length > 0.0 ? m_contourFeature.length
                                                     : m_contourFeature.radius * 2.0);
            const double slotWidth = m_contourFeature.width;
            params.set(QStringLiteral("slotWidth"),
                       slotWidth > 0.0 ? slotWidth : m_contourFeature.radius);
            params.set(QStringLiteral("angle"), m_contourFeature.angle);
        }
    }

    return params;
}

// 函数说明：setWorkOffset负责当前模块的核心处理。
void StrategyPanel::setWorkOffset(const QString &workOffset)
{
    QString normalized = workOffset.trimmed().toUpper();
    if (normalized.startsWith(QLatin1Char('G'))) {
        normalized.remove(0, 1);
    }
    bool ok = false;
    const int parsed = normalized.toInt(&ok);
    if (!ok || parsed < 54 || parsed > 59 || m_workOffset == parsed) {
        return;
    }
    cacheCurrentParams();
    m_workOffset = parsed;
    refreshParamTable();
}

// 函数说明：refreshParamTable负责当前模块的核心处理。
void StrategyPanel::refreshParamTable()
{
    refreshTemplates();
    m_paramTable->setRowCount(0);
    const QString id = m_strategyCombo->currentData().toString();
    if (id.isEmpty()) {
        return;
    }

    const StrategyParams defaults = paramsForStrategy(id, true);

    int row = 0;
    for (auto it = defaults.values.cbegin(); it != defaults.values.cend(); ++it) {
        if (it.key() == QStringLiteral("entryMode")) {
            continue;
        }
        m_paramTable->insertRow(row);
        auto *nameItem = new QTableWidgetItem(paramDisplayName(it.key()));
        nameItem->setData(Qt::UserRole, it.key());
        nameItem->setFlags(Qt::ItemIsEnabled);
        m_paramTable->setItem(row, 0, nameItem);
        m_paramTable->setItem(row, 1, new QTableWidgetItem(it.value().toString()));
        ++row;
    }

    if (m_operationTabs) rebuildOperationParameterGroups(m_operationTabs, m_paramTable, id);
    updateParamSummary();
}

bool StrategyPanel::selectStrategy(const QString &strategyId)
{
    const int index = m_strategyCombo->findData(strategyId);
    if (index < 0) return false;
    m_strategyCombo->setCurrentIndex(index);
    m_operationTabs->show();
    m_reselectButton->show();
    m_editHoleParamsBtn->hide();
    m_millingGroup->hide();
    findChild<QWidget *>(QStringLiteral("paramSummaryPanel"))->hide();
    m_proposalTitleLabel->hide();
    m_hintLabel->hide();
    rebuildOperationParameterGroups(m_operationTabs, m_paramTable, m_currentStrategyId);
    return true;
}

// 函数说明：setCurrentParams负责当前模块的核心处理。
void StrategyPanel::setCurrentParams(const StrategyParams &params)
{
    if (m_currentStrategyId.isEmpty()) {
        return;
    }

    m_userParams[m_currentStrategyId] = params;
    refreshParamTable();
}

// 函数说明：currentParams负责当前模块的核心处理。
StrategyParams StrategyPanel::currentParams() const
{
    if (m_paramTable->rowCount() <= 0 &&
        !m_currentStrategyId.isEmpty() &&
        m_userParams.contains(m_currentStrategyId)) {
        return m_userParams.value(m_currentStrategyId);
    }

    StrategyParams params;
    for (int row = 0; row < m_paramTable->rowCount(); ++row) {
        const QTableWidgetItem *keyItem = m_paramTable->item(row, 0);
        const QTableWidgetItem *valueItem = m_paramTable->item(row, 1);
        if (!keyItem || !valueItem) {
            continue;
        }
        params.set(keyItem->data(Qt::UserRole).toString(), valueItem->text().toDouble());
    }

    if (requiresPocketEntry(m_currentStrategyId,m_contourFeature)) {
        const int entryMode = m_entryModeCombo->currentData().toInt();
        if (entryMode == 0 || entryMode == 1) {
            params.set(QStringLiteral("entryMode"), entryMode);
        }
    }

    if (params.values.isEmpty() &&
        !m_currentStrategyId.isEmpty() &&
        m_userParams.contains(m_currentStrategyId)) {
        return m_userParams.value(m_currentStrategyId);
    }

    return params;
}

// 函数说明：currentContourFeature负责当前模块的核心处理。
ContourFeature StrategyPanel::currentContourFeature() const
{
    return applyMachiningParameters(machiningContour(m_contourFeature),currentParams());
}

// 函数说明：cacheCurrentParams负责当前模块的核心处理。
void StrategyPanel::cacheCurrentParams()
{
    if (m_currentStrategyId.isEmpty() || m_paramTable->rowCount() <= 0) {
        return;
    }
    m_userParams[m_currentStrategyId] = currentParams();
    updateParamSummary();
}

// 函数说明：paramSummaryText负责当前模块的核心处理。
QString StrategyPanel::paramSummaryText(const StrategyParams &params) const
{
    const bool zh = isChineseUi();
    if (params.values.isEmpty()) {
        return zh ? QStringLiteral("当前策略没有可编辑参数。")
                  : QStringLiteral("No editable parameters for the current strategy.");
    }

    const QStringList preferredKeys = {
        QStringLiteral("depth"),
        QStringLiteral("feedRate"),
        QStringLiteral("spindleSpeed"),
        QStringLiteral("stepDown"),
        QStringLiteral("stepover"),
        QStringLiteral("slotLength"),
        QStringLiteral("slotWidth"),
        QStringLiteral("angle")
    };

    QStringList pieces;
    QSet<QString> appended;
    for (const QString &key : preferredKeys) {
        if (!params.values.contains(key)) {
            continue;
        }
        pieces << QStringLiteral("%1 %2")
                      .arg(paramDisplayName(key))
                      .arg(formatParamValue(params.get(key)));
        appended.insert(key);
        if (pieces.size() >= 4) {
            break;
        }
    }

    if (pieces.size() < 4) {
        for (auto it = params.values.cbegin(); it != params.values.cend(); ++it) {
            if (appended.contains(it.key())) {
                continue;
            }
            pieces << QStringLiteral("%1 %2")
                          .arg(paramDisplayName(it.key()))
                          .arg(formatParamValue(it.value().toDouble()));
            if (pieces.size() >= 4) {
                break;
            }
        }
    }

    const QString prefix = zh
        ? QStringLiteral("共 %1 项").arg(params.values.size())
        : QStringLiteral("%1 params").arg(params.values.size());
    return QStringLiteral("%1  |  %2").arg(prefix, pieces.join(QStringLiteral("  |  ")));
}

// 函数说明：updateParamSummary负责当前模块的核心处理。
void StrategyPanel::updateParamSummary()
{
    if (!m_paramSummaryLabel) {
        return;
    }

    const StrategyParams params = currentParams();
    m_paramSummaryLabel->setText(paramSummaryText(params));
    const bool showPeck = m_currentStrategyId == "hole_peck" && m_hasFeature;
    m_effectivePeckSummary->setVisible(showPeck);
    m_effectivePeckSummary->setText(showPeck ? peckDepthSummary(m_feature, params) : QString());

    QStringList tooltipLines;
    for (auto it = params.values.cbegin(); it != params.values.cend(); ++it) {
        if (it.key() == QStringLiteral("entryMode")) {
            tooltipLines << QStringLiteral("%1: %2")
                                .arg(paramDisplayName(it.key()))
                                .arg(it.value().toInt() == 1
                                         ? (isChineseUi() ? QStringLiteral("螺旋下刀")
                                                          : QStringLiteral("Helical"))
                                         : (isChineseUi() ? QStringLiteral("垂直下刀")
                                                          : QStringLiteral("Vertical")));
            continue;
        }
        tooltipLines << QStringLiteral("%1: %2")
                            .arg(paramDisplayName(it.key()))
                            .arg(formatParamValue(it.value().toDouble()));
    }
    m_paramSummaryLabel->setToolTip(tooltipLines.join(QLatin1Char('\n')));
    updateToolCompatibilityPreview();
}

// 函数说明：seedContourParamsFromFeature负责当前模块的核心处理。
void StrategyPanel::seedContourParamsFromFeature()
{
    if (!m_hasContourFeature) {
        return;
    }

    for (const QString &strategyId : strategyIdsForCurrentContext()) {
        auto strategy = StrategyFactory::instance().strategy(strategyId);
        if (!strategy) {
            continue;
        }

        StrategyParams params = m_userParams.contains(strategyId)
            ? m_userParams.value(strategyId)
            : strategy->defaultParams();

        const double contourDepth = contourMachiningDepth(m_contourFeature);
        if (m_contourFeature.subType == QStringLiteral("verified_slot_opening"))
            params.set(QStringLiteral("depth"),0.0); // Never inherit or infer a machining depth.
        if (!isPlanarMillingStrategy(strategyId) && params.values.contains(QStringLiteral("depth"))) {
            params.set(QStringLiteral("depth"), contourDepth);
        }
        if (isSlotFeatureSelected()) {
            params.set(QStringLiteral("slotLength"),
                       m_contourFeature.length > 0.0 ? m_contourFeature.length
                                                     : m_contourFeature.radius * 2.0);
            const double slotWidth = m_contourFeature.width;
            params.set(QStringLiteral("slotWidth"),
                       slotWidth > 0.0 ? slotWidth
                                                    : m_contourFeature.radius);
            params.set(QStringLiteral("angle"), m_contourFeature.angle);
        }
        if (requiresPocketEntry(strategyId,m_contourFeature)) {
            params.values.remove(QStringLiteral("entryMode"));
        }

        m_userParams[strategyId] = params;
    }
}

// 函数说明：setContourFeature负责当前模块的核心处理。
void StrategyPanel::setContourFeature(const MachiningFeature &feature)
{
    m_contourFeature = feature;
    m_hasContourFeature = true;
    m_feature = HoleFeature();
    m_hasFeature = false;
    seedContourParamsFromFeature();
    populateStrategies();
    retranslateUi();
}

// 函数说明：clearContourFeature负责当前模块的核心处理。
void StrategyPanel::clearContourFeature()
{
    m_hasContourFeature = false;
    m_contourFeature = MachiningFeature();
    if (!m_hasFeature) {
        populateStrategies();
    }
    retranslateUi();
}

// 函数说明：setFeature负责当前模块的核心处理。
void StrategyPanel::setFeature(const HoleFeature &feature)
{
    m_feature = feature;
    m_hasFeature = true;
    m_contourFeature = MachiningFeature();
    m_hasContourFeature = false;
    populateStrategies();
    retranslateUi();
}

// 函数说明：clearFeature负责当前模块的核心处理。
void StrategyPanel::clearFeature()
{
    m_hasFeature = false;
    m_feature = HoleFeature();
    if (!m_hasContourFeature) {
        populateStrategies();
    }
    retranslateUi();
}

// 函数说明：setActiveRegion负责当前模块的核心处理。
void StrategyPanel::setActiveRegion(FaceRegion region)
{
    m_activeRegion = region;
    updateStrategyHint();
    updateProposalState();
}

// 函数说明：updateProposalState负责当前模块的核心处理。
void StrategyPanel::updateProposalState()
{
    if (!m_proposalStateLabel) {
        return;
    }

    FaceRegion featureRegion = FaceRegion::Unknown;
    if (m_hasFeature) {
        featureRegion = m_feature.region;
    } else if (m_hasContourFeature) {
        featureRegion = m_contourFeature.region;
    }

    const bool zh = isChineseUi();
    const bool hasSelection = m_hasFeature || m_hasContourFeature;
    m_materialSuggestionButton->setEnabled(hasSelection && !m_currentStrategyId.isEmpty() && currentTool().id > 0);
    const bool setupMismatch = hasSelection &&
        SetupOrientation::requiresActiveRegionConfirmation(m_activeRegion, featureRegion);
    const bool unspecifiedSetup = hasSelection && m_activeRegion == FaceRegion::Unknown;
    const bool noSupportedStrategy = hasSelection
        && m_strategyCombo->currentData().toString().isEmpty();
    const bool pocketRoughing = requiresPocketEntry(m_strategyCombo->currentData().toString(),m_contourFeature);
    const bool irregularPocket = pocketRoughing
        && m_contourFeature.boundaryPoints.size() >= 3
        && m_contourFeature.subType != QStringLiteral("rectangular_pocket")
        && m_contourFeature.subType != QStringLiteral("circular_pocket");
    const int entryMode = m_entryModeCombo->currentData().toInt();
    const bool entryModeMissing = pocketRoughing && entryMode != 0 && entryMode != 1;
    bool helicalEntryInvalid = false;
    if (pocketRoughing && entryMode == 1) {
        const StrategyParams params = currentParams();
        const ToolEntry tool = currentTool();
        const double stock = params.get(QStringLiteral("stockToLeave"), 0.0);
        const bool circular = m_contourFeature.subType == QStringLiteral("circular_pocket");
        const double halfLength = circular
            ? m_contourFeature.radius
            : m_contourFeature.length * 0.5;
        const double halfWidth = circular
            ? m_contourFeature.radius
            : m_contourFeature.width * 0.5;
        const double availableRadius = std::min(halfLength, halfWidth)
            - tool.diameter * 0.5 - stock;
        const double helixRadius = params.get(QStringLiteral("helixRadius"), 0.0);
        const double helixPitch = params.get(QStringLiteral("helixPitch"), 0.0);
        helicalEntryInvalid = irregularPocket || helixRadius <= 0.0 || helixPitch <= 0.0
            || helixRadius > availableRadius + 1.0e-6;
    }
    bool toolBlocked = false;
    if (hasSelection && !noSupportedStrategy) {
        const QString strategyId = m_strategyCombo->currentData().toString();
        const ToolEntry tool = currentTool();
        ToolCompatibilityReport report;
        if (m_hasFeature) {
            report = reviewToolCompatibility(
                strategyId, tool, m_feature, zh,
                holeCuttingDepth(strategyId, m_feature, currentParams(), tool));
        } else {
            const ContourFeature contour = currentContourFeature();
            report = reviewToolCompatibility(
                strategyId, tool, contour, zh,
                currentParams().get(QStringLiteral("depth"), contour.depth) - (isPlanarMillingStrategy(strategyId) ? currentParams().get("stockToLeave",0) : 0));
        }
        toolBlocked = report.hasBlockingIssues();
    }

    m_proposalStateLabel->setProperty(
        "warning", setupMismatch || unspecifiedSetup || entryModeMissing || helicalEntryInvalid);
    if (!hasSelection) {
        m_proposalStateLabel->setText(
            zh ? QStringLiteral("选择一个特征以创建待确认的工艺方案。")
               : QStringLiteral("Select a feature to create an operation proposal."));
    } else if (noSupportedStrategy) {
        if (isPocketFeature(m_contourFeature) && !m_contourFeature.boundaryPoints.isEmpty()) {
            m_proposalStateLabel->setText(
                zh ? QStringLiteral("已保存异形型腔外边界和孤岛拓扑；刀具半径偏置与分区避让尚未验证，暂不允许生成。")
                   : QStringLiteral("The irregular pocket boundary and island topology are preserved; tool-radius offset and segmented avoidance are not yet validated, so generation remains blocked."));
        } else {
            m_proposalStateLabel->setText(
                zh ? QStringLiteral("当前特征没有已验证的加工策略，不能创建正式工序。")
                   : QStringLiteral("No validated machining strategy is available for this feature; a confirmed operation cannot be created."));
        }
    } else if (unspecifiedSetup) {
        m_proposalStateLabel->setText(
            zh ? QStringLiteral("需要先确认 Setup：请使用“设置正面”并核对 WCS。")
               : QStringLiteral("Setup confirmation required: use Set Front Face and verify WCS."));
    } else if (setupMismatch) {
        m_proposalStateLabel->setText(
            zh ? QStringLiteral("无法确认：该特征不属于当前 Setup，或加工面尚未识别。")
               : QStringLiteral("Cannot confirm: this feature is outside the active Setup or its machining face is unknown."));
    } else if (entryModeMissing) {
        m_proposalStateLabel->setText(
            zh ? QStringLiteral("需要人工选择型腔下刀方式，软件不会自动代选。")
               : QStringLiteral("Select the pocket entry method; the software will not choose it automatically."));
    } else if (irregularPocket && entryMode == 1) {
        m_proposalStateLabel->setText(
            zh ? QStringLiteral("异形型腔目前仅支持经操作员确认的垂直下刀，请将下刀方式改为垂直下刀。")
               : QStringLiteral("Irregular pockets currently support confirmed vertical entry only. Change the entry method to Vertical."));
    } else if (helicalEntryInvalid) {
        m_proposalStateLabel->setText(
            zh ? QStringLiteral("螺旋下刀参数无效或超出有效型腔边界，请配置半径和节距。")
               : QStringLiteral("Helical entry parameters are invalid or outside the effective pocket boundary; configure radius and pitch."));
    } else {
        m_proposalStateLabel->setText(
            zh ? QStringLiteral("待确认：请复核策略、刀具和参数。")
               : QStringLiteral("Pending confirmation: review strategy, tool, and parameters."));
    }
    m_proposalStateLabel->style()->unpolish(m_proposalStateLabel);
    m_proposalStateLabel->style()->polish(m_proposalStateLabel);
    m_confirmOperationBtn->setEnabled(
        hasSelection && !noSupportedStrategy && !unspecifiedSetup && !setupMismatch &&
        !toolBlocked && !entryModeMissing && !helicalEntryInvalid);
}

// 中文说明：将当前特征、刀具和参数组装成待确认提案；只有确认后才创建正式工序，
// 防止自动识别结果未经人工核对直接进入 G 代码链路。
void StrategyPanel::onConfirmOperation()
{
    const QString strategyId = m_strategyCombo->currentData().toString();
    if (isPlanarMillingStrategy(strategyId)) {
        const QString error=planarMachiningError(inWorkCoordinates(currentContourFeature(),
            machiningOrigin(AppController::instance().projectManager())),currentTool(),currentParams());
        if (!error.isEmpty()) {OperationMessageBox::warning(this,tr("平面参数"),error);return;}
    }
    if (strategyId.isEmpty()) {
        return;
    }

    if (requiresPocketEntry(strategyId,m_contourFeature) &&
        m_entryModeCombo->currentData().toInt() == 0) {
        const bool irregularPocket = m_contourFeature.boundaryPoints.size() >= 3
            && m_contourFeature.subType != QStringLiteral("rectangular_pocket")
            && m_contourFeature.subType != QStringLiteral("circular_pocket");
        const bool finishing = strategyId == QStringLiteral("mill_pocket_finish") ||
            strategyId == QStringLiteral("mill_sloped_floor") ||
            strategyId == QStringLiteral("mill_pocket_floor_finish");
        const bool continueVertical = QMessageBox::warning(
            this,
            isChineseUi() ? QStringLiteral("确认垂直下刀条件")
                          : QStringLiteral("Confirm Vertical Entry Conditions"),
            isChineseUi()
                ? (finishing
                       ? QStringLiteral("精加工使用垂直下刀。请确认已完成粗加工、下刀及连接区域已清空，并核对侧壁和底部剩余量。是否继续创建工序？")
                       : irregularPocket
                       ? QStringLiteral("异形型腔会在每个独立扫描段重新垂直下刀。请确认刀具能够中心切削，或已为每个生成的分段入口预钻。是否继续创建工序？")
                       : QStringLiteral("垂直下刀仅适用于已预钻入口，或确认刀具能够中心切削的情况。是否继续创建工序？"))
                : (finishing
                       ? QStringLiteral("Finishing uses vertical entry. Confirm roughing is complete, entry and linking areas are clear, and side/floor stock is suitable. Create the operation?")
                       : irregularPocket
                       ? QStringLiteral("Irregular clearing plunges again for each separate scan segment. Confirm a center-cutting tool or a predrilled hole at every generated segment entry. Create the operation anyway?")
                       : QStringLiteral("Vertical entry is only suitable with a predrilled entry or a confirmed center-cutting tool. Create the operation anyway?")),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) == QMessageBox::Yes;
        if (!continueVertical) {
            return;
        }
    }

    cacheCurrentParams();
    OperationProposal proposal;
    proposal.strategyId = strategyId;
    proposal.params = currentParams();
    proposal.toolId = currentTool().id;
    if (m_hasFeature) {
        proposal.kind = OperationProposalKind::Hole;
        proposal.holeFeature = m_feature;
    } else if (m_hasContourFeature) {
        proposal.kind = OperationProposalKind::Contour;
        proposal.contourFeature = currentContourFeature();
    } else {
        return;
    }
    emit operationProposalConfirmed(proposal);
}

// 函数说明：onEditHoleParams负责当前模块的核心处理。
void StrategyPanel::onEditHoleParams()
{
    const QString strategyId = m_strategyCombo->currentData().toString();
    if (strategyId.isEmpty()) {
        return;
    }

    QMap<QString, QString> labels;
    StrategyParams current = currentParams();
    const int selectedEntryMode = m_entryModeCombo->currentData().toInt();
    current.values.remove(QStringLiteral("entryMode"));
    for (auto it = current.values.cbegin(); it != current.values.cend(); ++it) {
        labels.insert(it.key(), paramDisplayName(it.key()));
    }

    ParameterEditorDialog paramDialog(this);
    paramDialog.setChineseUi(isChineseUi());
    paramDialog.setWindowTitle(isChineseUi()
        ? QStringLiteral("参数配置 - %1").arg(displayNameForStrategy(strategyId))
        : QStringLiteral("Configure Parameters - %1").arg(displayNameForStrategy(strategyId)));
    paramDialog.setLabelMap(labels);
    paramDialog.setBasicKeys(basicParameterKeys());
    paramDialog.setNonNegativeKeys(nonNegativeParameterKeys());
    paramDialog.setAllowAnyValueKeys(unconstrainedParameterKeys());
    paramDialog.setDefaultParams(paramsForStrategy(strategyId, false));
    paramDialog.setParams(current);

    if (paramDialog.exec() == QDialog::Accepted) {
        StrategyParams updated = paramDialog.params();
        if (requiresPocketEntry(strategyId,m_contourFeature) &&
            (selectedEntryMode == 0 || selectedEntryMode == 1)) {
            updated.set(QStringLiteral("entryMode"), selectedEntryMode);
        }
        setCurrentParams(updated);
        updatePocketEntryUi();
        updateParamSummary();
    }
}
// 函数说明：onCircleMillClicked负责当前模块的核心处理。
void StrategyPanel::onCircleMillClicked()
{
    CircleMillDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    OperationProposal proposal;
    proposal.kind = OperationProposalKind::Contour;
    proposal.strategyId = QStringLiteral("mill_circle");
    proposal.toolId = currentTool().id;
    proposal.params = dialog.params();
    proposal.contourFeature = dialog.feature();
    if (proposal.contourFeature.region == FaceRegion::Unknown &&
        m_activeRegion != FaceRegion::Unknown) {
        proposal.contourFeature.region = m_activeRegion;
    }
    emit operationProposalConfirmed(proposal);
}

// 函数说明：retranslateUi负责当前模块的核心处理。
void StrategyPanel::retranslateUi()
{
    cacheCurrentParams();
    const bool zh = isChineseUi();
    m_templateLabel->setText(zh ? QStringLiteral("工艺模板：") : QStringLiteral("Template:"));
    m_templateCombo->setAccessibleName(zh ? QStringLiteral("用户工艺模板") : QStringLiteral("User process template"));
    m_applyTemplateButton->setText(zh ? QStringLiteral("应用") : QStringLiteral("Apply"));
    m_saveTemplateButton->setText(zh ? QStringLiteral("保存模板") : QStringLiteral("Save template"));
    m_materialSuggestionButton->setText(zh ? QStringLiteral("材料建议…") : QStringLiteral("Material suggestions…"));
    refreshTemplates();

    m_proposalTitleLabel->setText(zh ? QStringLiteral("工艺方案（待确认）")
                                     : QStringLiteral("Operation Proposal (Pending)"));

    if (m_hasFeature) {
        m_featureLabel->setText(
            zh ? QStringLiteral("当前特征：孔径 %1，深度 %2")
                     .arg(m_feature.radius * 2.0, 0, 'f', 2)
                     .arg(m_feature.depth, 0, 'f', 2)
               : QStringLiteral("Current feature: D%1, depth %2")
                     .arg(m_feature.radius * 2.0, 0, 'f', 2)
                     .arg(m_feature.depth, 0, 'f', 2));
    } else if (m_hasContourFeature) {
        if (m_contourFeature.subType=="verified_sloped_floor") {
            m_featureLabel->setText(zh?QStringLiteral("当前特征：带孔斜面，仅加工 +Z 可达区；遮挡条带、孔边及余量保留")
                :QStringLiteral("Perforated slope: fixed-Z accessible area only; occluded strips, hole margins and stock retained"));
        } else if (isFlatSurfaceFeature(m_contourFeature)) {
            m_featureLabel->setText(
                zh ? QStringLiteral("当前特征：平面 L%1，W%2，深度 %3")
                         .arg(m_contourFeature.length, 0, 'f', 2)
                         .arg(m_contourFeature.width, 0, 'f', 2)
                         .arg(m_contourFeature.depth, 0, 'f', 2)
                   : QStringLiteral("Current feature: surface L%1 W%2 depth %3")
                         .arg(m_contourFeature.length, 0, 'f', 2)
                         .arg(m_contourFeature.width, 0, 'f', 2)
                         .arg(m_contourFeature.depth, 0, 'f', 2));
        } else if (isPocketFeature(m_contourFeature)) {
            m_featureLabel->setText(
                zh ? QStringLiteral("当前特征：型腔 L%1，W%2，深度 %3，外边界 %4 点，孤岛 %5 个")
                         .arg(m_contourFeature.length, 0, 'f', 2)
                         .arg(m_contourFeature.width, 0, 'f', 2)
                         .arg(m_contourFeature.depth, 0, 'f', 2)
                         .arg(m_contourFeature.boundaryPoints.size())
                         .arg(m_contourFeature.islandBoundaries.size())
                   : QStringLiteral("Current feature: pocket L%1 W%2 depth %3, boundary %4 pts, %5 islands")
                         .arg(m_contourFeature.length, 0, 'f', 2)
                         .arg(m_contourFeature.width, 0, 'f', 2)
                         .arg(m_contourFeature.depth, 0, 'f', 2)
                         .arg(m_contourFeature.boundaryPoints.size())
                         .arg(m_contourFeature.islandBoundaries.size()));
        } else if (isSlotFeatureSelected()) {
            m_featureLabel->setText(
                zh ? QStringLiteral("当前特征：槽长 %1，槽宽 %2，深度 %3")
                         .arg(m_contourFeature.length, 0, 'f', 2)
                         .arg(m_contourFeature.width, 0, 'f', 2)
                         .arg(m_contourFeature.depth, 0, 'f', 2)
                   : QStringLiteral("Current feature: slot L%1 W%2 depth %3")
                         .arg(m_contourFeature.length, 0, 'f', 2)
                         .arg(m_contourFeature.width, 0, 'f', 2)
                         .arg(m_contourFeature.depth, 0, 'f', 2));
        } else {
            m_featureLabel->setText(
                zh ? QStringLiteral("当前特征：轮廓半径 %1，深度 %2")
                         .arg(m_contourFeature.radius, 0, 'f', 2)
                         .arg(m_contourFeature.depth, 0, 'f', 2)
                   : QStringLiteral("Current feature: R%1 depth %2")
                         .arg(m_contourFeature.radius, 0, 'f', 2)
                         .arg(m_contourFeature.depth, 0, 'f', 2));
        }
    } else {
        m_featureLabel->setText(zh ? QStringLiteral("当前特征：未选择")
                                   : QStringLiteral("Current feature: none selected"));
    }

    m_strategyLabel->setText(zh ? QStringLiteral("加工策略：") : QStringLiteral("Strategy:"));
    m_toolLabel->setText(zh ? QStringLiteral("刀具：") : QStringLiteral("Tool:"));
    m_entryModeLabel->setText(zh ? QStringLiteral("下刀方式：")
                                 : QStringLiteral("Entry method:"));
    m_entryModeCombo->setItemText(
        0, zh ? QStringLiteral("请选择（必须人工确认）")
              : QStringLiteral("Select (operator confirmation required)"));
    m_entryModeCombo->setItemText(
        1, zh ? QStringLiteral("垂直下刀（已预钻/可中心切削）")
              : QStringLiteral("Vertical (predrilled/center-cutting only)"));
    m_entryModeCombo->setItemText(
        2, zh ? QStringLiteral("螺旋下刀") : QStringLiteral("Helical"));
    m_editHoleParamsBtn->setText(zh ? QStringLiteral("编辑孔加工参数")
                                    : QStringLiteral("Configure Parameters..."));
    if (zh) {
        m_editHoleParamsBtn->setText(QStringLiteral("参数配置..."));
    }
    m_confirmOperationBtn->setText(zh ? QStringLiteral("确认并加入工序")
                                      : QStringLiteral("Confirm and Add Operation"));
    m_millingGroup->setTitle(zh ? QStringLiteral("手动几何") : QStringLiteral("Manual Geometry"));
    m_circleMillBtn->setText(zh ? QStringLiteral("自定义圆形工序...")
                                : QStringLiteral("Custom Circle Operation..."));
    m_paramTable->setHorizontalHeaderLabels({
        zh ? QStringLiteral("参数") : QStringLiteral("Parameter"),
        zh ? QStringLiteral("数值") : QStringLiteral("Value")
    });

    for (int i = 0; i < m_strategyCombo->count(); ++i) {
        m_strategyCombo->setItemText(i, displayNameForStrategy(m_strategyCombo->itemData(i).toString()));
    }

    m_editHoleParamsBtn->setVisible(m_hasFeature || m_hasContourFeature);
    m_paramSummaryLabel->setVisible(m_hasFeature || m_hasContourFeature);
    FaceRegion proposalRegion = FaceRegion::Unknown;
    if (m_hasFeature) {
        proposalRegion = m_feature.region;
    } else if (m_hasContourFeature) {
        proposalRegion = m_contourFeature.region;
    }
    const bool setupConfirmed = m_activeRegion != FaceRegion::Unknown;
    const bool setupMismatch = (m_hasFeature || m_hasContourFeature) &&
        SetupOrientation::requiresActiveRegionConfirmation(
            m_activeRegion, proposalRegion);
    m_confirmOperationBtn->setEnabled(
        setupConfirmed && !setupMismatch && (m_hasFeature || m_hasContourFeature));
    m_millingGroup->setVisible(
        setupConfirmed && m_hasContourFeature && !isSlotFeatureSelected());
    m_circleMillBtn->setVisible(
        setupConfirmed && m_hasContourFeature && !isSlotFeatureSelected());

    m_strategyCombo->setAccessibleName(zh ? QStringLiteral("加工策略")
                                          : QStringLiteral("Machining strategy"));
    m_toolCombo->setAccessibleName(zh ? QStringLiteral("刀具") : QStringLiteral("Tool"));
    m_entryModeCombo->setAccessibleName(
        zh ? QStringLiteral("型腔下刀方式") : QStringLiteral("Pocket entry method"));
    m_confirmOperationBtn->setAccessibleName(
        zh ? QStringLiteral("确认工艺方案并加入工序")
           : QStringLiteral("Confirm operation proposal and add operation"));

    {
        QSignalBlocker blocker(m_toolCombo);
        for (int i = 0; i < m_toolCombo->count(); ++i) {
            const ToolEntry tool = ToolLibrary::instance().tool(m_toolCombo->itemData(i).toInt());
            m_toolCombo->setItemText(i, toolDisplayName(tool));
        }
    }
    refreshParamTable();
    updatePocketEntryUi();
    updateStrategyHint();
    updateParamSummary();
    updateProposalState();
}

// 函数说明：paramDisplayName负责当前模块的核心处理。
QString StrategyPanel::paramDisplayName(const QString &key) const
{
    if(m_currentStrategyId=="mill_sloped_floor") {
        if(key=="feedHeight")return isChineseUi()?QStringLiteral("源模型上方最小净空"):QStringLiteral("Minimum clearance above source");
        if(key=="floorStockToLeave")return isChineseUi()?QStringLiteral("底面 Z 向余量"):QStringLiteral("Vertical floor allowance");
    }
    if (key == "peckDepth" && m_currentStrategyId == "hole_peck") return isChineseUi() ? QStringLiteral("单次深度上限 Q") : QStringLiteral("Peck depth upper limit Q");
    if (key=="stockToLeave" && m_currentStrategyId=="mill_pocket_rough") return isChineseUi()?QStringLiteral("侧壁余量"):QStringLiteral("Side allowance");
    if (key=="stockToLeave" && isPlanarMillingStrategy(m_currentStrategyId)) return isChineseUi()?QStringLiteral("底余量"):QStringLiteral("Floor allowance");
    if (!isChineseUi()) {
        if (key == QStringLiteral("entryMode")) return QStringLiteral("Entry method");
        if (key == QStringLiteral("sideStockToLeave")) return QStringLiteral("Side stock");
        if (key == QStringLiteral("floorStockToLeave")) return QStringLiteral("Floor stock");
        return key;
    }
    if (key == QStringLiteral("safeHeight")) return QStringLiteral("安全高度");
    if (key == QStringLiteral("feedHeight")) return QStringLiteral("进给起始高度");
    if (key == QStringLiteral("depth")) return QStringLiteral("加工深度");
    if (key == QStringLiteral("axialDepth")) return QStringLiteral("轴向 G81 深度（圆周方式不使用，用户确认）");
    if (key == QStringLiteral("chamferMode")) return QStringLiteral("倒角方式（0圆周 G1/G2 / 1轴向 G81）");
    if (key == QStringLiteral("peckDepth")) return QStringLiteral("每刀钻深");
    if (key == QStringLiteral("stepDown")) return QStringLiteral("每层切深");
    if (key == QStringLiteral("stepover")) return QStringLiteral("步距");
    if (key == QStringLiteral("retractHeight")) return QStringLiteral("退刀高度");
    if (key == QStringLiteral("retractDelta")) return QStringLiteral("回退量");
    if (key == QStringLiteral("fullRetractEvery")) return QStringLiteral("全退间隔");
    if (key == QStringLiteral("spindleSpeed")) return QStringLiteral("主轴转速");
    if (key == QStringLiteral("feedRate")) return QStringLiteral("进给速度");
    if (key == QStringLiteral("plungeRate")) return QStringLiteral("下刀速度");
    if (key == QStringLiteral("dwellTime")) return QStringLiteral("底部停留");
    if (key == QStringLiteral("pitch")) return QStringLiteral("螺距");
    if (key == QStringLiteral("chamferWidth")) return QStringLiteral("圆周倒角宽度（轴向方式不使用）");
    if (key == QStringLiteral("chamferAngle")) return QStringLiteral("圆周倒角角度（轴向方式不使用）");
    if (key == QStringLiteral("compensation")) return QStringLiteral("刀补方向");
    if (key == QStringLiteral("sideStockToLeave")) return QStringLiteral("侧壁余量");
    if (key == QStringLiteral("floorStockToLeave")) return QStringLiteral("底面余量");
    if (key == QStringLiteral("stockToLeave")) return QStringLiteral("余量");
    if (key == QStringLiteral("leadLength")) return QStringLiteral("圆周引入引出长度（轴向方式不使用）");
    if (key == QStringLiteral("slotLength")) return QStringLiteral("槽长");
    if (key == QStringLiteral("slotWidth")) return QStringLiteral("槽宽");
    if (key == QStringLiteral("angle")) return QStringLiteral("方向角");
    if (key == QStringLiteral("helixRadius")) return QStringLiteral("螺旋下刀半径");
    if (key == QStringLiteral("helixPitch")) return QStringLiteral("螺旋节距");
    if (key == QStringLiteral("entryMode")) return QStringLiteral("下刀方式");
    if (key == QStringLiteral("finishStock")) return QStringLiteral("斜面精修余量");
    if (key == QStringLiteral("slopeDirection")) return QStringLiteral("斜面方向");
    if (key == QStringLiteral("workOffset")) return QStringLiteral("工件坐标系（54=G54…59=G59）");
    if (key == QStringLiteral("coolantMode")) return QStringLiteral("冷却方式（0无/1外冷/2内冷/3吹气/4混合）");
    if (key == QStringLiteral("depthMode")) return QStringLiteral("深度模式（仅支持 0绝对）");
    if (key == QStringLiteral("threadHandedness")) return QStringLiteral("螺纹旋向（1右旋/-1左旋）");
    if (key == QStringLiteral("tapRetract")) return QStringLiteral("攻丝回退量");
    if (key == QStringLiteral("chipBreakRetract")) return QStringLiteral("断屑回退量");
    if (key == QStringLiteral("toolTipRadius")) return QStringLiteral("倒角刀尖半径");
    if (key == QStringLiteral("cutDirection")) return QStringLiteral("平面走刀方向（0内向外/1外向内/2混合）");
    if (key == QStringLiteral("linkMode")) return QStringLiteral("连接方式（0直线/1圆弧）");
    if (key == QStringLiteral("boundingRectangle")) return QStringLiteral("外接矩形范围（0关/1开）");
    if (key == QStringLiteral("pathPattern")) return QStringLiteral("型腔路径方式（0双向/1单向/2环切/3螺旋）");
    if (key == QStringLiteral("keepIslands")) return QStringLiteral("保留岛屿（0否/1是）");
    if (key == QStringLiteral("overcut")) return QStringLiteral("轮廓过切量");
    if (key == QStringLiteral("startAtMidpoint")) return QStringLiteral("长直线中点起刀（0否/1是）");
    return key;
}

// 函数说明：toolDisplayName负责当前模块的核心处理。
QString StrategyPanel::toolDisplayName(const ToolEntry &tool) const
{
    return isChineseUi()
        ? QStringLiteral("T%1 %2 直径%3").arg(tool.id).arg(tool.name).arg(tool.diameter, 0, 'f', 1)
        : QStringLiteral("T%1 %2 D%3").arg(tool.id).arg(tool.name).arg(tool.diameter, 0, 'f', 1);
}

// 函数说明：preferredToolType负责当前模块的核心处理。
QString StrategyPanel::preferredToolType(const QString &strategyId) const
{
    if (strategyId == QStringLiteral("hole_spot")) {
        return QStringLiteral("spot_drill");
    }
    if (strategyId == QStringLiteral("hole_tapping")) {
        return QStringLiteral("tap");
    }
    if (strategyId == QStringLiteral("hole_reaming")) {
        return QStringLiteral("reamer");
    }
    if (strategyId == QStringLiteral("hole_chamfer")) {
        return QStringLiteral("chamfer_mill");
    }
    if (strategyId == QStringLiteral("hole_g76") || strategyId == QStringLiteral("hole_g86")) {
        return QStringLiteral("boring_bar");
    }
    if (strategyId == QStringLiteral("mill_face") ||
        strategyId == QStringLiteral("mill_surface_finish")) {
        return QStringLiteral("end_mill");
    }
    if (strategyId.startsWith(QStringLiteral("mill_")) ||
        strategyId == QStringLiteral("hole_circular_mill")) {
        return QStringLiteral("end_mill");
    }
    return QStringLiteral("drill");
}

// 函数说明：strategyHint负责当前模块的核心处理。
QString StrategyPanel::strategyHint(const QString &strategyId) const
{
    const bool zh = isChineseUi();
    if(strategyId=="mill_sloped_floor")return zh
        ?QStringLiteral("固定 +Z 平底刀扫描可达区域；红色遮挡条带保留。须先清空下刀空间；各段从安全高度进给下刀并全退刀。底余量按 Z 向计，平底刀在斜面上另有残留。")
        :QStringLiteral("Fixed-Z flat cutter, accessible region only; occluded strips retained. Clear entry space first. Each span feeds from safe Z and fully retracts. Floor allowance is vertical; flat-cutter slope residue remains.");
    if (!zh) {
        if (strategyId == QStringLiteral("hole_spot"))
            return QStringLiteral("Spot drill for positioning only. Keep the depth shallow.");
        if (strategyId == QStringLiteral("hole_peck"))
            return QStringLiteral("Peck drilling for pilot or rough holes with chip evacuation.");
        if (strategyId == QStringLiteral("hole_deephole"))
            return QStringLiteral("Deep-hole drilling for large depth/diameter ratios.");
        if (strategyId == QStringLiteral("hole_g73"))
            return QStringLiteral("G73 uses short-retract chip breaking; Q is the positive depth per peck.");
        if (strategyId == QStringLiteral("hole_circular_mill"))
            return QStringLiteral("Mill a drilled pilot hole to final diameter with an end mill.");
        if (strategyId == QStringLiteral("hole_reaming"))
            return QStringLiteral("Use a reamer close to the target hole diameter.");
        if (strategyId == QStringLiteral("hole_chamfer"))
            return QStringLiteral("Choose circumferential G1/G2 milling or axial G81 countersinking. Axial mode cuts the operator-confirmed axial depth; it does not infer tip compensation from chamfer width.");
        if (strategyId == QStringLiteral("hole_tapping"))
            return QStringLiteral("Tapping uses pitch-based feed. Match it to the thread hole.");
        if (strategyId == QStringLiteral("mill_slot"))
            return QStringLiteral("Open-slot milling uses the selected slot geometry and shared roughing parameters.");
        if (strategyId == QStringLiteral("mill_blind_slot"))
            return QStringLiteral("Blind-slot milling keeps the slot closed at both ends and supports helix entry.");
        if (strategyId == QStringLiteral("mill_tapered_slot"))
            return QStringLiteral("Tapered-slot milling uses slot roughing plus a single sloped-floor finish pass for front-reachable wedge slots.");
        if (strategyId == QStringLiteral("mill_face"))
            return QStringLiteral("Single-layer planar raster: explicitly enter removal depth; stock is floor allowance. Original rectangle required.");
        if (strategyId == QStringLiteral("mill_surface_finish"))
            return QStringLiteral("Single-layer planar finish: confirm remaining material and enter removal depth; prior roughing is not assumed.");
        if (strategyId == QStringLiteral("mill_pocket_rough"))
            return QStringLiteral("Pocket roughing clears the recognized length/width boundary in layers; review tool diameter, side stock, stepover, and entry mode.");
        if (strategyId == QStringLiteral("mill_pocket_finish"))
            return QStringLiteral("Pocket wall finish plunges at the cleared pocket center, then uses CAM-side offset on rectangular or circular walls; review side stock, floor stock, and center clearance.");
        if (strategyId == QStringLiteral("mill_pocket_floor_finish"))
            return QStringLiteral("Pocket floor finish plunges at the cleared pocket center, then uses CAM-side raster passes inside rectangular or circular boundaries; review side stock, floor stock, and stepover.");
        if (strategyId == QStringLiteral("mill_closed_contour"))
            return QStringLiteral("Closed contour milling follows the selected contour for profile cutting.");
        if (strategyId == QStringLiteral("mill_open_contour"))
            return QStringLiteral("Open contour milling is suited for open edges and partial profiles.");
        return QStringLiteral("Verify tool, depth, feed, and process stage before generating G-code.");
    }

    if (strategyId == QStringLiteral("hole_spot"))
        return QStringLiteral("定点钻仅用于定位，深度应保持较浅。");
    if (strategyId == QStringLiteral("hole_peck"))
        return QStringLiteral("排屑钻适合底孔或粗孔，可分段排屑。");
    if (strategyId == QStringLiteral("hole_deephole"))
        return QStringLiteral("深孔钻适合深径比较大的孔。");
    if (strategyId == QStringLiteral("hole_g73"))
        return QStringLiteral("G73 采用短回退高速断屑；Q 为每次钻削的正深度。");
    if (strategyId == QStringLiteral("hole_circular_mill"))
        return QStringLiteral("圆插补扩孔用于把底孔铣到最终孔径。");
    if (strategyId == QStringLiteral("hole_reaming"))
        return QStringLiteral("铰孔要求铰刀直径接近目标孔径。");
    if (strategyId == QStringLiteral("hole_chamfer"))
        return QStringLiteral("孔口倒角可选圆周 G1/G2 铣削或轴向 G81 锪削；轴向方式严格按用户确认的轴向深度加工，不按倒角宽度推算刀尖补偿。");
    if (strategyId == QStringLiteral("hole_tapping"))
        return QStringLiteral("攻丝按主轴转速和螺距自动匹配进给。");
    if (strategyId == QStringLiteral("mill_slot"))
        return QStringLiteral("开口槽铣会直接使用当前槽长、槽宽、槽深和方向角。");
    if (strategyId == QStringLiteral("mill_blind_slot"))
        return QStringLiteral("盲槽铣适合两端封闭的槽，可配合螺旋下刀。");
    if (strategyId == QStringLiteral("mill_tapered_slot"))
        return QStringLiteral("斜底槽铣适合正面可达的楔形槽，先开粗再做单方向斜底精修。");
    if (strategyId == QStringLiteral("mill_face"))
        return QStringLiteral("单层平面扫掠：请明确输入去除深度；余量为底余量，仅支持原始矩形平面。");
    if (strategyId == QStringLiteral("mill_surface_finish"))
        return QStringLiteral("单层平面精加工：请确认实际剩余量并输入去除深度；不自动假定已完成粗加工。");
    if (strategyId == QStringLiteral("mill_pocket_rough"))
        return QStringLiteral("型腔开粗按识别到的真实长宽边界分层清腔，请人工核对刀径、侧壁余量、行距和下刀方式。");
    if (strategyId == QStringLiteral("mill_pocket_finish"))
        return QStringLiteral("型腔侧壁精加工支持矩形、圆形及源模型已证明的单一平底凸区域。请确认已开粗、下刀区域清空，并核对侧壁余量、底部余量和分层深度。");
    if (strategyId == QStringLiteral("mill_pocket_floor_finish"))
        return QStringLiteral("型腔底面精加工支持矩形、圆形及源模型已证明的单一平底凸区域。请确认已开粗、下刀区域清空；真实区域采用单层扫描及边缘清理，步距不得超过刀径，底部余量独立设置。");
    if (strategyId == QStringLiteral("mill_closed_contour"))
        return QStringLiteral("封闭轮廓铣用于外形或闭合边界精修。");
    if (strategyId == QStringLiteral("mill_open_contour"))
        return QStringLiteral("开放轮廓铣用于开放边界或局部轮廓。");
    return QStringLiteral("生成前请确认刀具、深度和进给参数。");
}

// 函数说明：updateStrategyHint负责当前模块的核心处理。
void StrategyPanel::updateStrategyHint()
{
    if (m_hintLabel) {
        m_hintLabel->setText(strategyHint(m_strategyCombo->currentData().toString()));
    }
}

// 函数说明：updatePocketEntryUi负责当前模块的核心处理。
void StrategyPanel::updatePocketEntryUi()
{
    const bool visible = m_hasContourFeature &&
        requiresPocketEntry(m_strategyCombo->currentData().toString(),m_contourFeature);
    m_entryModeLabel->setVisible(visible);
    m_entryModeCombo->setVisible(visible);
    if (!visible) {
        return;
    }

    int entryMode = -1;
    const StrategyParams saved = m_userParams.value(m_strategyCombo->currentData().toString());
    if (saved.values.contains(QStringLiteral("entryMode"))) {
        entryMode = int(saved.get(QStringLiteral("entryMode"), -1.0));
    }
    const int index = m_entryModeCombo->findData(entryMode);
    QSignalBlocker blocker(m_entryModeCombo);
    m_entryModeCombo->setCurrentIndex(index >= 0 ? index : 0);
}

// 函数说明：updateToolCompatibilityPreview负责当前模块的核心处理。
void StrategyPanel::updateToolCompatibilityPreview()
{
    if (!m_toolFitLabel) {
        return;
    }
    const QString strategyId = m_strategyCombo->currentData().toString();
    const ToolEntry tool = currentTool();
    const bool zh = isChineseUi();
    if (strategyId.isEmpty()) {
        m_toolFitLabel->setProperty("state", QStringLiteral("blocking"));
        m_toolFitLabel->setText(
            zh ? QStringLiteral("当前特征没有已验证的刀路策略，刀具适配无法通过。")
               : QStringLiteral("No validated toolpath strategy exists for this feature, so tool compatibility cannot pass."));
        m_toolFitLabel->style()->unpolish(m_toolFitLabel);
        m_toolFitLabel->style()->polish(m_toolFitLabel);
        return;
    }
    ToolCompatibilityReport report;
    if (m_hasFeature) {
        report = reviewToolCompatibility(strategyId, tool, m_feature, zh,
                                         holeCuttingDepth(strategyId, m_feature, currentParams(), tool));
    } else if (m_hasContourFeature) {
        const ContourFeature contour = currentContourFeature();
        report = reviewToolCompatibility(strategyId, tool, contour, zh,
                                         currentParams().get(QStringLiteral("depth"), contour.depth) - (isPlanarMillingStrategy(strategyId) ? currentParams().get("stockToLeave",0) : 0));
    } else {
        m_toolFitLabel->setText(zh ? QStringLiteral("选择加工特征后显示刀具适配结果。")
                                   : QStringLiteral("Select a feature to preview tool compatibility."));
        m_toolFitLabel->setProperty("state", QStringLiteral("normal"));
        return;
    }

    QStringList messages;
    for (const ToolCompatibilityIssue &issue : report.issues) {
        messages.append(QStringLiteral("• %1").arg(issue.message));
    }
    if (report.hasBlockingIssues()) {
        m_toolFitLabel->setProperty("state", QStringLiteral("blocking"));
        m_toolFitLabel->setText((zh ? QStringLiteral("刀具不适配，禁止确认：\n")
                                    : QStringLiteral("Tool is incompatible; confirmation is blocked:\n"))
                                + messages.join(QLatin1Char('\n')));
    } else if (report.hasWarnings()) {
        m_toolFitLabel->setProperty("state", QStringLiteral("warning"));
        m_toolFitLabel->setText((zh ? QStringLiteral("需要人工核对：\n")
                                    : QStringLiteral("Operator review required:\n"))
                                + messages.join(QLatin1Char('\n')));
    } else {
        m_toolFitLabel->setProperty("state", QStringLiteral("normal"));
        m_toolFitLabel->setText(
            zh ? QStringLiteral("刀具基础适配通过：%1，D%2，有效刃长 %3 mm。")
                     .arg(tool.type).arg(tool.diameter, 0, 'f', 2).arg(tool.fluteLen, 0, 'f', 2)
               : QStringLiteral("Basic tool fit passed: %1, D%2, flute length %3 mm.")
                     .arg(tool.type).arg(tool.diameter, 0, 'f', 2).arg(tool.fluteLen, 0, 'f', 2));
    }
    m_toolFitLabel->style()->unpolish(m_toolFitLabel);
    m_toolFitLabel->style()->polish(m_toolFitLabel);
}

// 函数说明：selectPreferredToolForFeature负责当前模块的核心处理。
void StrategyPanel::selectPreferredToolForFeature()
{
    if (m_toolCombo->count() <= 0) {
        return;
    }

    const QString strategyId = m_strategyCombo->currentData().toString();
    int bestComboIndex = -1;
    double bestScore = 1.0e100;

    for (int i = 0; i < m_toolCombo->count(); ++i) {
        const ToolEntry tool = ToolLibrary::instance().tool(m_toolCombo->itemData(i).toInt());
        if (tool.id <= 0 || tool.diameter <= 0.0) {
            continue;
        }

        double score = tool.diameter;
        if (m_hasFeature && m_feature.radius > 0.0) {
            const double targetDiameter = m_feature.radius * 2.0;
            score = std::abs(tool.diameter - targetDiameter);
            if (strategyId == QStringLiteral("hole_spot")) {
                score = tool.diameter;
            }
            if ((strategyId == QStringLiteral("hole_peck") ||
                 strategyId == QStringLiteral("hole_deephole") ||
                 strategyId == QStringLiteral("hole_g73")) &&
                tool.diameter > targetDiameter) {
                score += 1.0e6;
            }
            if (strategyId == QStringLiteral("hole_circular_mill") && tool.diameter >= targetDiameter) {
                score += 1.0e6;
            }
        } else if (isSlotFeatureSelected()) {
            const double mappedSlotWidth = m_contourFeature.width;
            const double slotWidth = mappedSlotWidth > 0.0
                ? mappedSlotWidth
                : m_contourFeature.radius;
            if (tool.diameter >= slotWidth) {
                score += 1.0e6;
            } else {
                score = slotWidth - tool.diameter;
            }
        } else if (isFlatSurfaceFeature(m_contourFeature)) {
            score = -tool.diameter;
        }

        if (score < bestScore) {
            bestScore = score;
            bestComboIndex = i;
        }
    }

    if (bestComboIndex >= 0) {
        m_toolCombo->setCurrentIndex(bestComboIndex);
    }
}

// 函数说明：currentTool负责当前模块的核心处理。
ToolEntry StrategyPanel::currentTool() const
{
    const int toolId = m_toolCombo->currentData().toInt();
    return ToolLibrary::instance().tool(toolId);
}

void StrategyPanel::restoreDraft(const StrategyParams &params, int toolId)
{
    setCurrentParams(params);
    const int index = m_toolCombo->findData(toolId);
    if (index >= 0) m_toolCombo->setCurrentIndex(index);
    if (params.values.contains(QStringLiteral("entryMode")))
        m_entryModeCombo->setCurrentIndex(m_entryModeCombo->findData(int(params.get(QStringLiteral("entryMode")))));
    updateProposalState();
}

void StrategyPanel::refreshTemplates()
{
    if (!m_templateCombo) return;
    const bool zh = isChineseUi();
    const QString selected = m_templateCombo->currentData().toString();
    QSignalBlocker blocker(m_templateCombo);
    m_templateCombo->clear();
    m_templateCombo->addItem(zh ? QStringLiteral("选择用户模板") : QStringLiteral("Select user template"), QString());
    QString error;
    const auto rows = ProcessParameterTemplateStore().entries(&error);
    for (const auto &value : rows) {
        const auto row = value.toObject();
        if (row.value("strategyId").toString() == m_currentStrategyId)
            m_templateCombo->addItem(row.value("name").toString(), row.value("name").toString());
    }
    const int index = m_templateCombo->findData(selected);
    m_templateCombo->setCurrentIndex(index >= 0 ? index : 0);
    const bool available = (m_hasFeature || m_hasContourFeature) && !m_currentStrategyId.isEmpty() && error.isEmpty();
    m_templateCombo->setEnabled(available && m_templateCombo->count() > 1);
    m_applyTemplateButton->setEnabled(available && m_templateCombo->currentIndex() > 0);
    m_saveTemplateButton->setEnabled(available);
    m_templateStatus->setText(!error.isEmpty() ? error
        : !(m_hasFeature || m_hasContourFeature)
            ? (zh ? QStringLiteral("先选择几何，再应用模板。") : QStringLiteral("Select geometry before applying a template."))
        : m_templateCombo->count() == 1
            ? (zh ? QStringLiteral("此策略暂无用户模板；编辑切削参数后可保存。") : QStringLiteral("No user templates for this strategy. Edit cutting parameters, then save."))
            : (zh ? QStringLiteral("仅复制切削参数；应用后请复核并确认工序。") : QStringLiteral("Copies cutting parameters only. Review before confirming the operation.")));
}

void StrategyPanel::applySelectedTemplate()
{
    if (!(m_hasFeature || m_hasContourFeature) || m_templateCombo->currentIndex() <= 0) return;
    StrategyParams params = currentParams();
    QString error;
    if (!ProcessParameterTemplateStore().apply(m_templateCombo->currentData().toString(),
                                               m_currentStrategyId, &params.values, &error)) {
        OperationMessageBox::warning(this, isChineseUi() ? QStringLiteral("模板应用失败") : QStringLiteral("Cannot apply template"), error);
        return;
    }
    setCurrentParams(params);
    updatePocketEntryUi();
    updateProposalState();
    m_templateStatus->setText(isChineseUi() ? QStringLiteral("已复制切削参数，可继续编辑；尚未确认工序。")
                                           : QStringLiteral("Cutting parameters copied and editable. Operation remains unconfirmed."));
}

void StrategyPanel::saveCurrentTemplate()
{
    if (!(m_hasFeature || m_hasContourFeature) || m_currentStrategyId.isEmpty()) return;
    bool ok = false;
    const bool zh = isChineseUi();
    const QString name = QInputDialog::getText(this,
        zh ? QStringLiteral("保存工艺模板") : QStringLiteral("Save process template"),
        zh ? QStringLiteral("模板名称（仅保存切削参数）：") : QStringLiteral("Template name (cutting parameters only):"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    QString error;
    if (!ProcessParameterTemplateStore().save(name, m_currentStrategyId, currentParams().values, &error)) {
        OperationMessageBox::warning(this, zh ? QStringLiteral("模板保存失败") : QStringLiteral("Cannot save template"), error);
        return;
    }
    refreshTemplates();
    m_templateCombo->setCurrentIndex(m_templateCombo->findData(name.trimmed()));
    m_templateStatus->setText(zh ? QStringLiteral("模板已保存到本机用户配置目录。") : QStringLiteral("Template saved in the local user configuration folder."));
}

void StrategyPanel::showTemplateManager()
{
    const bool zh = isChineseUi();
    QDialog dialog(this);
    dialog.setWindowTitle(zh ? QStringLiteral("用户工艺模板") : QStringLiteral("User process templates"));
    dialog.resize(520, 380);
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(zh ? QStringLiteral("模板仅保存切削参数。请先选择几何，再在参数窗口应用模板。")
                              : QStringLiteral("Templates contain cutting parameters only. Select geometry, then apply in the parameter window."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *list = new QListWidget(&dialog);
    layout->addWidget(list);
    auto *status = new QLabel(&dialog);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *remove = buttons->addButton(zh ? QStringLiteral("删除所选模板") : QStringLiteral("Delete selected template"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    const auto reload = [=]() {
        list->clear();
        QString error;
        const auto rows = ProcessParameterTemplateStore().entries(&error);
        for (const auto &value : rows) {
            const auto row = value.toObject();
            auto *item = new QListWidgetItem(QStringLiteral("%1 — %2").arg(row.value("name").toString(), displayNameForStrategy(row.value("strategyId").toString())), list);
            item->setData(Qt::UserRole, row.toVariantMap());
        }
        status->setText(!error.isEmpty() ? error : list->count() == 0
            ? (zh ? QStringLiteral("尚未保存用户模板。") : QStringLiteral("No saved user templates.")) : QString());
        remove->setEnabled(false);
    };
    connect(list, &QListWidget::currentRowChanged, &dialog, [=](int row) { remove->setEnabled(row >= 0); });
    connect(remove, &QPushButton::clicked, &dialog, [=]() {
        if (!list->currentItem()) return;
        const auto row = list->currentItem()->data(Qt::UserRole).toMap();
        QString error;
        if (!ProcessParameterTemplateStore().remove(row.value("name").toString(), row.value("strategyId").toString(), &error)) {
            status->setText(error);
            return;
        }
        reload();
        refreshTemplates();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    reload();
    dialog.exec();
}

void StrategyPanel::showMaterialLibrary()
{
    MaterialLibraryDialog dialog(this);
    dialog.exec();
}

void StrategyPanel::reviewMaterialSuggestion()
{
    if (!(m_hasFeature || m_hasContourFeature) || m_currentStrategyId.isEmpty() || currentTool().id <= 0) return;
    const QString strategyId = m_currentStrategyId;
    const int toolId = currentTool().id;
    const StrategyParams before = currentParams();
    MaterialLibraryDialog dialog(this, strategyId, currentTool(), before);
    if (dialog.exec() != QDialog::Accepted) return;
    if (m_currentStrategyId != strategyId || currentTool().id != toolId || currentParams().values != before.values) {
        OperationMessageBox::warning(this, tr("草稿已改变"), tr("当前工序草稿已改变，请重新预览材料建议。"));
        return;
    }
    StrategyParams updated = before;
    updated.values = dialog.appliedParameters();
    setCurrentParams(updated);
    updateParamSummary();
    updateProposalState();
}
