#include "SetupOriginDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class OriginCoordinateSpinBox final : public QDoubleSpinBox
{
public:
    explicit OriginCoordinateSpinBox(QWidget *parent) : QDoubleSpinBox(parent)
    {
        connect(lineEdit(), &QLineEdit::textEdited, this, [this] { m_edited = true; });
        connect(this, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this] { m_edited = true; });
    }

    void setCoordinate(double value)
    {
        setRange(std::min(-1000000.0, value), std::max(1000000.0, value));
        setValue(value);
        m_original = value;
        m_edited = false;
    }

    double coordinate() const { return m_edited ? value() : m_original; }

protected:
    void stepBy(int steps) override
    {
        m_edited = true;
        QDoubleSpinBox::stepBy(steps);
    }

private:
    double m_original = 0.0;
    bool m_edited = false;
};
} // namespace

// 函数说明：SetupOriginDialog负责当前模块的核心处理。
SetupOriginDialog::SetupOriginDialog(bool chinese, QWidget *parent)
    // 函数说明：QDialog负责当前模块的核心处理。
    : QDialog(parent)
    // 函数说明：m_chinese负责当前模块的核心处理。
    , m_chinese(chinese)
{
    const auto ui = [chinese](const char *zh, const char *en) {
        return chinese ? QString::fromUtf8(zh) : QString::fromLatin1(en);
    };
    setWindowTitle(ui("设置 Setup 原点", "Setup Origin"));
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(
        ui("请人工确认工件坐标原点。九点定位以模型顶面为基准；自定义点可填写测量点或孔中心。",
           "Confirm the work coordinate origin manually. The nine anchors use the model top plane; custom point can be a measured point or hole center."),
        this);
    hint->setWordWrap(true);
    hint->setProperty("role", QStringLiteral("supportingText"));
    layout->addWidget(hint);

    auto *definitionGroup = new QGroupBox(ui("坐标定义", "Coordinate definition"), this);
    auto *definitionForm = new QFormLayout(definitionGroup);
    m_workOffsetCombo = new QComboBox(definitionGroup);
    for (int code = 54; code <= 59; ++code) {
        const QString value = QStringLiteral("G%1").arg(code);
        m_workOffsetCombo->addItem(value, value);
    }
    definitionForm->addRow(ui("工件坐标系", "Work offset"), m_workOffsetCombo);

    m_anchorCombo = new QComboBox(definitionGroup);
    const QList<QPair<QString, SetupOriginAnchor>> anchors = {
        {ui("顶面 · 左下", "Top · lower left"), SetupOriginAnchor::TopMinXMinY},
        {ui("顶面 · 下中", "Top · lower center"), SetupOriginAnchor::TopCenterXMinY},
        {ui("顶面 · 右下", "Top · lower right"), SetupOriginAnchor::TopMaxXMinY},
        {ui("顶面 · 左中", "Top · middle left"), SetupOriginAnchor::TopMinXCenterY},
        {ui("顶面 · 中心", "Top · center"), SetupOriginAnchor::TopCenter},
        {ui("顶面 · 右中", "Top · middle right"), SetupOriginAnchor::TopMaxXCenterY},
        {ui("顶面 · 左上", "Top · upper left"), SetupOriginAnchor::TopMinXMaxY},
        {ui("顶面 · 上中", "Top · upper center"), SetupOriginAnchor::TopCenterXMaxY},
        {ui("顶面 · 右上", "Top · upper right"), SetupOriginAnchor::TopMaxXMaxY},
        {ui("自定义点 / 孔中心", "Custom point / hole center"), SetupOriginAnchor::CustomPoint}
    };
    for (const auto &item : anchors) {
        m_anchorCombo->addItem(item.first, setupOriginAnchorId(item.second));
    }
    definitionForm->addRow(ui("定位点", "Anchor"), m_anchorCombo);
    layout->addWidget(definitionGroup);

    auto *customGroup = new QGroupBox(ui("自定义基准点（毫米）", "Custom base point (mm)"), this);
    auto *customForm = new QFormLayout(customGroup);
    m_customX = createCoordinateSpinBox();
    m_customY = createCoordinateSpinBox();
    m_customZ = createCoordinateSpinBox();
    customForm->addRow(tr("X"), m_customX);
    customForm->addRow(tr("Y"), m_customY);
    customForm->addRow(tr("Z"), m_customZ);
    layout->addWidget(customGroup);

    auto *offsetGroup = new QGroupBox(ui("原点偏移（毫米）", "Origin offset (mm)"), this);
    auto *offsetForm = new QFormLayout(offsetGroup);
    m_offsetX = createCoordinateSpinBox();
    m_offsetY = createCoordinateSpinBox();
    m_offsetZ = createCoordinateSpinBox();
    offsetForm->addRow(ui("X 偏移", "X offset"), m_offsetX);
    offsetForm->addRow(ui("Y 偏移", "Y offset"), m_offsetY);
    offsetForm->addRow(ui("Z 偏移", "Z offset"), m_offsetZ);
    layout->addWidget(offsetGroup);

    m_resolvedLabel = new QLabel(this);
    m_resolvedLabel->setObjectName(QStringLiteral("setupResolvedOrigin"));
    m_resolvedLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_resolvedLabel);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(ui("确认 Setup", "Confirm Setup"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SetupOriginDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    const auto update = [this]() { updateResolvedPoint(); };
    connect(m_anchorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update);
    for (QDoubleSpinBox *spin : {m_customX, m_customY, m_customZ,
                                 m_offsetX, m_offsetY, m_offsetZ}) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, update);
    }
    updateResolvedPoint();
}

// 函数说明：createCoordinateSpinBox负责当前模块的核心处理。
QDoubleSpinBox *SetupOriginDialog::createCoordinateSpinBox()
{
    auto *spin = new OriginCoordinateSpinBox(this);
    spin->setRange(-1000000.0, 1000000.0);
    spin->setDecimals(6);
    spin->setSingleStep(0.1);
    spin->setSuffix(QStringLiteral(" mm"));
    return spin;
}

// 函数说明：setModelBounds负责当前模块的核心处理。
void SetupOriginDialog::setModelBounds(const Coordinate3D &minimum, const Coordinate3D &maximum)
{
    m_bbMin = minimum;
    m_bbMax = maximum;
    updateResolvedPoint();
}

// 函数说明：setOrigin负责当前模块的核心处理。
void SetupOriginDialog::setOrigin(const SetupOrigin &origin)
{
    const int index = m_anchorCombo->findData(setupOriginAnchorId(origin.anchor));
    m_anchorCombo->setCurrentIndex(index >= 0 ? index : 4);
    const auto setCoordinate = [](QDoubleSpinBox *spin, double value) {
        static_cast<OriginCoordinateSpinBox *>(spin)->setCoordinate(value);
    };
    setCoordinate(m_customX, origin.customPoint.x());
    setCoordinate(m_customY, origin.customPoint.y());
    setCoordinate(m_customZ, origin.customPoint.z());
    setCoordinate(m_offsetX, origin.offset.x());
    setCoordinate(m_offsetY, origin.offset.y());
    setCoordinate(m_offsetZ, origin.offset.z());
    updateResolvedPoint();
}

// 函数说明：origin负责当前模块的核心处理。
SetupOrigin SetupOriginDialog::origin() const
{
    SetupOrigin value;
    value.anchor = setupOriginAnchorFromId(m_anchorCombo->currentData().toString());
    // Display rounding must not change a CAD-derived origin when its field is untouched.
    const auto coordinate = [](QDoubleSpinBox *spin) {
        return static_cast<OriginCoordinateSpinBox *>(spin)->coordinate();
    };
    value.customPoint = Coordinate3D(
        coordinate(m_customX), coordinate(m_customY), coordinate(m_customZ));
    value.offset = Coordinate3D(
        coordinate(m_offsetX), coordinate(m_offsetY), coordinate(m_offsetZ));
    value.confirmed = true;
    return value;
}

// 函数说明：setWorkOffset负责当前模块的核心处理。
void SetupOriginDialog::setWorkOffset(const QString &workOffset)
{
    const int index = m_workOffsetCombo->findData(workOffset.trimmed().toUpper());
    m_workOffsetCombo->setCurrentIndex(index >= 0 ? index : 0);
}

// 函数说明：workOffset负责当前模块的核心处理。
QString SetupOriginDialog::workOffset() const
{
    return m_workOffsetCombo->currentData().toString();
}

// 函数说明：updateResolvedPoint负责当前模块的核心处理。
void SetupOriginDialog::updateResolvedPoint()
{
    const bool custom = setupOriginAnchorFromId(m_anchorCombo->currentData().toString())
                        == SetupOriginAnchor::CustomPoint;
    m_customX->setEnabled(custom);
    m_customY->setEnabled(custom);
    m_customZ->setEnabled(custom);
    const Coordinate3D point = origin().resolvedPoint(m_bbMin, m_bbMax);
    m_resolvedLabel->setText((m_chinese
        ? QStringLiteral("最终原点：X %1   Y %2   Z %3 mm")
        : QStringLiteral("Resolved origin: X %1   Y %2   Z %3 mm"))
        .arg(double(point.x()), 0, 'f', 3)
        .arg(double(point.y()), 0, 'f', 3)
        .arg(double(point.z()), 0, 'f', 3));
}

// 函数说明：accept负责当前模块的核心处理。
void SetupOriginDialog::accept()
{
    QDialog::accept();
}
