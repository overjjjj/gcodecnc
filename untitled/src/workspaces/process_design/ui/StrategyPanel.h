#pragma once

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QWidget>
#include "../../../import/StepImporter.h"
#include "../operations/OperationProposal.h"
#include "../../../strategies/StrategyBase.h"
#include "../../../tool/ToolEntry.h"

class StrategyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit StrategyPanel(QWidget *parent = nullptr);

    void setFeature(const HoleFeature &feature);
    void clearFeature();
    void setContourFeature(const MachiningFeature &feature);
    void clearContourFeature();
    void setWorkOffset(const QString &workOffset);
    ToolEntry currentTool() const;
    void retranslateUi();
    bool selectStrategy(const QString &strategyId);
    StrategyParams parameterDraft() const { return currentParams(); }
    void restoreDraft(const StrategyParams &params, int toolId);
    void showTemplateManager();
    void showMaterialLibrary();

public slots:
    void setActiveRegion(FaceRegion region);

signals:
    void reselectGeometryRequested();
    void operationProposalConfirmed(const OperationProposal &proposal);

private slots:
    void onStrategyChanged(int index);
    void onConfirmOperation();
    void onEditHoleParams();
    void onCircleMillClicked();

private:
    void populateStrategies();
    void populateTools();
    void selectPreferredStrategyForCurrentContext();
    void selectPreferredToolForFeature();
    void refreshParamTable();
    void cacheCurrentParams();
    StrategyParams currentParams() const;
    StrategyParams paramsForStrategy(const QString &strategyId, bool preferUserParams) const;
    void setCurrentParams(const StrategyParams &params);
    void updateParamSummary();
    QString paramSummaryText(const StrategyParams &params) const;
    ContourFeature currentContourFeature() const;
    QStringList strategyIdsForCurrentContext() const;
    QString paramDisplayName(const QString &key) const;
    QString displayNameForStrategy(const QString &strategyId) const;
    QString toolDisplayName(const ToolEntry &tool) const;
    QString preferredToolType(const QString &strategyId) const;
    QString strategyHint(const QString &strategyId) const;
    void updateStrategyHint();
    void updatePocketEntryUi();
    void updateToolCompatibilityPreview();
    void updateProposalState();
    bool isSlotFeatureSelected() const;
    void seedContourParamsFromFeature();
    void refreshTemplates();
    void applySelectedTemplate();
    void saveCurrentTemplate();
    void reviewMaterialSuggestion();

    HoleFeature m_feature;
    bool m_hasFeature = false;
    MachiningFeature m_contourFeature;
    bool m_hasContourFeature = false;
    FaceRegion m_activeRegion = FaceRegion::Unknown;
    int m_workOffset = 54;
    QString m_currentStrategyId;
    QMap<QString, StrategyParams> m_userParams;
    QLabel *m_proposalTitleLabel = nullptr;
    QLabel *m_proposalStateLabel = nullptr;
    QLabel *m_featureLabel = nullptr;
    QLabel *m_strategyLabel = nullptr;
    QLabel *m_toolLabel = nullptr;
    QLabel *m_entryModeLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_toolFitLabel = nullptr;
    QLabel *m_paramSummaryLabel = nullptr;
    QLabel *m_effectivePeckSummary = nullptr;
    QComboBox *m_strategyCombo = nullptr;
    QComboBox *m_templateCombo = nullptr;
    QLabel *m_templateLabel = nullptr;
    QLabel *m_templateStatus = nullptr;
    QPushButton *m_applyTemplateButton = nullptr;
    QPushButton *m_saveTemplateButton = nullptr;
    QPushButton *m_materialSuggestionButton = nullptr;
    QComboBox *m_toolCombo = nullptr;
    QComboBox *m_entryModeCombo = nullptr;
    QTableWidget *m_paramTable = nullptr;
    class QTabWidget *m_operationTabs = nullptr;
    QPushButton *m_reselectButton = nullptr;
    QPushButton *m_editHoleParamsBtn = nullptr;
    QPushButton *m_confirmOperationBtn = nullptr;
    QGroupBox *m_millingGroup = nullptr;
    QPushButton *m_circleMillBtn = nullptr;
};
