#pragma once

#include <QDialog>
#include "../operations/MachiningOperation.h"

class QTableWidget;
class QComboBox;
class QLabel;

QString operationEditParameterLabel(const QString &key);
QString operationEditError(const MachiningOperation &operation, const StrategyParams &params,
                           int toolId);

class OperationEditDialog : public QDialog
{
public:
    explicit OperationEditDialog(const MachiningOperation &operation, QWidget *parent = nullptr);
    QString operationId() const { return m_original.id; }
    StrategyParams params() const { return m_resultParams; }
    int toolId() const { return m_resultToolId; }
    void accept() override;

private:
    StrategyParams draftParams() const;
    QString validationError(const StrategyParams &params) const;
    const MachiningOperation m_original;
    StrategyParams m_resultParams;
    int m_resultToolId;
    QTableWidget *m_table = nullptr;
    QComboBox *m_tools = nullptr;
    QLabel *m_error = nullptr;
};
