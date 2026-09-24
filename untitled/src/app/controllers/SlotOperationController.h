#pragma once
#include <QObject>
#include <functional>
#include "../../workspaces/process_design/operations/OperationProposal.h"
#include "../../tool/ToolEntry.h"

class ProjectManager;
class FeatureListPanel;
class StrategyPanel;
class OperationListPanel;
class QWidget;

// Owns the non-executable slot draft and the modal edit transaction.
class SlotOperationController : public QObject
{
    Q_OBJECT
public:
    using ConvertFeature = std::function<ContourFeature(const MachiningFeature &)>;
    using ValidateProposal = std::function<QString(const OperationProposal &, const ToolEntry &)>;
    SlotOperationController(ProjectManager *project, FeatureListPanel *features, StrategyPanel *strategy,
                            OperationListPanel *operations, ConvertFeature convert,
                            ValidateProposal validate, QWidget *parent);
signals:
    void errorOccurred(const QString &message);
    void operationsConfirmed(const QStringList &ids);
private:
    void invalidate();
    QString contextKey() const;
    QVector<int> selectedMembers() const;
    bool chooseGeometry(OperationProposal &proposal);
    bool locate(const ContourFeature &feature) const;
    QString validate(const OperationProposal &proposal) const;
    void selectGeometry(const QString &strategyId);
    void confirm(const OperationProposal &parameters);
    void edit(MachiningOperation original);
    ProjectManager *m_project;
    FeatureListPanel *m_features;
    StrategyPanel *m_strategy;
    OperationListPanel *m_operations;
    QWidget *m_parent;
    ConvertFeature m_convert;
    ValidateProposal m_validate;
    QList<OperationProposal> m_draft;
    QVector<int> m_members;
    QString m_context;
    quint64 m_revision = 0;
};
