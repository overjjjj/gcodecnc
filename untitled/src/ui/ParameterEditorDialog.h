#pragma once

#include <QDialog>
#include <QMap>
#include <QSet>

#include "../strategies/StrategyBase.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

class ParameterEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ParameterEditorDialog(QWidget *parent = nullptr);

    void setChineseUi(bool enabled);
    void setLabelMap(const QMap<QString, QString> &labels);
    void setBasicKeys(const QSet<QString> &keys);
    void setNonNegativeKeys(const QSet<QString> &keys);
    void setAllowAnyValueKeys(const QSet<QString> &keys);
    void setDefaultParams(const StrategyParams &params);
    void setParams(const StrategyParams &params);

    StrategyParams params() const;

private slots:
    void onAccept();
    void onRestoreDefaults();
    void onShowAdvancedToggled(bool checked);

private:
    void rebuildTable(const StrategyParams &params);
    void syncRowVisibility();
    QString labelForKey(const QString &key) const;
    QString validationError() const;
    QStringList orderedKeys(const StrategyParams &params) const;

    bool m_chineseUi = false;
    StrategyParams m_params;
    StrategyParams m_defaultParams;
    QMap<QString, QString> m_labels;
    QSet<QString> m_basicKeys;
    QSet<QString> m_nonNegativeKeys;
    QSet<QString> m_allowAnyValueKeys;
    QLabel *m_hintLabel = nullptr;
    QCheckBox *m_showAdvancedCheck = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_restoreDefaultsButton = nullptr;
};
