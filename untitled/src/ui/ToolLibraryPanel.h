#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include "../tool/ToolEntry.h"
#include "../tool/ToolLibrary.h"

class ToolLibraryPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ToolLibraryPanel(QWidget *parent = nullptr);

    void refresh();
    void retranslateUi();

signals:
    void toolSelected(const ToolEntry &tool);

private slots:
    void onSelectionChanged();
    void onAdd();
    void onEdit();
    void onRemove();

private:
    QLabel      *m_titleLabel;
    QListWidget *m_list;
    QPushButton *m_btnAdd    = nullptr;
    QPushButton *m_btnEdit   = nullptr;
    QPushButton *m_btnRemove = nullptr;
};
