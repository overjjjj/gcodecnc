#pragma once

#include <QDialog>

#include "../import/StepImporter.h"
#include "../strategies/StrategyBase.h"
#include "../tool/ToolEntry.h"

class QComboBox;
class QDoubleSpinBox;
class QTableWidget;

class MillingOperationDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MillingOperationDialog(QWidget *parent = nullptr);

    QString strategyId() const;
    ToolEntry tool() const;
    ContourFeature feature() const;
    StrategyParams params() const;

    void setFeature(const MachiningFeature &feature);

private slots:
    void onStrategyChanged(int index);

private:
    void populateStrategies();
    void populateTools();
    void refreshParamTable();
    QString paramDisplayName(const QString &key) const;

    QComboBox *m_strategyCombo = nullptr;
    QComboBox *m_toolCombo = nullptr;
    QDoubleSpinBox *m_centerX = nullptr;
    QDoubleSpinBox *m_centerY = nullptr;
    QDoubleSpinBox *m_centerZ = nullptr;
    QDoubleSpinBox *m_radius = nullptr;
    QDoubleSpinBox *m_depth = nullptr;
    QTableWidget *m_paramTable = nullptr;
};
