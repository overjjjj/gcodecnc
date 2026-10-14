#pragma once

#include "ContourMachiningChoice.h"

#include <QDialog>
#include <functional>

class QComboBox;
class QLabel;
class QPushButton;

class ContourMachiningChoiceDialog : public QDialog
{
public:
    explicit ContourMachiningChoiceDialog(QWidget *parent = nullptr);

    void setChineseUi(bool chinese);
    void setContext(const QString &strategyId,
                    const ContourFeature &feature,
                    const StrategyParams &params,
                    ChainMachiningSide side = ChainMachiningSide::Mixed);
    ContourMachiningChoice choice() const;
    void setAllowedStartIndices(const QVector<int> &indices);
    void accept() override;
    void setChoiceChangedCallback(
        std::function<void(const ContourMachiningChoice &)> callback);

private:
    void updateText();
    void updateStartOptions();
    void updateSummary();

    QVector<int> m_allowedStartIndices;
    bool m_restrictStartIndices = false;
    bool m_chinese = true;
    bool m_closed = true;
    bool m_canReverse = false;
    QVector<QVector3D> m_featurePoints;
    ContourFeature m_feature;
    StrategyParams m_params;
    ChainMachiningSide m_machiningSide = ChainMachiningSide::Mixed;
    QPushButton *m_confirm = nullptr;
    std::function<void(const ContourMachiningChoice &)> m_choiceChangedCallback;
    QLabel *m_introLabel = nullptr;
    QLabel *m_stateValue = nullptr;
    QLabel *m_directionLabel = nullptr;
    QLabel *m_startLabel = nullptr;
    QLabel *m_compensationLabel = nullptr;
    QComboBox *m_directionCombo = nullptr;
    QComboBox *m_startCombo = nullptr;
    QComboBox *m_compensationCombo = nullptr;
    QLabel *m_summaryLabel = nullptr;
};
