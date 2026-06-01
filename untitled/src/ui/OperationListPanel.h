#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QToolButton>

#include "../strategies/MachiningOperation.h"
#include "../import/StepImporter.h"

class OperationListPanel : public QWidget
{
    Q_OBJECT
public:
    explicit OperationListPanel(QWidget *parent = nullptr);

    void addHoleOperation(const HoleFeature &feature,
                          const QString &strategyId,
                          const StrategyParams &params,
                          int toolId);

    void addContourOperation(const ContourFeature &feature,
                             const QString &strategyId,
                             const StrategyParams &params,
                             int toolId);

    void setOperations(const QList<MachiningOperation> &operations);
    const QList<MachiningOperation> &operations() const { return m_operations; }
    int currentOperationNumber() const;

    bool applyToolToSelection(int toolId);

    void retranslateUi();

signals:
    void generateAllRequested(const QList<MachiningOperation> &operations);
    void applyCurrentToolRequested();
    void currentOperationChanged(int operationNumber);

private slots:
    void onMoveUp();
    void onMoveDown();
    void onDelete();
    void onSortByStage();
    void onGenerateAll();
    void onSelectionChanged();

private:
    QTableWidget  *m_table;
    QToolButton   *m_btnUp;
    QToolButton   *m_btnDown;
    QToolButton   *m_btnDelete;
    QToolButton   *m_btnApplyTool;
    QToolButton   *m_btnSortStage;
    QPushButton   *m_btnGenerate;

    QList<MachiningOperation> m_operations;

    void refreshTable();
    QString operationLabel(const MachiningOperation &op) const;
};
