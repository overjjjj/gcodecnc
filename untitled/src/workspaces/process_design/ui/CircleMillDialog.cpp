#include "CircleMillDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>
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

static QSpinBox *makeIntSpin(int minValue, int maxValue, int defaultValue, const QString &suffix)
{
    auto *spin = new QSpinBox;
    spin->setRange(minValue, maxValue);
    spin->setSingleStep(100);
    spin->setValue(defaultValue);
    spin->setSuffix(suffix);
    return spin;
}

} // namespace

CircleMillDialog::CircleMillDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("铣边(封闭圆)"));
    setModal(true);

    m_centerX = makeDoubleSpin(-100000.0, 100000.0, 0.0);
    m_centerY = makeDoubleSpin(-100000.0, 100000.0, 0.0);
    m_centerZ = makeDoubleSpin(-100000.0, 100000.0, 0.0);
    m_radius = makeDoubleSpin(0.001, 100000.0, 10.0);
    m_depth = makeDoubleSpin(0.001, 100000.0, 2.0);
    m_stepDown = makeDoubleSpin(0.001, 100000.0, 1.0);
    m_spindleSpeed = makeIntSpin(1, 100000, 3000, QStringLiteral(" rpm"));
    m_feedRate = makeIntSpin(1, 100000, 1500, QStringLiteral(" mm/min"));
    m_plungeRate = makeIntSpin(1, 100000, 300, QStringLiteral(" mm/min"));

    m_compensation = new QComboBox(this);
    m_compensation->addItem(QStringLiteral("左补偿 G41"), 1.0);
    m_compensation->addItem(QStringLiteral("右补偿 G42"), -1.0);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("圆心 X"), m_centerX);
    form->addRow(QStringLiteral("圆心 Y"), m_centerY);
    form->addRow(QStringLiteral("圆心 Z"), m_centerZ);
    form->addRow(QStringLiteral("半径"), m_radius);
    form->addRow(QStringLiteral("总深度"), m_depth);
    form->addRow(QStringLiteral("每层切深"), m_stepDown);
    form->addRow(QStringLiteral("主轴转速"), m_spindleSpeed);
    form->addRow(QStringLiteral("进给速率"), m_feedRate);
    form->addRow(QStringLiteral("下刀速率"), m_plungeRate);
    form->addRow(QStringLiteral("补偿方向"), m_compensation);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

ContourFeature CircleMillDialog::feature() const
{
    ContourFeature f;
    f.subType = QStringLiteral("circle");
    f.center = QVector3D(float(m_centerX->value()),
                         float(m_centerY->value()),
                         float(m_centerZ->value()));
    f.radius = m_radius->value();
    f.depth = m_depth->value();
    f.axis = QVector3D(0, 0, 1);
    return f;
}

StrategyParams CircleMillDialog::params() const
{
    StrategyParams p;
    p.set(QStringLiteral("safeHeight"), 50.0);
    p.set(QStringLiteral("feedHeight"), 3.0);
    p.set(QStringLiteral("stepDown"), m_stepDown->value());
    p.set(QStringLiteral("spindleSpeed"), double(m_spindleSpeed->value()));
    p.set(QStringLiteral("feedRate"), double(m_feedRate->value()));
    p.set(QStringLiteral("plungeRate"), double(m_plungeRate->value()));
    p.set(QStringLiteral("compensation"), m_compensation->currentData().toDouble());
    p.set(QStringLiteral("stockToLeave"), 0.0);
    return p;
}
