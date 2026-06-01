#pragma once

#include <QDialog>
#include "../import/StepImporter.h"
#include "../strategies/StrategyBase.h"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

class CircleMillDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CircleMillDialog(QWidget *parent = nullptr);

    ContourFeature feature() const;
    StrategyParams params() const;

private:
    QDoubleSpinBox *m_centerX = nullptr;
    QDoubleSpinBox *m_centerY = nullptr;
    QDoubleSpinBox *m_centerZ = nullptr;
    QDoubleSpinBox *m_radius = nullptr;
    QDoubleSpinBox *m_depth = nullptr;
    QDoubleSpinBox *m_stepDown = nullptr;
    QSpinBox *m_spindleSpeed = nullptr;
    QSpinBox *m_feedRate = nullptr;
    QSpinBox *m_plungeRate = nullptr;
    QComboBox *m_compensation = nullptr;
};
