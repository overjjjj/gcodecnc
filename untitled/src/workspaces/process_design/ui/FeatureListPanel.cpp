#include "FeatureListPanel.h"
#include "ContourFeatureGrouping.h"
#include "HoleFeatureGrouping.h"

#include "../../../core/Settings.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QHeaderView>
#include <QGridLayout>
#include <QMap>
#include <QStringList>
#include <QToolButton>
#include <QTabBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

// 函数说明：isChineseUi负责当前模块的核心处理。
static bool isChineseUi()
{
    return Settings::instance().language() == QStringLiteral("zh_CN");
}

// 函数说明：featureKindLabel负责当前模块的核心处理。
static QString featureKindLabel(FeatureKind kind)
{
    const bool zh = isChineseUi();
    switch (kind) {
    case FeatureKind::Hole:        return zh ? QStringLiteral("孔") : QStringLiteral("Hole");
    case FeatureKind::Slot:        return zh ? QStringLiteral("槽") : QStringLiteral("Slot");
    case FeatureKind::Pocket:      return zh ? QStringLiteral("型腔") : QStringLiteral("Pocket");
    case FeatureKind::Boss:        return zh ? QStringLiteral("凸台") : QStringLiteral("Boss");
    case FeatureKind::Chamfer:     return zh ? QStringLiteral("倒角") : QStringLiteral("Chamfer");
    case FeatureKind::Fillet:      return zh ? QStringLiteral("圆角") : QStringLiteral("Fillet");
    case FeatureKind::FlatSurface: return zh ? QStringLiteral("平面") : QStringLiteral("Flat Surface");
    case FeatureKind::Thread:      return zh ? QStringLiteral("螺纹孔") : QStringLiteral("Thread");
    }
    return zh ? QStringLiteral("未知") : QStringLiteral("Unknown");
}

// 函数说明：regionLabel负责当前模块的核心处理。
static QString regionLabel(FaceRegion region)
{
    const bool zh = isChineseUi();
    switch (region) {
    case FaceRegion::Front: return zh ? QStringLiteral("正面") : QStringLiteral("Front");
    case FaceRegion::Side:  return zh ? QStringLiteral("侧面") : QStringLiteral("Side");
    case FaceRegion::Back:  return zh ? QStringLiteral("反面") : QStringLiteral("Back");
    case FaceRegion::Unknown:
    default:
        return zh ? QStringLiteral("未知面") : QStringLiteral("Unknown");
    }
}

// 函数说明：featureSubTypeLabel负责当前模块的核心处理。
static QString featureSubTypeLabel(const MachiningFeature &feature)
{
    const bool zh = isChineseUi();
    if (feature.subType == QStringLiteral("through_hole")) return zh ? QStringLiteral("通孔") : QStringLiteral("Through Hole");
    if (feature.subType == QStringLiteral("blind_hole")) return zh ? QStringLiteral("盲孔") : QStringLiteral("Blind Hole");
    if (feature.subType == QStringLiteral("enclosed_hole")) return zh ? QStringLiteral("封闭内腔（无开口）") : QStringLiteral("Enclosed Cavity (No Opening)");
    if (feature.subType == QStringLiteral("countersunk_through_hole")) return zh ? QStringLiteral("沉头通孔") : QStringLiteral("Countersunk Through Hole");
    if (feature.subType == QStringLiteral("countersunk_blind_hole")) return zh ? QStringLiteral("沉头盲孔") : QStringLiteral("Countersunk Blind Hole");
    if (feature.subType == QStringLiteral("tapped_hole")) return zh ? QStringLiteral("螺纹孔") : QStringLiteral("Tapped Hole");
    if (feature.subType == QStringLiteral("straight_slot")) return zh ? QStringLiteral("直线槽") : QStringLiteral("Straight Slot");
    if (feature.subType == QStringLiteral("arc_slot")) return zh ? QStringLiteral("圆弧槽") : QStringLiteral("Arc Slot");
    if (feature.subType == QStringLiteral("open_slot")) return zh ? QStringLiteral("开口槽") : QStringLiteral("Open Slot");
    if (feature.subType == QStringLiteral("slot")) return zh ? QStringLiteral("槽") : QStringLiteral("Slot");
    if (feature.subType == QStringLiteral("rectangular_pocket")) return zh ? QStringLiteral("矩形腔") : QStringLiteral("Rectangular Pocket");
    if (feature.subType == QStringLiteral("circular_pocket")) return zh ? QStringLiteral("圆形腔") : QStringLiteral("Circular Pocket");
    if (feature.subType == QStringLiteral("irregular_pocket")) return zh ? QStringLiteral("异形腔") : QStringLiteral("Irregular Pocket");
    if (feature.subType == QStringLiteral("pocket")) return zh ? QStringLiteral("型腔") : QStringLiteral("Pocket");
    if (feature.subType == QStringLiteral("circular_boss")) return zh ? QStringLiteral("圆形凸台") : QStringLiteral("Circular Boss");
    if (feature.subType == QStringLiteral("boss_like_cylindrical_face")) return zh ? QStringLiteral("凸台候选") : QStringLiteral("Boss Candidate");
    if (feature.subType == QStringLiteral("top_surface")) return zh ? QStringLiteral("顶面") : QStringLiteral("Top Surface");
    if (feature.subType == QStringLiteral("step_surface")) return zh ? QStringLiteral("台阶面") : QStringLiteral("Step Surface");
    if (feature.subType == QStringLiteral("bottom_surface")) return zh ? QStringLiteral("底面") : QStringLiteral("Bottom Surface");
    if (feature.subType == QStringLiteral("side_surface")) return zh ? QStringLiteral("侧壁面") : QStringLiteral("Side Surface");
    if (feature.subType == QStringLiteral("inner_chamfer")) return zh ? QStringLiteral("内倒角") : QStringLiteral("Inner Chamfer");
    if (feature.subType == QStringLiteral("outer_chamfer")) return zh ? QStringLiteral("外倒角") : QStringLiteral("Outer Chamfer");
    if (feature.subType == QStringLiteral("inner_fillet")) return zh ? QStringLiteral("内圆角") : QStringLiteral("Inner Fillet");
    if (feature.subType == QStringLiteral("outer_fillet")) return zh ? QStringLiteral("外圆角") : QStringLiteral("Outer Fillet");
    return feature.subType;
}

// 函数说明：featureSummary负责当前模块的核心处理。
static QString featureSummary(const MachiningFeature &feature, int index)
{
    const bool zh = isChineseUi();
    QString label = QStringLiteral("#%1  %2").arg(index + 1).arg(featureSubTypeLabel(feature));
    if (feature.region != FaceRegion::Unknown) {
        label += QStringLiteral("  %1").arg(regionLabel(feature.region));
    }
    if (feature.radius > 0.0) {
        label += zh
            ? QStringLiteral("  直径%1").arg(feature.radius * 2.0, 0, 'f', 2)
            : QStringLiteral("  D%1").arg(feature.radius * 2.0, 0, 'f', 2);
    }
    if (feature.secondaryRadius > 0.0 && std::abs(feature.secondaryRadius - feature.radius) > 1.0e-6) {
        label += zh
            ? QStringLiteral("  大径%1").arg(feature.secondaryRadius * 2.0, 0, 'f', 2)
            : QStringLiteral("  Major D%1").arg(feature.secondaryRadius * 2.0, 0, 'f', 2);
    }
    if (feature.pitch > 0.0) {
        label += zh
            ? QStringLiteral("  螺距%1").arg(feature.pitch, 0, 'f', 2)
            : QStringLiteral("  Pitch %1").arg(feature.pitch, 0, 'f', 2);
    }
    if (feature.width > 0.0) {
        label += zh
            ? QStringLiteral("  宽%1").arg(feature.width, 0, 'f', 2)
            : QStringLiteral("  W%1").arg(feature.width, 0, 'f', 2);
    }
    if (feature.length > 0.0) {
        label += zh
            ? QStringLiteral("  长%1").arg(feature.length, 0, 'f', 2)
            : QStringLiteral("  L%1").arg(feature.length, 0, 'f', 2);
    }
    if (feature.depth > 0.0) {
        label += zh
            ? QStringLiteral("  深%1").arg(feature.depth, 0, 'f', 2)
            : QStringLiteral("  Depth %1").arg(feature.depth, 0, 'f', 2);
    }
    if (!feature.boundaryPoints.isEmpty()) {
        label += zh
            ? QStringLiteral("  边界%1点").arg(feature.boundaryPoints.size())
            : QStringLiteral("  Boundary %1 pts").arg(feature.boundaryPoints.size());
    }
    if (!feature.islandBoundaries.isEmpty()) {
        label += zh
            ? QStringLiteral("  孤岛%1").arg(feature.islandBoundaries.size())
            : QStringLiteral("  Islands %1").arg(feature.islandBoundaries.size());
    }
    return label;
}

// 函数说明：findFeatureItem负责当前模块的核心处理。
static QTreeWidgetItem *findFeatureItem(QTreeWidgetItem *item, int index)
{
    if (!item) {
        return nullptr;
    }
    if (item->data(0, Qt::UserRole).toInt() == index) {
        return item;
    }
    for (int i = 0; i < item->childCount(); ++i) {
        if (QTreeWidgetItem *found = findFeatureItem(item->child(i), index)) {
            return found;
        }
    }
    return nullptr;
}

// 函数说明：indexListToStrings负责当前模块的核心处理。
static QStringList indexListToStrings(const QVector<int> &indices)
{
    QStringList values;
    values.reserve(indices.size());
    for (int index : indices) {
        values.append(QString::number(index));
    }
    return values;
}

// 函数说明：stringsToIndexList负责当前模块的核心处理。
static QVector<int> stringsToIndexList(const QStringList &values)
{
    QVector<int> indices;
    indices.reserve(values.size());
    for (const QString &value : values) {
        bool ok = false;
        const int index = value.toInt(&ok);
        if (ok) {
            indices.append(index);
        }
    }
    return indices;
}

} // namespace

// 函数说明：FeatureListPanel负责当前模块的核心处理。
FeatureListPanel::FeatureListPanel(QWidget *parent)
    // 函数说明：QWidget负责当前模块的核心处理。
    : QWidget(parent)
    // 函数说明：m_titleLabel负责当前模块的核心处理。
    , m_titleLabel(new QLabel(this))
    // 函数说明：m_tree负责当前模块的核心处理。
    , m_tree(new QTreeWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);
    layout->addWidget(m_titleLabel);

    auto *filterLayout = new QGridLayout();
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(4);
    m_filterAllButton = new QToolButton(this);
    m_filterAllButton->setObjectName(QStringLiteral("featureFilterAll"));
    m_filterHoleButton = new QToolButton(this);
    m_filterHoleButton->setObjectName(QStringLiteral("featureFilterHole"));
    m_filterSlotButton = new QToolButton(this);
    m_filterSlotButton->setObjectName(QStringLiteral("featureFilterSlot"));
    m_filterPlaneButton = new QToolButton(this);
    m_filterPlaneButton->setObjectName(QStringLiteral("featureFilterPlane"));
    m_filterContourButton = new QToolButton(this);
    m_filterContourButton->setObjectName(QStringLiteral("featureFilterContour"));
    const QList<QToolButton*> filterButtons = {
        m_filterAllButton, m_filterHoleButton, m_filterSlotButton,
        m_filterPlaneButton, m_filterContourButton
    };
    auto *filterGroup = new QButtonGroup(this);
    filterGroup->setExclusive(true);
    for (QToolButton *button : filterButtons) {
        button->setProperty("featureFilter", true);
        button->setCheckable(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        filterGroup->addButton(button);
        const int index = filterButtons.indexOf(button);
        filterLayout->addWidget(button, index / 2, index % 2);
    }
    m_filterAllButton->setChecked(true);
    layout->addLayout(filterLayout);
    layout->addWidget(m_tree);

    m_tree->setColumnCount(1);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);

    // 函数说明：connect负责当前模块的核心处理。
    connect(m_filterAllButton, &QToolButton::clicked, this,
            [this]() { setFeatureFilter(FeatureDisplayFilter::All); });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_filterHoleButton, &QToolButton::clicked, this,
            [this]() { setFeatureFilter(FeatureDisplayFilter::Hole); });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_filterSlotButton, &QToolButton::clicked, this,
            [this]() { setFeatureFilter(FeatureDisplayFilter::SlotPocket); });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_filterPlaneButton, &QToolButton::clicked, this,
            [this]() { setFeatureFilter(FeatureDisplayFilter::Plane); });
    // 函数说明：connect负责当前模块的核心处理。
    connect(m_filterContourButton, &QToolButton::clicked, this,
            [this]() { setFeatureFilter(FeatureDisplayFilter::ContourOther); });

    // 函数说明：connect负责当前模块的核心处理。
    connect(m_tree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
        if (!current) {
            return;
        }
        const int featureIndex = current->data(0, Qt::UserRole).toInt();
        if (featureIndex >= 0) {
            emit featureSelected(featureIndex);
            return;
        }

        const QVector<int> groupIndices = stringsToIndexList(current->data(0, Qt::UserRole + 1).toStringList());
        if (!groupIndices.isEmpty()) {
            emit featureGroupSelected(groupIndices);
        }
    });

    // 函数说明：connect负责当前模块的核心处理。
    connect(m_tree, &QTreeWidget::itemChanged,
            this, [this](QTreeWidgetItem *item, int column) {
        if (m_updatingTree || !item || column != 0) {
            return;
        }

        const int featureIndex = item->data(0, Qt::UserRole).toInt();
        if (featureIndex < 0) {
            const QVector<int> groupIndices = stringsToIndexList(
                item->data(0, Qt::UserRole + 1).toStringList());
            if (groupIndices.isEmpty() || item->checkState(0) == Qt::PartiallyChecked) {
                return;
            }
            const bool checked = item->checkState(0) == Qt::Checked;
            m_updatingTree = true;
            for (int index : groupIndices) {
                if (checked) {
                    m_checkedFeatureIndices.insert(index);
                } else {
                    m_checkedFeatureIndices.remove(index);
                }
            }
            for (int child = 0; child < item->childCount(); ++child) {
                item->child(child)->setCheckState(
                    0, checked ? Qt::Checked : Qt::Unchecked);
            }
            m_updatingTree = false;
            emit checkedFeaturesChanged(checkedFeatureIndices());
            return;
        }

        if (item->checkState(0) == Qt::Checked) {
            m_checkedFeatureIndices.insert(featureIndex);
            if (m_tree->currentItem() != item) {
                m_tree->setCurrentItem(item);
            }
        } else {
            m_checkedFeatureIndices.remove(featureIndex);
        }
        emit checkedFeaturesChanged(checkedFeatureIndices());
    });

    retranslateUi();
}

// 函数说明：setFeatures负责当前模块的核心处理。
void FeatureListPanel::setFeatures(const QVector<MachiningFeature> &features)
{
    m_features = features;
    for (auto it = m_checkedFeatureIndices.begin(); it != m_checkedFeatureIndices.end(); ) {
        if (*it < 0 || *it >= m_features.size()) {
            it = m_checkedFeatureIndices.erase(it);
        } else {
            ++it;
        }
    }

    m_updatingTree = true;
    m_tree->clear();

    QMap<int, QMap<QString, QVector<int>>> regionGroups;
    for (int i = 0; i < m_features.size(); ++i) {
        if (!matchesFeatureDisplayFilter(m_features[i].kind, m_featureFilter)) {
            continue;
        }
        const MachiningFeature &feature = m_features[i];
        const QString holeGroup = holeFeatureGroupLabel(feature, isChineseUi());
        const QString contourGroup = contourFeatureGroupLabel(feature, isChineseUi());
        const QString groupLabel = !holeGroup.isEmpty()
            ? holeGroup
            : (!contourGroup.isEmpty() ? contourGroup : featureKindLabel(feature.kind));
        regionGroups[int(feature.region)][groupLabel].append(i);
    }

    const QVector<FaceRegion> regionOrder = {
        FaceRegion::Front,
        FaceRegion::Side,
        FaceRegion::Back,
        FaceRegion::Unknown
    };
    for (FaceRegion region : regionOrder) {
        auto regionIt = regionGroups.constFind(int(region));
        if (regionIt == regionGroups.cend()) {
            continue;
        }

        int regionCount = 0;
        for (auto kindIt = regionIt.value().cbegin(); kindIt != regionIt.value().cend(); ++kindIt) {
            regionCount += kindIt.value().size();
        }

        auto *regionItem = new QTreeWidgetItem(m_tree);
        regionItem->setText(0, QStringLiteral("%1 (%2)").arg(regionLabel(region)).arg(regionCount));
        regionItem->setData(0, Qt::UserRole, -1);
        regionItem->setData(0, Qt::UserRole + 2, int(region));
        QVector<int> regionIndices;
        for (auto kindIt = regionIt.value().cbegin(); kindIt != regionIt.value().cend(); ++kindIt) {
            regionIndices += kindIt.value();
        }
        regionItem->setData(0, Qt::UserRole + 1, indexListToStrings(regionIndices));
        regionItem->setFirstColumnSpanned(true);
        regionItem->setExpanded(m_activeRegion == FaceRegion::Unknown ||
                                region == m_activeRegion);

        for (auto kindIt = regionIt.value().cbegin(); kindIt != regionIt.value().cend(); ++kindIt) {
            auto *kindItem = new QTreeWidgetItem(regionItem);
            kindItem->setText(0, QStringLiteral("%1 (%2)").arg(kindIt.key()).arg(kindIt.value().size()));
            kindItem->setData(0, Qt::UserRole, -1);
            kindItem->setData(0, Qt::UserRole + 1, indexListToStrings(kindIt.value()));
            kindItem->setFlags(kindItem->flags()
                               | Qt::ItemIsUserCheckable
                               | Qt::ItemIsAutoTristate);
            kindItem->setCheckState(0, Qt::Unchecked);
            kindItem->setToolTip(
                0, isChineseUi()
                    ? QStringLiteral("勾选此组可批量选择相同类型和尺寸的加工对象。")
                    : QStringLiteral("Check this group to select machining targets with matching type and dimensions."));
            kindItem->setFirstColumnSpanned(true);
            kindItem->setExpanded(m_activeRegion == FaceRegion::Unknown ||
                                  region == m_activeRegion);

            for (int index : kindIt.value()) {
                const MachiningFeature &feature = m_features[index];
                auto *featureItem = new QTreeWidgetItem(kindItem);
                featureItem->setText(0, featureSummary(feature, index));
                featureItem->setData(0, Qt::UserRole, index);
                featureItem->setFlags(featureItem->flags() | Qt::ItemIsUserCheckable);
                featureItem->setCheckState(0, m_checkedFeatureIndices.contains(index)
                    ? Qt::Checked
                    : Qt::Unchecked);
            }
        }
    }

    m_updatingTree = false;
    applyActiveRegionExpansion();
    updateFilterLabels();
}

// 函数说明：setFeatureFilter负责当前模块的核心处理。
void FeatureListPanel::setHoleRecognitionOnly()
{
    if (m_holeCategories) return;
    m_holeCategories = new QTabBar(this);
    m_holeCategories->setObjectName(QStringLiteral("recognizedHoleCategories"));
    m_holeCategories->addTab(QString());
    m_holeCategories->addTab(QString());
    m_holeCategories->addTab(QString());
    m_holeCategories->setExpanding(true);
    m_holeCategories->setTabEnabled(1, false);
    m_holeCategories->setTabEnabled(2, false);
    for (auto *button : {m_filterAllButton, m_filterHoleButton, m_filterSlotButton,
                         m_filterPlaneButton, m_filterContourButton}) button->hide();
    static_cast<QVBoxLayout *>(layout())->insertWidget(1, m_holeCategories);
    setFeatureFilter(FeatureDisplayFilter::Hole);
    retranslateUi();
}

void FeatureListPanel::setFeatureFilter(FeatureDisplayFilter filter)
{
    if (m_holeCategories) filter = FeatureDisplayFilter::Hole;
    m_featureFilter = filter;
    if (m_filterAllButton) m_filterAllButton->setChecked(filter == FeatureDisplayFilter::All);
    if (m_filterHoleButton) m_filterHoleButton->setChecked(filter == FeatureDisplayFilter::Hole);
    if (m_filterSlotButton) m_filterSlotButton->setChecked(filter == FeatureDisplayFilter::SlotPocket);
    if (m_filterPlaneButton) m_filterPlaneButton->setChecked(filter == FeatureDisplayFilter::Plane);
    if (m_filterContourButton) m_filterContourButton->setChecked(filter == FeatureDisplayFilter::ContourOther);
    setFeatures(m_features);
}

// 函数说明：visibleFeatureIndices负责当前模块的核心处理。
QVector<int> FeatureListPanel::visibleFeatureIndices() const
{
    return filteredFeatureIndices(m_features, m_featureFilter);
}

// 函数说明：updateFilterLabels负责当前模块的核心处理。
void FeatureListPanel::updateFilterLabels()
{
    const bool zh = isChineseUi();
    auto countFor = [this](FeatureDisplayFilter filter) {
        int count = 0;
        for (const MachiningFeature &feature : m_features) {
            if (matchesFeatureDisplayFilter(feature.kind, filter)) {
                ++count;
            }
        }
        return count;
    };
    if (m_holeCategories) {
        m_titleLabel->setText(zh ? QStringLiteral("孔识别") : QStringLiteral("Hole recognition"));
        m_holeCategories->setTabText(0, (zh ? QStringLiteral("圆孔 %1") : QStringLiteral("Round %1")).arg(countFor(FeatureDisplayFilter::Hole)));
        m_holeCategories->setTabText(1, zh ? QStringLiteral("破孔") : QStringLiteral("Broken"));
        m_holeCategories->setTabText(2, zh ? QStringLiteral("侧壁") : QStringLiteral("Sidewall"));
        m_holeCategories->setTabToolTip(1, zh ? QStringLiteral("破孔识别尚未实现；不等同通孔。") : QStringLiteral("Broken-hole recognition is not implemented; through holes are different."));
        m_holeCategories->setTabToolTip(2, zh ? QStringLiteral("侧壁识别尚未实现；不等同侧向孔。") : QStringLiteral("Sidewall recognition is not implemented; side-facing holes are different."));
    }
    if (m_filterAllButton) {
        m_filterAllButton->setText(zh ? QStringLiteral("全部 %1").arg(m_features.size())
                                      : QStringLiteral("All %1").arg(m_features.size()));
    }
    if (m_filterHoleButton) {
        m_filterHoleButton->setText(zh ? QStringLiteral("孔 %1").arg(countFor(FeatureDisplayFilter::Hole))
                                       : QStringLiteral("Holes %1").arg(countFor(FeatureDisplayFilter::Hole)));
    }
    if (m_filterSlotButton) {
        m_filterSlotButton->setText(zh ? QStringLiteral("槽/腔 %1").arg(countFor(FeatureDisplayFilter::SlotPocket))
                                       : QStringLiteral("Slots %1").arg(countFor(FeatureDisplayFilter::SlotPocket)));
    }
    if (m_filterPlaneButton) {
        m_filterPlaneButton->setText(zh ? QStringLiteral("平面 %1").arg(countFor(FeatureDisplayFilter::Plane))
                                        : QStringLiteral("Planes %1").arg(countFor(FeatureDisplayFilter::Plane)));
    }
    if (m_filterContourButton) {
        m_filterContourButton->setText(zh ? QStringLiteral("轮廓 %1").arg(countFor(FeatureDisplayFilter::ContourOther))
                                          : QStringLiteral("Contours %1").arg(countFor(FeatureDisplayFilter::ContourOther)));
    }
}

// 函数说明：onActiveRegionChanged负责当前模块的核心处理。
void FeatureListPanel::onActiveRegionChanged(FaceRegion region)
{
    m_activeRegion = region;
    applyActiveRegionExpansion();
}

// 函数说明：applyActiveRegionExpansion负责当前模块的核心处理。
void FeatureListPanel::applyActiveRegionExpansion()
{
    if (!m_tree || m_activeRegion == FaceRegion::Unknown) {
        return;
    }

    QTreeWidgetItem *activeItem = nullptr;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *regionItem = m_tree->topLevelItem(i);
        const FaceRegion itemRegion =
            FaceRegion(regionItem->data(0, Qt::UserRole + 2).toInt());
        const bool active = itemRegion == m_activeRegion;
        regionItem->setExpanded(active);
        if (active) {
            activeItem = regionItem;
            for (int child = 0; child < regionItem->childCount(); ++child) {
                regionItem->child(child)->setExpanded(true);
            }
        }
    }

    if (activeItem) {
        m_tree->scrollToItem(activeItem, QAbstractItemView::PositionAtTop);
    }
}

// 函数说明：selectFeature负责当前模块的核心处理。
void FeatureListPanel::selectFeature(int index)
{
    if (index < 0) {
        m_tree->setCurrentItem(nullptr);
        return;
    }

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *featureItem = findFeatureItem(m_tree->topLevelItem(i), index);
        if (featureItem) {
            QTreeWidgetItem *parent = featureItem->parent();
            while (parent) {
                parent->setExpanded(true);
                parent = parent->parent();
            }
            m_tree->setCurrentItem(featureItem);
            m_tree->scrollToItem(featureItem);
            return;
        }
    }
}

// 函数说明：currentFeatureIndex负责当前模块的核心处理。
int FeatureListPanel::currentFeatureIndex() const
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item) {
        return -1;
    }
    return item->data(0, Qt::UserRole).toInt();
}

// 函数说明：checkedFeatureIndices负责当前模块的核心处理。
QVector<int> FeatureListPanel::checkedFeatureIndices() const
{
    QVector<int> indices = m_checkedFeatureIndices.values().toVector();
    std::sort(indices.begin(), indices.end());
    return indices;
}

// 函数说明：retranslateUi负责当前模块的核心处理。
void FeatureListPanel::retranslateUi()
{
    m_titleLabel->setText(isChineseUi()
        ? QStringLiteral("识别特征")
        : QStringLiteral("Recognized Features"));

    const QString filterHint = isChineseUi()
        ? QStringLiteral("只筛选显示的加工对象，不会自动创建正式工序。")
        : QStringLiteral("Filters visible machining targets only; it never creates confirmed operations.");
    const QList<QToolButton*> filterButtons = {
        m_filterAllButton, m_filterHoleButton, m_filterSlotButton,
        m_filterPlaneButton, m_filterContourButton
    };
    for (QToolButton *button : filterButtons) {
        if (button) button->setToolTip(filterHint);
    }
    updateFilterLabels();

    if (!m_features.isEmpty()) {
        setFeatures(m_features);
    }
}
