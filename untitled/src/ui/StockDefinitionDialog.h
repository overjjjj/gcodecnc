#pragma once

#include <QDialog>

#include "../core/StockDefinition.h"

class QDoubleSpinBox;
class QLabel;

class StockDefinitionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit StockDefinitionDialog(bool chinese, QWidget *parent = nullptr);

    void setPartBounds(const QVector3D &minimum, const QVector3D &maximum);
    void setStockDefinition(const StockDefinition &stock);
    StockDefinition stockDefinition() const;

private:
    QDoubleSpinBox *createAllowanceSpinBox();
    void updateSummary();

    QDoubleSpinBox *m_minusX = nullptr;
    QDoubleSpinBox *m_plusX = nullptr;
    QDoubleSpinBox *m_minusY = nullptr;
    QDoubleSpinBox *m_plusY = nullptr;
    QDoubleSpinBox *m_minusZ = nullptr;
    QDoubleSpinBox *m_plusZ = nullptr;
    QLabel *m_partBoundsLabel = nullptr;
    QLabel *m_partSizeLabel = nullptr;
    QLabel *m_stockBoundsLabel = nullptr;
    QLabel *m_stockSizeLabel = nullptr;
    QVector3D m_partMin;
    QVector3D m_partMax;
    bool m_chinese = true;
};
