#pragma once

#include <QDialog>

#include "../core/HoleSelectionSession.h"

class QCheckBox;
class QPushButton;
class QTableWidget;

class HoleSelectionDialog : public QDialog
{
public:
    explicit HoleSelectionDialog(QWidget *parent = nullptr);

    void setChineseUi(bool chinese);
    void setRecords(const QList<HoleSelectionRecord> &records);
    const QList<HoleSelectionRecord> &records() const { return m_session.records(); }

    bool sortDiameterDescending();
    bool undo();
    bool redo();
    bool confirm();

private:
    void refreshTable();
    void retranslateUi();
    void cancel();

    HoleSelectionSession m_session;
    bool m_chinese = true;
    QCheckBox *m_showSerialCheck = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_sortDiameterButton = nullptr;
    QPushButton *m_sortXButton = nullptr;
    QPushButton *m_sortYButton = nullptr;
    QPushButton *m_sortNearestButton = nullptr;
    QPushButton *m_undoButton = nullptr;
    QPushButton *m_redoButton = nullptr;
    QPushButton *m_confirmButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};
