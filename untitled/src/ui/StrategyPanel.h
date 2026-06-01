#pragma once

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QWidget>
#include "../import/StepImporter.h"
#include "../strategies/StrategyBase.h"
#include "../tool/ToolEntry.h"

class StrategyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit StrategyPanel(QWidget *parent = nullptr);

    void setFeature(const HoleFeature &feature);
    void clearFeature();
    void setContourFeature(const MachiningFeature &feature);
    void clearContourFeature();
    ToolEntry currentTool() const;
    void retranslateUi();

signals:
    void generateRequested(const HoleFeature &feature,
                           const QString &strategyId,
                           const StrategyParams &params,
                           const ToolEntry &tool);
    void circleMillRequested(const ContourFeature &feature,
                             const StrategyParams &params,
                             const ToolEntry &tool);
    void millingRequested(const ContourFeature &feature,
                          const QString &strategyId,
                          const StrategyParams &params,
                          const ToolEntry &tool);

private slots:
    void onStrategyChanged(int index);
    void onGenerate();
    void onEditHoleParams();
    void onCircleMillClicked();
    void onMillingClicked();

private:
    void populateStrategies();
    void populateTools();
    void selectPreferredStrategyForCurrentContext();
    void selectPreferredToolForFeature();
    void refreshParamTable();
    void cacheCurrentParams();
    StrategyParams currentParams() const;
    ContourFeature currentContourFeature() const;
    QStringList strategyIdsForCurrentContext() const;
    QString paramDisplayName(const QString &key) const;
    QString displayNameForStrategy(const QString &strategyId) const;
    QString toolDisplayName(const ToolEntry &tool) const;
    QString preferredToolType(const QString &strategyId) const;
    QString strategyHint(const QString &strategyId) const;
    void updateStrategyHint();
    bool isSlotFeatureSelected() const;
    void seedContourParamsFromFeature();

    HoleFeature m_feature;
    bool m_hasFeature = false;
    MachiningFeature m_contourFeature;
    bool m_hasContourFeature = false;
    QString m_currentStrategyId;
    QMap<QString, StrategyParams> m_userParams;
    QLabel *m_featureLabel = nullptr;
    QLabel *m_strategyLabel = nullptr;
    QLabel *m_toolLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QComboBox *m_strategyCombo = nullptr;
    QComboBox *m_toolCombo = nullptr;
    QTableWidget *m_paramTable = nullptr;
    QPushButton *m_editHoleParamsBtn = nullptr;
    QPushButton *m_generateBtn = nullptr;
    QGroupBox *m_millingGroup = nullptr;
    QPushButton *m_millingBtn = nullptr;
    QPushButton *m_circleMillBtn = nullptr;
};
