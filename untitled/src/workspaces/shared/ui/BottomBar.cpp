#include "BottomBar.h"

#include <QComboBox>
#include <QHBoxLayout>

BottomBar::BottomBar(QWidget *parent)
    : QWidget(parent)
    , m_label(new QLabel(QStringLiteral("就绪"), this))
    , m_progress(new QProgressBar(this))
    , m_speedLabel(new QLabel(QStringLiteral("仿真速度"), this))
    , m_speedCombo(new QComboBox(this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    m_progress->setRange(0, 1000);
    m_progress->setFixedWidth(160);
    m_progress->setVisible(false);
    m_speedCombo->addItem(QStringLiteral("0.25x"), 0.25);
    m_speedCombo->addItem(QStringLiteral("0.5x"), 0.5);
    m_speedCombo->addItem(QStringLiteral("1x"), 1.0);
    m_speedCombo->addItem(QStringLiteral("2x"), 2.0);
    m_speedCombo->addItem(QStringLiteral("5x"), 5.0);
    m_speedCombo->addItem(QStringLiteral("10x"), 10.0);
    m_speedCombo->setCurrentIndex(2);
    m_speedCombo->setFixedWidth(72);
    layout->addWidget(m_label, 1);
    layout->addWidget(m_progress);
    layout->addWidget(m_speedLabel);
    layout->addWidget(m_speedCombo);
    setFixedHeight(28);

    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        emit simulationSpeedChanged(m_speedCombo->itemData(index).toDouble());
    });
}

void BottomBar::setStatus(const QString &msg)
{
    m_label->setText(msg);
}

void BottomBar::setProgress(double fraction)
{
    m_progress->setValue(static_cast<int>(fraction * 1000));
}

void BottomBar::showProgress(bool visible)
{
    m_progress->setVisible(visible);
}
