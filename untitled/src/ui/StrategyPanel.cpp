#include "StrategyPanel.h"

#include "CircleMillDialog.h"
#include "MillingOperationDialog.h"
#include "../core/Settings.h"
#include "../strategies/StrategyFactory.h"
#include "../tool/ToolLibrary.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>

namespace {

static bool isChineseUi()
{
    return Settings::instance().language() == QStringLiteral("zh_CN");
}

static double machiningTopZ(const MachiningFeature &feature)
{
    if (feature.kind == FeatureKind::Slot && feature.depth > 0.0) {
        return double(feature.center.z()) + feature.depth * 0.5;
    }
    return double(feature.center.z());
}

static ContourFeature toContourFeature(const MachiningFeature &feature)
{
    ContourFeature contour;
    contour.subType = feature.subType;
    contour.center = feature.center;
    contour.center.setZ(float(machiningTopZ(feature)));
    contour.radius = feature.radius;
    contour.depth = feature.depth;
    contour.width = feature.width;
    contour.length = feature.length;
    contour.angle = feature.angle;
    contour.axis = feature.axis;
    return contour;
}

static bool isSlotFeature(const MachiningFeature &feature)
{
    return feature.kind == FeatureKind::Slot ||
           feature.subType == QStringLiteral("open_slot") ||
           feature.subType == QStringLiteral("blind_slot") ||
           feature.subType == QStringLiteral("straight_slot") ||
           feature.subType == QStringLiteral("arc_slot");
}

static bool isFlatSurfaceFeature(const MachiningFeature &feature)
{
    return feature.kind == FeatureKind::FlatSurface;
}

static QString preferredSlotStrategy(const MachiningFeature &feature)
{
    return feature.subType == QStringLiteral("open_slot")
               ? QStringLiteral("mill_slot")
               : QStringLiteral("mill_blind_slot");
}

static QString strategyDisplayName(const QString &strategyId, bool zh)
{
    if (zh) {
        if (strategyId == QStringLiteral("hole_spot")) return QStringLiteral("定点钻");
        if (strategyId == QStringLiteral("hole_peck")) return QStringLiteral("排屑钻");
        if (strategyId == QStringLiteral("hole_deephole")) return QStringLiteral("深孔钻");
        if (strategyId == QStringLiteral("hole_circular_mill")) return QStringLiteral("圆插补扩孔");
        if (strategyId == QStringLiteral("hole_reaming")) return QStringLiteral("铰孔");
        if (strategyId == QStringLiteral("hole_chamfer")) return QStringLiteral("孔口倒角");
        if (strategyId == QStringLiteral("hole_tapping")) return QStringLiteral("攻丝");
        if (strategyId == QStringLiteral("mill_face")) return QStringLiteral("铣平面");
        if (strategyId == QStringLiteral("mill_surface_finish")) return QStringLiteral("精面铣");
        if (strategyId == QStringLiteral("mill_closed_contour")) return QStringLiteral("封闭轮廓铣");
        if (strategyId == QStringLiteral("mill_open_contour")) return QStringLiteral("开放轮廓铣");
        if (strategyId == QStringLiteral("mill_slot")) return QStringLiteral("开口槽铣");
        if (strategyId == QStringLiteral("mill_blind_slot")) return QStringLiteral("盲槽铣");
    } else {
        if (strategyId == QStringLiteral("hole_spot")) return QStringLiteral("Spot Drilling");
        if (strategyId == QStringLiteral("hole_peck")) return QStringLiteral("Peck Drilling");
        if (strategyId == QStringLiteral("hole_deephole")) return QStringLiteral("Deep-Hole Drilling");
        if (strategyId == QStringLiteral("hole_circular_mill")) return QStringLiteral("Circular Hole Milling");
        if (strategyId == QStringLiteral("hole_reaming")) return QStringLiteral("Reaming");
        if (strategyId == QStringLiteral("hole_chamfer")) return QStringLiteral("Hole Chamfering");
        if (strategyId == QStringLiteral("hole_tapping")) return QStringLiteral("Tapping");
        if (strategyId == QStringLiteral("mill_face")) return QStringLiteral("Face Milling");
        if (strategyId == QStringLiteral("mill_surface_finish")) return QStringLiteral("Surface Finish");
        if (strategyId == QStringLiteral("mill_closed_contour")) return QStringLiteral("Closed Contour Milling");
        if (strategyId == QStringLiteral("mill_open_contour")) return QStringLiteral("Open Contour Milling");
        if (strategyId == QStringLiteral("mill_slot")) return QStringLiteral("Open Slot Milling");
        if (strategyId == QStringLiteral("mill_blind_slot")) return QStringLiteral("Blind Slot Milling");
    }

    auto strategy = StrategyFactory::instance().strategy(strategyId);
    return strategy ? strategy->displayName() : strategyId;
}

} // namespace

StrategyPanel::StrategyPanel(QWidget *parent)
    : QWidget(parent)
    , m_featureLabel(new QLabel(this))
    , m_strategyLabel(new QLabel(this))
    , m_toolLabel(new QLabel(this))
    , m_hintLabel(new QLabel(this))
    , m_strategyCombo(new QComboBox(this))
    , m_toolCombo(new QComboBox(this))
    , m_paramTable(new QTableWidget(this))
    , m_editHoleParamsBtn(new QPushButton(this))
    , m_generateBtn(new QPushButton(this))
    , m_millingGroup(new QGroupBox(this))
    , m_millingBtn(new QPushButton(this))
    , m_circleMillBtn(new QPushButton(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_featureLabel);

    auto *formLayout = new QHBoxLayout;
    formLayout->addWidget(m_strategyLabel);
    formLayout->addWidget(m_strategyCombo, 1);
    layout->addLayout(formLayout);

    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(QStringLiteral("QLabel { color: #555; padding: 4px 2px; }"));
    layout->addWidget(m_hintLabel);

    auto *toolLayout = new QHBoxLayout;
    toolLayout->addWidget(m_toolLabel);
    toolLayout->addWidget(m_toolCombo, 1);
    layout->addLayout(toolLayout);

    m_paramTable->setColumnCount(2);
    m_paramTable->horizontalHeader()->setStretchLastSection(true);
    m_paramTable->verticalHeader()->setVisible(false);
    layout->addWidget(m_paramTable, 1);

    layout->addWidget(m_editHoleParamsBtn);
    layout->addWidget(m_generateBtn);

    auto *millingLayout = new QVBoxLayout(m_millingGroup);
    millingLayout->setContentsMargins(6, 6, 6, 6);
    millingLayout->addWidget(m_millingBtn);
    millingLayout->addWidget(m_circleMillBtn);
    layout->addWidget(m_millingGroup);

    connect(m_strategyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StrategyPanel::onStrategyChanged);
    connect(m_editHoleParamsBtn, &QPushButton::clicked,
            this, &StrategyPanel::onEditHoleParams);
    connect(m_generateBtn, &QPushButton::clicked,
            this, &StrategyPanel::onGenerate);
    connect(m_circleMillBtn, &QPushButton::clicked,
            this, &StrategyPanel::onCircleMillClicked);
    connect(m_millingBtn, &QPushButton::clicked,
            this, &StrategyPanel::onMillingClicked);

    populateStrategies();
    retranslateUi();
}

QStringList StrategyPanel::strategyIdsForCurrentContext() const
{
    if (m_hasContourFeature) {
        if (isFlatSurfaceFeature(m_contourFeature)) {
            return {
                QStringLiteral("mill_face"),
                QStringLiteral("mill_surface_finish")
            };
        }

        if (isSlotFeatureSelected()) {
            return {
                QStringLiteral("mill_slot"),
                QStringLiteral("mill_blind_slot")
            };
        }

        return {
            QStringLiteral("mill_closed_contour"),
            QStringLiteral("mill_open_contour")
        };
    }

    return {
        QStringLiteral("hole_spot"),
        QStringLiteral("hole_peck"),
        QStringLiteral("hole_deephole"),
        QStringLiteral("hole_circular_mill"),
        QStringLiteral("hole_reaming"),
        QStringLiteral("hole_chamfer"),
        QStringLiteral("hole_tapping")
    };
}

QString StrategyPanel::displayNameForStrategy(const QString &strategyId) const
{
    return strategyDisplayName(strategyId, isChineseUi());
}

bool StrategyPanel::isSlotFeatureSelected() const
{
    return m_hasContourFeature && isSlotFeature(m_contourFeature);
}

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
    updateStrategyHint();
}

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
}

void StrategyPanel::selectPreferredStrategyForCurrentContext()
{
    QString preferred;
    if (m_hasContourFeature) {
        if (isFlatSurfaceFeature(m_contourFeature)) {
            preferred = QStringLiteral("mill_face");
        } else if (isSlotFeatureSelected()) {
            preferred = preferredSlotStrategy(m_contourFeature);
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

void StrategyPanel::onStrategyChanged(int)
{
    cacheCurrentParams();
    m_currentStrategyId = m_strategyCombo->currentData().toString();
    populateTools();
    refreshParamTable();
    updateStrategyHint();
}

void StrategyPanel::refreshParamTable()
{
    m_paramTable->setRowCount(0);
    const QString id = m_strategyCombo->currentData().toString();
    auto strategy = StrategyFactory::instance().strategy(id);
    StrategyBase *s = strategy.get();
    if (!s) {
        return;
    }

    StrategyParams defaults = m_userParams.contains(id)
        ? m_userParams.value(id)
        : s->defaultParams();

    if (!m_userParams.contains(id) &&
        m_hasFeature &&
        id != QStringLiteral("hole_spot") &&
        m_feature.depth > 0.0 &&
        defaults.values.contains(QStringLiteral("depth"))) {
        defaults.set(QStringLiteral("depth"), m_feature.depth);
    }

    if (!m_userParams.contains(id) && m_hasContourFeature) {
        if (defaults.values.contains(QStringLiteral("depth")) && m_contourFeature.depth > 0.0) {
            defaults.set(QStringLiteral("depth"), m_contourFeature.depth);
        }
        if (isSlotFeatureSelected()) {
            defaults.set(QStringLiteral("slotLength"),
                         m_contourFeature.length > 0.0 ? m_contourFeature.length
                                                       : m_contourFeature.radius * 2.0);
            defaults.set(QStringLiteral("slotWidth"),
                         m_contourFeature.width > 0.0 ? m_contourFeature.width
                                                      : m_contourFeature.radius);
            defaults.set(QStringLiteral("angle"), m_contourFeature.angle);
        }
    }

    int row = 0;
    for (auto it = defaults.values.cbegin(); it != defaults.values.cend(); ++it, ++row) {
        m_paramTable->insertRow(row);
        auto *nameItem = new QTableWidgetItem(paramDisplayName(it.key()));
        nameItem->setData(Qt::UserRole, it.key());
        nameItem->setFlags(Qt::ItemIsEnabled);
        m_paramTable->setItem(row, 0, nameItem);
        m_paramTable->setItem(row, 1, new QTableWidgetItem(it.value().toString()));
    }
}

StrategyParams StrategyPanel::currentParams() const
{
    StrategyParams params;
    for (int row = 0; row < m_paramTable->rowCount(); ++row) {
        const QTableWidgetItem *keyItem = m_paramTable->item(row, 0);
        const QTableWidgetItem *valueItem = m_paramTable->item(row, 1);
        if (!keyItem || !valueItem) {
            continue;
        }
        params.set(keyItem->data(Qt::UserRole).toString(), valueItem->text().toDouble());
    }
    return params;
}

ContourFeature StrategyPanel::currentContourFeature() const
{
    ContourFeature contour = toContourFeature(m_contourFeature);
    const StrategyParams params = currentParams();

    if (params.values.contains(QStringLiteral("depth"))) {
        contour.depth = params.get(QStringLiteral("depth"), contour.depth);
    }
    if (params.values.contains(QStringLiteral("slotLength"))) {
        contour.length = params.get(QStringLiteral("slotLength"), contour.length);
    }
    if (params.values.contains(QStringLiteral("slotWidth"))) {
        contour.width = params.get(QStringLiteral("slotWidth"), contour.width);
    }
    if (params.values.contains(QStringLiteral("angle"))) {
        contour.angle = params.get(QStringLiteral("angle"), contour.angle);
    }
    return contour;
}

void StrategyPanel::cacheCurrentParams()
{
    if (m_currentStrategyId.isEmpty() || m_paramTable->rowCount() <= 0) {
        return;
    }
    m_userParams[m_currentStrategyId] = currentParams();
}

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

        if (params.values.contains(QStringLiteral("depth"))) {
            params.set(QStringLiteral("depth"), m_contourFeature.depth);
        }
        if (isSlotFeatureSelected()) {
            params.set(QStringLiteral("slotLength"),
                       m_contourFeature.length > 0.0 ? m_contourFeature.length
                                                     : m_contourFeature.radius * 2.0);
            params.set(QStringLiteral("slotWidth"),
                       m_contourFeature.width > 0.0 ? m_contourFeature.width
                                                    : m_contourFeature.radius);
            params.set(QStringLiteral("angle"), m_contourFeature.angle);
        }

        m_userParams[strategyId] = params;
    }
}

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

void StrategyPanel::clearContourFeature()
{
    m_hasContourFeature = false;
    m_contourFeature = MachiningFeature();
    if (!m_hasFeature) {
        populateStrategies();
    }
    retranslateUi();
}

void StrategyPanel::setFeature(const HoleFeature &feature)
{
    m_feature = feature;
    m_hasFeature = true;
    m_contourFeature = MachiningFeature();
    m_hasContourFeature = false;
    populateStrategies();
    retranslateUi();
}

void StrategyPanel::clearFeature()
{
    m_hasFeature = false;
    m_feature = HoleFeature();
    if (!m_hasContourFeature) {
        populateStrategies();
    }
    retranslateUi();
}

void StrategyPanel::onGenerate()
{
    const QString strategyId = m_strategyCombo->currentData().toString();
    if (strategyId.isEmpty()) {
        return;
    }

    cacheCurrentParams();
    if (m_hasFeature) {
        emit generateRequested(m_feature, strategyId, currentParams(), currentTool());
    } else if (m_hasContourFeature) {
        emit millingRequested(currentContourFeature(), strategyId, currentParams(), currentTool());
    }
}

void StrategyPanel::onEditHoleParams()
{
    const bool zh = isChineseUi();
    QDialog dialog(this);
    dialog.setWindowTitle(zh ? QStringLiteral("编辑孔加工参数")
                             : QStringLiteral("Edit Hole Parameters"));
    dialog.setModal(true);

    auto *table = new QTableWidget(&dialog);
    table->setColumnCount(2);
    table->setRowCount(m_paramTable->rowCount());
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setHorizontalHeaderLabels({
        zh ? QStringLiteral("参数") : QStringLiteral("Parameter"),
        zh ? QStringLiteral("数值") : QStringLiteral("Value")
    });

    for (int row = 0; row < m_paramTable->rowCount(); ++row) {
        const QTableWidgetItem *srcName = m_paramTable->item(row, 0);
        const QTableWidgetItem *srcValue = m_paramTable->item(row, 1);

        auto *nameItem = new QTableWidgetItem(srcName ? srcName->text() : QString());
        nameItem->setData(Qt::UserRole, srcName ? srcName->data(Qt::UserRole) : QVariant());
        nameItem->setFlags(Qt::ItemIsEnabled);
        table->setItem(row, 0, nameItem);
        table->setItem(row, 1, new QTableWidgetItem(srcValue ? srcValue->text() : QString()));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(zh ? QStringLiteral("确定")
                                                     : QStringLiteral("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(zh ? QStringLiteral("取消")
                                                         : QStringLiteral("Cancel"));

    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(table);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    for (int row = 0; row < table->rowCount() && row < m_paramTable->rowCount(); ++row) {
        const QTableWidgetItem *valueItem = table->item(row, 1);
        if (valueItem && m_paramTable->item(row, 1)) {
            m_paramTable->item(row, 1)->setText(valueItem->text());
        }
    }
    cacheCurrentParams();
}

void StrategyPanel::onCircleMillClicked()
{
    CircleMillDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    emit circleMillRequested(dialog.feature(), dialog.params(), currentTool());
}

void StrategyPanel::onMillingClicked()
{
    if (m_hasContourFeature) {
        const QString strategyId = m_strategyCombo->currentData().toString();
        if (strategyId.isEmpty()) {
            return;
        }
        cacheCurrentParams();
        emit millingRequested(currentContourFeature(), strategyId, currentParams(), currentTool());
        return;
    }

    MillingOperationDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    emit millingRequested(dialog.feature(), dialog.strategyId(), dialog.params(), dialog.tool());
}

void StrategyPanel::retranslateUi()
{
    const bool zh = isChineseUi();

    if (m_hasFeature) {
        m_featureLabel->setText(
            zh ? QStringLiteral("当前特征：孔径 %1，深度 %2")
                     .arg(m_feature.radius * 2.0, 0, 'f', 2)
                     .arg(m_feature.depth, 0, 'f', 2)
               : QStringLiteral("Current feature: D%1, depth %2")
                     .arg(m_feature.radius * 2.0, 0, 'f', 2)
                     .arg(m_feature.depth, 0, 'f', 2));
    } else if (m_hasContourFeature) {
        if (isFlatSurfaceFeature(m_contourFeature)) {
            m_featureLabel->setText(
                zh ? QStringLiteral("当前特征：平面 L%1，W%2，深度 %3")
                         .arg(m_contourFeature.length, 0, 'f', 2)
                         .arg(m_contourFeature.width, 0, 'f', 2)
                         .arg(m_contourFeature.depth, 0, 'f', 2)
                   : QStringLiteral("Current feature: surface L%1 W%2 depth %3")
                         .arg(m_contourFeature.length, 0, 'f', 2)
                         .arg(m_contourFeature.width, 0, 'f', 2)
                         .arg(m_contourFeature.depth, 0, 'f', 2));
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
    m_editHoleParamsBtn->setText(zh ? QStringLiteral("编辑孔加工参数")
                                    : QStringLiteral("Edit Hole Parameters"));
    m_generateBtn->setText(zh ? QStringLiteral("生成 G 代码")
                              : QStringLiteral("Generate G-code"));
    m_millingGroup->setTitle(zh ? QStringLiteral("铣削") : QStringLiteral("Milling"));
    m_millingBtn->setText(
        m_hasContourFeature && isSlotFeatureSelected()
            ? (zh ? QStringLiteral("加入槽工序") : QStringLiteral("Add Slot Operation"))
            : (zh ? QStringLiteral("加入铣削工序") : QStringLiteral("Add Milling Operation")));
    m_circleMillBtn->setText(zh ? QStringLiteral("圆边铣削") : QStringLiteral("Mill Edge (Circle)"));
    m_paramTable->setHorizontalHeaderLabels({
        zh ? QStringLiteral("参数") : QStringLiteral("Parameter"),
        zh ? QStringLiteral("数值") : QStringLiteral("Value")
    });

    for (int i = 0; i < m_strategyCombo->count(); ++i) {
        m_strategyCombo->setItemText(i, displayNameForStrategy(m_strategyCombo->itemData(i).toString()));
    }

    m_editHoleParamsBtn->setVisible(m_hasFeature);
    m_generateBtn->setEnabled(m_hasFeature || m_hasContourFeature);
    m_millingGroup->setVisible(m_hasContourFeature);
    m_millingBtn->setVisible(m_hasContourFeature);
    m_circleMillBtn->setVisible(m_hasContourFeature && !isSlotFeatureSelected());

    populateTools();
    refreshParamTable();
    updateStrategyHint();
}

QString StrategyPanel::paramDisplayName(const QString &key) const
{
    if (!isChineseUi()) {
        return key;
    }
    if (key == QStringLiteral("safeHeight")) return QStringLiteral("安全高度");
    if (key == QStringLiteral("feedHeight")) return QStringLiteral("进给起始高度");
    if (key == QStringLiteral("depth")) return QStringLiteral("加工深度");
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
    if (key == QStringLiteral("chamferWidth")) return QStringLiteral("倒角宽度");
    if (key == QStringLiteral("chamferAngle")) return QStringLiteral("倒角角度");
    if (key == QStringLiteral("compensation")) return QStringLiteral("刀补方向");
    if (key == QStringLiteral("stockToLeave")) return QStringLiteral("余量");
    if (key == QStringLiteral("leadLength")) return QStringLiteral("引入引出长度");
    if (key == QStringLiteral("slotLength")) return QStringLiteral("槽长");
    if (key == QStringLiteral("slotWidth")) return QStringLiteral("槽宽");
    if (key == QStringLiteral("angle")) return QStringLiteral("方向角");
    if (key == QStringLiteral("helixRadius")) return QStringLiteral("螺旋下刀半径");
    if (key == QStringLiteral("helixPitch")) return QStringLiteral("螺旋节距");
    return key;
}

QString StrategyPanel::toolDisplayName(const ToolEntry &tool) const
{
    return isChineseUi()
        ? QStringLiteral("T%1 %2 直径%3").arg(tool.id).arg(tool.name).arg(tool.diameter, 0, 'f', 1)
        : QStringLiteral("T%1 %2 D%3").arg(tool.id).arg(tool.name).arg(tool.diameter, 0, 'f', 1);
}

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

QString StrategyPanel::strategyHint(const QString &strategyId) const
{
    const bool zh = isChineseUi();
    if (!zh) {
        if (strategyId == QStringLiteral("hole_spot"))
            return QStringLiteral("Spot drill for positioning only. Keep the depth shallow.");
        if (strategyId == QStringLiteral("hole_peck"))
            return QStringLiteral("Peck drilling for pilot or rough holes with chip evacuation.");
        if (strategyId == QStringLiteral("hole_deephole"))
            return QStringLiteral("Deep-hole drilling for large depth/diameter ratios.");
        if (strategyId == QStringLiteral("hole_circular_mill"))
            return QStringLiteral("Mill a drilled pilot hole to final diameter with an end mill.");
        if (strategyId == QStringLiteral("hole_reaming"))
            return QStringLiteral("Use a reamer close to the target hole diameter.");
        if (strategyId == QStringLiteral("hole_chamfer"))
            return QStringLiteral("Chamfer or deburr the hole after drilling and finishing.");
        if (strategyId == QStringLiteral("hole_tapping"))
            return QStringLiteral("Tapping uses pitch-based feed. Match it to the thread hole.");
        if (strategyId == QStringLiteral("mill_slot"))
            return QStringLiteral("Open-slot milling uses the selected slot geometry and shared roughing parameters.");
        if (strategyId == QStringLiteral("mill_blind_slot"))
            return QStringLiteral("Blind-slot milling keeps the slot closed at both ends and supports helix entry.");
        if (strategyId == QStringLiteral("mill_face"))
            return QStringLiteral("Face milling uses the recognized planar length and width to generate a raster pass.");
        if (strategyId == QStringLiteral("mill_surface_finish"))
            return QStringLiteral("Surface finish uses the same planar area with a finer stepover for finishing.");
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
    if (strategyId == QStringLiteral("hole_circular_mill"))
        return QStringLiteral("圆插补扩孔用于把底孔铣到最终孔径。");
    if (strategyId == QStringLiteral("hole_reaming"))
        return QStringLiteral("铰孔要求铰刀直径接近目标孔径。");
    if (strategyId == QStringLiteral("hole_chamfer"))
        return QStringLiteral("孔口倒角应放在钻孔和精加工之后。");
    if (strategyId == QStringLiteral("hole_tapping"))
        return QStringLiteral("攻丝按主轴转速和螺距自动匹配进给。");
    if (strategyId == QStringLiteral("mill_slot"))
        return QStringLiteral("开口槽铣会直接使用当前槽长、槽宽、槽深和方向角。");
    if (strategyId == QStringLiteral("mill_blind_slot"))
        return QStringLiteral("盲槽铣适合两端封闭的槽，可配合螺旋下刀。");
    if (strategyId == QStringLiteral("mill_face"))
        return QStringLiteral("平面铣会直接使用识别到的平面长宽生成往复面铣刀路。");
    if (strategyId == QStringLiteral("mill_surface_finish"))
        return QStringLiteral("精面铣会在同一平面区域内使用更小步距生成精加工刀路。");
    if (strategyId == QStringLiteral("mill_closed_contour"))
        return QStringLiteral("封闭轮廓铣用于外形或闭合边界精修。");
    if (strategyId == QStringLiteral("mill_open_contour"))
        return QStringLiteral("开放轮廓铣用于开放边界或局部轮廓。");
    return QStringLiteral("生成前请确认刀具、深度和进给参数。");
}

void StrategyPanel::updateStrategyHint()
{
    if (m_hintLabel) {
        m_hintLabel->setText(strategyHint(m_strategyCombo->currentData().toString()));
    }
}

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
                 strategyId == QStringLiteral("hole_deephole")) &&
                tool.diameter > targetDiameter) {
                score += 1.0e6;
            }
            if (strategyId == QStringLiteral("hole_circular_mill") && tool.diameter >= targetDiameter) {
                score += 1.0e6;
            }
        } else if (isSlotFeatureSelected()) {
            const double slotWidth = m_contourFeature.width > 0.0
                ? m_contourFeature.width
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

ToolEntry StrategyPanel::currentTool() const
{
    const int toolId = m_toolCombo->currentData().toInt();
    return ToolLibrary::instance().tool(toolId);
}
