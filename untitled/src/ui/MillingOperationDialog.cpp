#include "MillingOperationDialog.h"

#include "../strategies/StrategyFactory.h"
#include "../tool/ToolLibrary.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

static QDoubleSpinBox *makeDoubleSpin(double minValue,
                                      double maxValue,
                                      double defaultValue,
                                      int decimals = 3)
{
    auto *spin = new QDoubleSpinBox;
    spin->setRange(minValue, maxValue);
    spin->setDecimals(decimals);
    spin->setSingleStep(decimals >= 3 ? 0.1 : 1.0);
    spin->setValue(defaultValue);
    spin->setSuffix(QStringLiteral(" mm"));
    return spin;
}

static bool isMillingStrategy(const QString &id)
{
    return id == QStringLiteral("mill_face")
        || id == QStringLiteral("mill_pocket_rough")
        || id == QStringLiteral("mill_contour_finish")
        || id == QStringLiteral("mill_surface_finish")
        || id == QStringLiteral("mill_closed_contour")
        || id == QStringLiteral("mill_open_contour")
        || id == QStringLiteral("mill_slot")
        || id == QStringLiteral("mill_blind_slot")
        || id == QStringLiteral("mill_tapered_slot");
}

static QString featureSubTypeForStrategy(const QString &strategyId)
{
    if (strategyId == QStringLiteral("mill_face")) {
        return QStringLiteral("face_area");
    }
    if (strategyId == QStringLiteral("mill_pocket_rough")) {
        return QStringLiteral("pocket_area");
    }
    if (strategyId == QStringLiteral("mill_surface_finish")) {
        return QStringLiteral("surface_area");
    }
    if (strategyId == QStringLiteral("mill_open_contour")) {
        return QStringLiteral("open_contour");
    }
    if (strategyId == QStringLiteral("mill_closed_contour")) {
        return QStringLiteral("closed_contour");
    }
    if (strategyId == QStringLiteral("mill_slot")) {
        return QStringLiteral("open_slot");
    }
    if (strategyId == QStringLiteral("mill_blind_slot")) {
        return QStringLiteral("blind_slot");
    }
    if (strategyId == QStringLiteral("mill_tapered_slot")) {
        return QStringLiteral("tapered_slot");
    }
    return QStringLiteral("circle");
}

static double machiningTopZ(const MachiningFeature &feature)
{
    if (feature.kind == FeatureKind::Slot && feature.depth > 0.0) {
        return double(feature.center.z()) + feature.depth * 0.5;
    }
    return double(feature.center.z());
}

} // namespace

MillingOperationDialog::MillingOperationDialog(QWidget *parent)
    : QDialog(parent)
    , m_strategyCombo(new QComboBox(this))
    , m_toolCombo(new QComboBox(this))
    , m_centerX(makeDoubleSpin(-100000.0, 100000.0, 0.0))
    , m_centerY(makeDoubleSpin(-100000.0, 100000.0, 0.0))
    , m_centerZ(makeDoubleSpin(-100000.0, 100000.0, 0.0))
    , m_radius(makeDoubleSpin(0.001, 100000.0, 20.0))
    , m_depth(makeDoubleSpin(0.001, 100000.0, 2.0))
    , m_paramTable(new QTableWidget(this))
{
    setWindowTitle(QStringLiteral("添加铣削工序"));
    setModal(true);

    populateStrategies();
    populateTools();

    m_paramTable->setColumnCount(2);
    m_paramTable->horizontalHeader()->setStretchLastSection(true);
    m_paramTable->verticalHeader()->setVisible(false);
    m_paramTable->setHorizontalHeaderLabels({
        QStringLiteral("参数"),
        QStringLiteral("值")
    });

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("铣削策略"), m_strategyCombo);
    form->addRow(QStringLiteral("刀具"), m_toolCombo);
    form->addRow(QStringLiteral("中心 X"), m_centerX);
    form->addRow(QStringLiteral("中心 Y"), m_centerY);
    form->addRow(QStringLiteral("顶面 Z"), m_centerZ);
    form->addRow(QStringLiteral("半径/半宽"), m_radius);
    form->addRow(QStringLiteral("加工深度"), m_depth);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_paramTable, 1);
    layout->addWidget(buttons);

    connect(m_strategyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MillingOperationDialog::onStrategyChanged);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshParamTable();
}

QString MillingOperationDialog::strategyId() const
{
    return m_strategyCombo->currentData().toString();
}

ToolEntry MillingOperationDialog::tool() const
{
    return ToolLibrary::instance().tool(m_toolCombo->currentData().toInt());
}

ContourFeature MillingOperationDialog::feature() const
{
    ContourFeature f;
    f.subType = featureSubTypeForStrategy(strategyId());
    f.center = QVector3D(float(m_centerX->value()),
                         float(m_centerY->value()),
                         float(m_centerZ->value()));
    f.radius = m_radius->value();
    f.depth = m_depth->value();
    f.axis = QVector3D(0, 0, 1);
    for (int row = 0; row < m_paramTable->rowCount(); ++row) {
        const QTableWidgetItem *nameItem = m_paramTable->item(row, 0);
        const QTableWidgetItem *valueItem = m_paramTable->item(row, 1);
        if (!nameItem || !valueItem) {
            continue;
        }
        const QString key = nameItem->data(Qt::UserRole).toString();
        const double value = valueItem->text().toDouble();
        if (key == QStringLiteral("slotLength")) {
            f.length = value;
        } else if (key == QStringLiteral("slotWidth")) {
            f.width = value;
        } else if (key == QStringLiteral("angle")) {
            f.angle = value;
        }
    }
    return f;
}

StrategyParams MillingOperationDialog::params() const
{
    StrategyParams p;
    for (int r = 0; r < m_paramTable->rowCount(); ++r) {
        const QTableWidgetItem *nameItem = m_paramTable->item(r, 0);
        const QTableWidgetItem *valueItem = m_paramTable->item(r, 1);
        if (!nameItem || !valueItem) {
            continue;
        }
        p.set(nameItem->data(Qt::UserRole).toString(), valueItem->text().toDouble());
    }
    return p;
}

void MillingOperationDialog::onStrategyChanged(int)
{
    refreshParamTable();
}

void MillingOperationDialog::populateStrategies()
{
    m_strategyCombo->clear();
    const QStringList ids = {
        QStringLiteral("mill_face"),
        QStringLiteral("mill_pocket_rough"),
        QStringLiteral("mill_contour_finish"),
        QStringLiteral("mill_surface_finish"),
        QStringLiteral("mill_closed_contour"),
        QStringLiteral("mill_open_contour"),
        QStringLiteral("mill_slot"),
        QStringLiteral("mill_blind_slot"),
        QStringLiteral("mill_tapered_slot")
    };
    for (const QString &id : ids) {
        auto strategy = StrategyFactory::instance().strategy(id);
        if (strategy && isMillingStrategy(id)) {
            m_strategyCombo->addItem(strategy->displayName(), id);
        }
    }
}

void MillingOperationDialog::populateTools()
{
    m_toolCombo->clear();

    QVector<ToolEntry> tools = ToolLibrary::instance().toolsByType(QStringLiteral("end_mill"));
    if (tools.isEmpty()) {
        tools = ToolLibrary::instance().allTools();
    }

    for (const ToolEntry &tool : tools) {
        m_toolCombo->addItem(
            QStringLiteral("T%1 %2 直径%3")
                .arg(tool.id)
                .arg(tool.name)
                .arg(tool.diameter, 0, 'f', 1),
            tool.id);
    }
}

void MillingOperationDialog::refreshParamTable()
{
    m_paramTable->setRowCount(0);

    auto strategy = StrategyFactory::instance().strategy(strategyId());
    if (!strategy) {
        return;
    }

    const QVariantMap vals = strategy->defaultParams().values;
    int row = 0;
    for (auto it = vals.cbegin(); it != vals.cend(); ++it, ++row) {
        m_paramTable->insertRow(row);

        auto *nameItem = new QTableWidgetItem(paramDisplayName(it.key()));
        nameItem->setData(Qt::UserRole, it.key());
        nameItem->setFlags(Qt::ItemIsEnabled);

        m_paramTable->setItem(row, 0, nameItem);
        m_paramTable->setItem(row, 1, new QTableWidgetItem(it.value().toString()));
    }
}

void MillingOperationDialog::setFeature(const MachiningFeature &feature)
{
    // Pre-fill center and depth from recognized feature geometry.
    m_centerX->setValue(double(feature.center.x()));
    m_centerY->setValue(double(feature.center.y()));
    m_centerZ->setValue(machiningTopZ(feature));
    if (feature.depth > 0.0) {
        m_depth->setValue(feature.depth);
    }

    // For slots use the larger dimension as "radius/half-width" display.
    if (feature.kind == FeatureKind::Slot) {
        const double halfLen = feature.length > 0.0 ? feature.length / 2.0 : feature.radius;
        if (halfLen > 0.0) {
            m_radius->setValue(halfLen);
        }
        const QString preferred = (feature.subType == QStringLiteral("open_slot"))
                                      ? QStringLiteral("mill_slot")
                                      : QStringLiteral("mill_blind_slot");
        const int idx = m_strategyCombo->findData(preferred);
        if (idx >= 0) {
            m_strategyCombo->setCurrentIndex(idx);
        }
        // Overwrite slot dimension params with recognized feature geometry.
        for (int row = 0; row < m_paramTable->rowCount(); ++row) {
            const QTableWidgetItem *nameItem = m_paramTable->item(row, 0);
            QTableWidgetItem *valueItem = m_paramTable->item(row, 1);
            if (!nameItem || !valueItem) {
                continue;
            }
            const QString key = nameItem->data(Qt::UserRole).toString();
            if (key == QStringLiteral("slotLength") && feature.length > 0.0) {
                valueItem->setText(QString::number(feature.length, 'f', 3));
            } else if (key == QStringLiteral("slotWidth") && feature.width > 0.0) {
                valueItem->setText(QString::number(feature.width, 'f', 3));
            } else if (key == QStringLiteral("angle")) {
                valueItem->setText(QString::number(feature.angle, 'f', 2));
            }
        }
    } else if (feature.kind == FeatureKind::Pocket) {
        const int idx = m_strategyCombo->findData(QStringLiteral("mill_pocket_rough"));
        if (idx >= 0) {
            m_strategyCombo->setCurrentIndex(idx);
        }
        if (feature.radius > 0.0) {
            m_radius->setValue(feature.radius);
        }
    } else if (feature.kind == FeatureKind::FlatSurface) {
        const int idx = m_strategyCombo->findData(QStringLiteral("mill_face"));
        if (idx >= 0) {
            m_strategyCombo->setCurrentIndex(idx);
        }
    }
}

QString MillingOperationDialog::paramDisplayName(const QString &key) const
{
    if (key == QStringLiteral("safeHeight")) return QStringLiteral("安全高度");
    if (key == QStringLiteral("feedHeight")) return QStringLiteral("进给起始高度");
    if (key == QStringLiteral("stepDown")) return QStringLiteral("每层切深");
    if (key == QStringLiteral("stepover")) return QStringLiteral("行距/步距");
    if (key == QStringLiteral("spindleSpeed")) return QStringLiteral("主轴转速");
    if (key == QStringLiteral("feedRate")) return QStringLiteral("进给速度");
    if (key == QStringLiteral("plungeRate")) return QStringLiteral("下刀速度");
    if (key == QStringLiteral("stockToLeave")) return QStringLiteral("加工余量");
    if (key == QStringLiteral("compensation")) return QStringLiteral("刀补方向");
    if (key == QStringLiteral("leadLength"))   return QStringLiteral("引入长度");
    if (key == QStringLiteral("slotLength"))   return QStringLiteral("槽长");
    if (key == QStringLiteral("slotWidth"))    return QStringLiteral("槽宽");
    if (key == QStringLiteral("angle"))        return QStringLiteral("方向角(°)");
    return key;
}
