#pragma once
#include <QDialog>
#include <QMap>
#include "../operations/MachiningOperation.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

class OperationBatchEditDialog : public QDialog
{
public:
    explicit OperationBatchEditDialog(const QList<MachiningOperation> &operations,
                                      QWidget *parent = nullptr);
    const QList<MachiningOperation> &operations() const { return m_result; }
    void accept() override;

private:
    const QList<MachiningOperation> m_original;
    QList<MachiningOperation> m_result;
    QMap<QString, QCheckBox *> m_selectedFields;
    QMap<QString, QLineEdit *> m_values;
    QMap<QString, QComboBox *> m_options;
    QCheckBox *m_changeTool;
    QComboBox *m_tools;
    QLabel *m_error;
};
