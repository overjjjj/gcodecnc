#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QToolButton>

#include "../strategies/MachiningOperation.h"
#include "../import/StepImporter.h"

class QLabel;

class OperationListPanel : public QWidget
{
    Q_OBJECT
public:
    explicit OperationListPanel(QWidget *parent = nullptr);

    QStringList addConfirmedOperations(const QList<MachiningOperation> &operations);

    void setOperations(const QList<MachiningOperation> &operations);
    const QList<MachiningOperation> &operations() const { return m_operations; }
    int currentOperationNumber() const;
    QString currentOperationId() const;
    bool selectOperationById(const QString &operationId);

    bool applyToolToSelection(int toolId);
    bool setToolpathResult(const QString &operationId, bool success,
                           const QString &message = QString());

    void retranslateUi();

public slots:
    void setActiveRegion(FaceRegion region);

signals:
    void generateProgramRequested(const QList<MachiningOperation> &operations);
    void recalculateRequested(const QList<MachiningOperation> &operations);
    void operationsEdited(const QList<MachiningOperation> &operations);
    void applyCurrentToolRequested();
    void currentOperationChanged(int operationNumber);

private slots:
    void onMoveUp();
    void onMoveDown();
    void onDelete();
    void onSortByStage();
    void onRecalculate();
    void onGenerateProgram();
    void onSelectionChanged();
    void onItemChanged(QTableWidgetItem *item);

private:
    QLabel         *m_titleLabel;
    QLabel         *m_summaryLabel;
    QTableWidget  *m_table;
    QToolButton   *m_btnUp;
    QToolButton   *m_btnDown;
    QToolButton   *m_btnDelete;
    QToolButton   *m_btnApplyTool;
    QToolButton   *m_btnSortStage;
    QToolButton   *m_btnRecalculate;
    QPushButton   *m_btnGenerateProgram;

    QList<MachiningOperation> m_operations;
    FaceRegion m_activeRegion = FaceRegion::Unknown;

    void refreshTable();
    void updateSummary();
    QString operationLabel(const MachiningOperation &op) const;
};
