#pragma once
#include <QDialog>
#include "MaterialRecommendationStore.h"
#include "../../../core/StrategyParams.h"
#include "../../../tool/ToolEntry.h"

class QListWidget;
class QLabel;
class QTableWidget;
class QPushButton;

class MaterialLibraryDialog : public QDialog {
public:
    explicit MaterialLibraryDialog(QWidget *parent = nullptr, const QString &strategyId = {},
                                   const ToolEntry &tool = {}, const StrategyParams &params = {},
                                   const QString &storePath = {});
    QVariantMap appliedParameters() const { return m_applied; }
private:
    void reload(const QString &selectedId = {});
    void previewSelection();
    void editSelection(bool creating);
    void applySelection();
    MaterialRecommendationStore m_store;
    QString m_strategyId;
    QString m_toolFingerprint;
    int m_toolId = 0;
    QVariantMap m_original;
    QVariantMap m_applied;
    QListWidget *m_list = nullptr;
    QTableWidget *m_preview = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_new = nullptr;
    QPushButton *m_edit = nullptr;
    QPushButton *m_remove = nullptr;
    QPushButton *m_apply = nullptr;
};
