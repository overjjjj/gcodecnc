#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QSet>
#include "../import/StepImporter.h"

class FeatureListPanel : public QWidget
{
    Q_OBJECT
public:
    explicit FeatureListPanel(QWidget *parent = nullptr);

    void setFeatures(const QVector<MachiningFeature> &features);
    void selectFeature(int index);
    int currentFeatureIndex() const;
    QVector<int> checkedFeatureIndices() const;
    void retranslateUi();

signals:
    void featureSelected(int index);
    void featureGroupSelected(const QVector<int> &indices);
    void checkedFeaturesChanged(const QVector<int> &indices);

private:
    QVector<MachiningFeature> m_features;
    QSet<int> m_checkedFeatureIndices;
    bool m_updatingTree = false;
    QLabel *m_titleLabel;
    QTreeWidget *m_tree;
};
