#pragma once

#include "ContourMachiningChoice.h"

#include <QDialog>
#include <functional>

class QComboBox;
class QLabel;

class ContourMachiningChoiceDialog : public QDialog
{
public:
    explicit ContourMachiningChoiceDialog(QWidget *parent = nullptr);

    void setChineseUi(bool chinese);
    void setContext(const QString &strategyId,
                    const ContourFeature &feature,
                    const StrategyParams &params);
    ContourMachiningChoice choice() const;
    void setChoiceChangedCallback(
        std::function<void(const ContourMachiningChoice &)> callback);

private:
    void updateText();
    void updateStartOptions();
    void updateSummary();

    bool m_chinese = true;
    bool m_closed = true;
    bool m_canReverse = false;
    QVector<QVector3D> m_featurePoints;
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
