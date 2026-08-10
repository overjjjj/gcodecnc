#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QSet>
#include "../import/StepImporter.h"
#include "FeatureDisplayFilter.h"

class QToolButton;

class FeatureListPanel : public QWidget
{
    Q_OBJECT
public:
    explicit FeatureListPanel(QWidget *parent = nullptr);

    void setFeatures(const QVector<MachiningFeature> &features);
    void selectFeature(int index);
    int currentFeatureIndex() const;
    QVector<int> checkedFeatureIndices() const;
    QVector<int> visibleFeatureIndices() const;
    void setFeatureFilter(FeatureDisplayFilter filter);
    FeatureDisplayFilter featureFilter() const { return m_featureFilter; }
    void retranslateUi();

public slots:
    void onActiveRegionChanged(FaceRegion region);

signals:
    void featureSelected(int index);
    void featureGroupSelected(const QVector<int> &indices);
    void checkedFeaturesChanged(const QVector<int> &indices);

private:
    void applyActiveRegionExpansion();
    void updateFilterLabels();

    QVector<MachiningFeature> m_features;
    QSet<int> m_checkedFeatureIndices;
    bool m_updatingTree = false;
    FaceRegion m_activeRegion = FaceRegion::Unknown;
    FeatureDisplayFilter m_featureFilter = FeatureDisplayFilter::All;
    QLabel *m_titleLabel;
    QToolButton *m_filterAllButton = nullptr;
    QToolButton *m_filterHoleButton = nullptr;
    QToolButton *m_filterSlotButton = nullptr;
    QToolButton *m_filterPlaneButton = nullptr;
    QToolButton *m_filterContourButton = nullptr;
    QTreeWidget *m_tree;
};
