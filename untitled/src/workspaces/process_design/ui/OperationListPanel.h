#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QHash>
#include <QPushButton>
#include <QToolButton>

#include "../operations/MachiningOperation.h"
#include "../../../import/StepImporter.h"

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

    bool operationVisible(const QString &id) const { return m_visibility.value(id, true); }
    void retranslateUi();

public slots:
    void setActiveRegion(FaceRegion region);

signals:
    void generateProgramRequested(const QList<MachiningOperation> &operations);
    void recalculateRequested(const QList<MachiningOperation> &operations);
    void operationsEdited(const QList<MachiningOperation> &operations);
    void applyCurrentToolRequested();
    void currentOperationChanged(int operationNumber);
    void editOperationRequested(const QString &operationId);
    void batchEditOperationsRequested(const QStringList &operationIds);
    void operationVisibilityChanged(const QString &operationId, bool visible);

private slots:
    void onMoveUp();
    void onMoveDown();
    void onDelete();
    void onCopy();
    void onSortByStage();
    void onRecalculate();
    void onGenerateProgram();
    void onSelectionChanged();
    void onItemChanged(QTreeWidgetItem *item, int column);

private:
    QLabel         *m_titleLabel;
    QLabel         *m_summaryLabel;
    QTreeWidget   *m_table;
    QToolButton   *m_btnUp;
    QToolButton   *m_btnDown;
    QToolButton   *m_btnDelete;
    QToolButton   *m_btnApplyTool;
    QToolButton   *m_btnSortStage;
    QToolButton   *m_btnRecalculate;
    QToolButton   *m_btnCopy;
    QToolButton   *m_btnBatchEdit;
    QPushButton   *m_btnGenerateProgram;

    QList<MachiningOperation> m_operations;
    FaceRegion m_activeRegion = FaceRegion::Unknown;

    QHash<QString, bool> m_visibility;
    int operationRow(QTreeWidgetItem *item) const;
    QList<int> selectedOperationRows() const;
    void refreshTable();
    void updateSummary();
    QString operationLabel(const MachiningOperation &op) const;
};
