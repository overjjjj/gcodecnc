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

    void retranslateUi();

public slots:
    void setActiveRegion(FaceRegion region);

signals:
    void generateProgramRequested(const QList<MachiningOperation> &operations);
    void operationsEdited(const QList<MachiningOperation> &operations);
    void applyCurrentToolRequested();
    void currentOperationChanged(int operationNumber);

private slots:
    void onMoveUp();
    void onMoveDown();
    void onDelete();
    void onSortByStage();
    void onGenerateProgram();
    void onSelectionChanged();

private:
    QLabel         *m_titleLabel;
    QLabel         *m_summaryLabel;
    QTableWidget  *m_table;
    QToolButton   *m_btnUp;
    QToolButton   *m_btnDown;
    QToolButton   *m_btnDelete;
    QToolButton   *m_btnApplyTool;
    QToolButton   *m_btnSortStage;
    QPushButton   *m_btnGenerateProgram;

    QList<MachiningOperation> m_operations;
    FaceRegion m_activeRegion = FaceRegion::Unknown;

    void refreshTable();
    void updateSummary();
    QString operationLabel(const MachiningOperation &op) const;
};
