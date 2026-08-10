#include "StockDefinitionDialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

StockDefinitionDialog::StockDefinitionDialog(bool chinese, QWidget *parent)
    : QDialog(parent)
    , m_chinese(chinese)
{
    const auto ui = [chinese](const char *zh, const char *en) {
        return chinese ? QString::fromUtf8(zh) : QString::fromLatin1(en);
    };
    setWindowTitle(ui("零件与毛坯属性", "Part and Stock Properties"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(
        ui("当前阶段仅支持矩形毛坯。请根据实际下料尺寸人工确认六个方向的余量。",
           "The current stage supports rectangular stock only. Confirm all six allowances from the actual blank size."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *partGroup = new QGroupBox(ui("STEP 零件属性", "STEP Part Properties"), this);
    auto *partLayout = new QVBoxLayout(partGroup);
    m_partBoundsLabel = new QLabel(partGroup);
    m_partSizeLabel = new QLabel(partGroup);
    m_partBoundsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_partSizeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    partLayout->addWidget(m_partBoundsLabel);
    partLayout->addWidget(m_partSizeLabel);
    layout->addWidget(partGroup);

    auto *allowanceGroup = new QGroupBox(ui("矩形毛坯六向余量（毫米）", "Rectangular Stock Allowances (mm)"), this);
    auto *allowanceLayout = new QGridLayout(allowanceGroup);
    m_minusX = createAllowanceSpinBox();
    m_plusX = createAllowanceSpinBox();
    m_minusY = createAllowanceSpinBox();
    m_plusY = createAllowanceSpinBox();
    m_minusZ = createAllowanceSpinBox();
    m_plusZ = createAllowanceSpinBox();
    allowanceLayout->addWidget(new QLabel(ui("X− 左侧", "X− Left"), allowanceGroup), 0, 0);
    allowanceLayout->addWidget(m_minusX, 0, 1);
    allowanceLayout->addWidget(new QLabel(ui("X+ 右侧", "X+ Right"), allowanceGroup), 0, 2);
    allowanceLayout->addWidget(m_plusX, 0, 3);
    allowanceLayout->addWidget(new QLabel(ui("Y− 下侧", "Y− Lower"), allowanceGroup), 1, 0);
    allowanceLayout->addWidget(m_minusY, 1, 1);
    allowanceLayout->addWidget(new QLabel(ui("Y+ 上侧", "Y+ Upper"), allowanceGroup), 1, 2);
    allowanceLayout->addWidget(m_plusY, 1, 3);
    allowanceLayout->addWidget(new QLabel(ui("Z− 底部", "Z− Bottom"), allowanceGroup), 2, 0);
    allowanceLayout->addWidget(m_minusZ, 2, 1);
    allowanceLayout->addWidget(new QLabel(ui("Z+ 顶部", "Z+ Top"), allowanceGroup), 2, 2);
    allowanceLayout->addWidget(m_plusZ, 2, 3);
    layout->addWidget(allowanceGroup);

    auto *resultGroup = new QGroupBox(ui("最终毛坯", "Resolved Stock"), this);
    auto *resultLayout = new QVBoxLayout(resultGroup);
    m_stockBoundsLabel = new QLabel(resultGroup);
    m_stockSizeLabel = new QLabel(resultGroup);
    m_stockBoundsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_stockSizeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    resultLayout->addWidget(m_stockBoundsLabel);
    resultLayout->addWidget(m_stockSizeLabel);
    layout->addWidget(resultGroup);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(ui("确认毛坯", "Confirm Stock"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    for (QDoubleSpinBox *spin : {m_minusX, m_plusX, m_minusY,
                                 m_plusY, m_minusZ, m_plusZ}) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this]() { updateSummary(); });
    }
    updateSummary();
}

QDoubleSpinBox *StockDefinitionDialog::createAllowanceSpinBox()
{
    auto *spin = new QDoubleSpinBox(this);
    spin->setRange(0.0, 100000.0);
    spin->setDecimals(3);
    spin->setSingleStep(0.5);
    spin->setSuffix(QStringLiteral(" mm"));
    spin->setMinimumWidth(130);
    return spin;
}

void StockDefinitionDialog::setPartBounds(const QVector3D &minimum,
                                          const QVector3D &maximum)
{
    m_partMin = minimum;
    m_partMax = maximum;
    updateSummary();
}

void StockDefinitionDialog::setStockDefinition(const StockDefinition &stock)
{
    m_minusX->setValue(stock.minusX);
    m_plusX->setValue(stock.plusX);
    m_minusY->setValue(stock.minusY);
    m_plusY->setValue(stock.plusY);
    m_minusZ->setValue(stock.minusZ);
    m_plusZ->setValue(stock.plusZ);
    updateSummary();
}

StockDefinition StockDefinitionDialog::stockDefinition() const
{
    StockDefinition stock;
    stock.minusX = m_minusX->value();
    stock.plusX = m_plusX->value();
    stock.minusY = m_minusY->value();
    stock.plusY = m_plusY->value();
    stock.minusZ = m_minusZ->value();
    stock.plusZ = m_plusZ->value();
    stock.confirmed = true;
    return stock;
}

void StockDefinitionDialog::updateSummary()
{
    const QVector3D partSize = m_partMax - m_partMin;
    const StockBounds stock = stockDefinition().resolvedBounds(m_partMin, m_partMax);
    const QVector3D stockSize = stock.size();
    const QString partBoundsPattern = m_chinese
        ? QStringLiteral("范围：X [%1, %2]   Y [%3, %4]   Z [%5, %6] mm")
        : QStringLiteral("Bounds: X [%1, %2]   Y [%3, %4]   Z [%5, %6] mm");
    const QString sizePattern = m_chinese
        ? QStringLiteral("尺寸：长 %1 × 宽 %2 × 高 %3 mm")
        : QStringLiteral("Size: L %1 × W %2 × H %3 mm");
    m_partBoundsLabel->setText(partBoundsPattern
        .arg(double(m_partMin.x()), 0, 'f', 3).arg(double(m_partMax.x()), 0, 'f', 3)
        .arg(double(m_partMin.y()), 0, 'f', 3).arg(double(m_partMax.y()), 0, 'f', 3)
        .arg(double(m_partMin.z()), 0, 'f', 3).arg(double(m_partMax.z()), 0, 'f', 3));
    m_partSizeLabel->setText(sizePattern
        .arg(double(partSize.x()), 0, 'f', 3)
        .arg(double(partSize.y()), 0, 'f', 3)
        .arg(double(partSize.z()), 0, 'f', 3));
    m_stockBoundsLabel->setText(partBoundsPattern
        .arg(double(stock.minimum.x()), 0, 'f', 3).arg(double(stock.maximum.x()), 0, 'f', 3)
        .arg(double(stock.minimum.y()), 0, 'f', 3).arg(double(stock.maximum.y()), 0, 'f', 3)
        .arg(double(stock.minimum.z()), 0, 'f', 3).arg(double(stock.maximum.z()), 0, 'f', 3));
    m_stockSizeLabel->setText(sizePattern
        .arg(double(stockSize.x()), 0, 'f', 3)
        .arg(double(stockSize.y()), 0, 'f', 3)
        .arg(double(stockSize.z()), 0, 'f', 3));
}
