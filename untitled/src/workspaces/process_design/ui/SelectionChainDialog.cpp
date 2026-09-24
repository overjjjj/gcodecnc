#include "SelectionChainDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

SelectionChainDialog::SelectionChainDialog(QWidget *parent, bool slotSelection)
    : QDialog(parent), m_slotSelection(slotSelection)
{
    setWindowTitle(tr("公共几何选择与串联"));
    resize(580, 540);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(m_slotSelection
        ? tr("槽加工仅确认完整实体候选，固定内侧；入口、方向和顺序由槽策略决定，当前不支持手动修改。确认后仍须通过工序与 Setup 校验。")
        : tr("选择候选并核对方向、起点和顺序；确认后仍须通过工序与 Setup 校验。"), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *form = new QFormLayout;
    auto combo = [this, form](const char *name, const QString &label, const QStringList &items) {
        auto *widget = new QComboBox(this);
        widget->setObjectName(QString::fromLatin1(name));
        widget->addItems(items);
        form->addRow(label, widget);
        return widget;
    };
    m_source = combo("selectionSource", tr("来源"), {tr("实体"), tr("线框"), tr("毛坯")});
    m_mode = combo("selectionMode", tr("选择方式"), {tr("边"), tr("串联"), tr("面"), tr("部分串联")});
    m_mode->setCurrentIndex(1);
    m_side = combo("selectionSide", tr("加工侧"), {tr("内侧"), tr("外侧"), tr("混合")});
    m_side->setCurrentIndex(2);
    if (m_slotSelection) {
        m_context.machiningSide = ChainMachiningSide::Inside;
        m_side->setCurrentIndex(0);
    }
    m_start = combo("selectionStart", tr("起点"), {});
    m_sort = combo("selectionSort", tr("排序"), {tr("选择顺序"), tr("X 双向"), tr("Y 双向"), tr("X 单向"), tr("Y 单向"), tr("对角线"), tr("最近点")});
    layout->addLayout(form);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("selectionCandidates"));
    layout->addWidget(m_list);
    m_edges = new QListWidget(this);
    m_edges->setObjectName(QStringLiteral("selectionEdges"));
    m_edges->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_edges);
    auto *actions = new QHBoxLayout;
    auto button = [this, actions](const char *name, const QString &label, auto action) {
        auto *widget = new QPushButton(label, this);
        widget->setObjectName(QString::fromLatin1(name));
        actions->addWidget(widget);
        connect(widget, &QPushButton::clicked, this, action);
    };
    button("selectionReverse", tr("反向"), [this]() {
        if (m_slotSelection) return;
        remember(); m_controller.reverse(); refresh();
    });
    button("selectionUndo", tr("撤销"), [this]() { restoreHistory(false); });
    button("selectionRedo", tr("重做"), [this]() { restoreHistory(true); });
    button("selectionPreviousBranch", tr("上一分支"), [this]() { navigateBranch(-1); });
    button("selectionNextBranch", tr("下一分支"), [this]() { navigateBranch(1); });
    button("selectionSwitchBranch", tr("切换分支"), [this]() { navigateBranch(0); });
    layout->addLayout(actions);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SelectionChainDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SelectionChainDialog::reject);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &SelectionChainDialog::pickCandidate);
    connect(m_edges, &QListWidget::itemSelectionChanged, this, [this]() { remember(); updateGeometry(); });
    connect(m_source, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_slotSelection) return;
        remember();
        m_candidateIndex = -1;
        m_controller = SelectionChainController(m_context);
        m_controller.begin();
        m_controller.setGeometrySource(static_cast<ChainGeometrySource>(index));
        refresh();
    });
    connect(m_mode, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_slotSelection) return;
        remember(); m_controller.setSelectionMode(static_cast<ChainSelectionMode>(index)); updateGeometry();
    });
    connect(m_side, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_slotSelection) return;
        remember(); m_controller.setMachiningSide(static_cast<ChainMachiningSide>(index)); refresh();
    });
    connect(m_sort, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_slotSelection) return;
        remember(); m_controller.sort(static_cast<ChainSortStrategy>(index)); refresh();
    });
    connect(m_start, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_slotSelection || m_candidateIndex < 0) return;
        remember();
        const auto &points = m_candidates.at(m_candidateIndex).points;
        if (index >= 0 && index < points.size()) m_controller.setStartPoint(points.at(index));
        refresh();
    });
}

void SelectionChainDialog::setCandidates(const QList<SelectionCandidate> &candidates)
{
    m_candidates = candidates;
    m_candidateContext = m_context;
    m_selection = {};
    m_candidateIndex = -1;
    m_controller = SelectionChainController(m_context);
    m_controller.begin();
    m_controller.setGeometrySource(static_cast<ChainGeometrySource>(m_source->currentIndex()));
    m_undo.clear();
    m_redo.clear();
    setResult(QDialog::Rejected);
    refresh();
}

void SelectionChainDialog::setContext(const QString &source, const QString &setup,
                                      const QString &coordinate, const QVector3D &axis)
{
    m_context.sourceFingerprint = source;
    m_context.setupFingerprint = setup;
    m_context.coordinateSystemId = coordinate;
    m_context.toolAxis = axis;
    if (m_candidateIndex < 0) {
        m_controller.setSourceContext(source, setup);
        m_controller.setCoordinateSystemId(coordinate);
        m_controller.setToolAxis(axis);
    }
}

void SelectionChainDialog::pickCandidate()
{
    if (!m_list->currentItem() || !m_list->currentItem()->isSelected()) {
        if (m_candidateIndex < 0) return;
        remember();
        m_candidateIndex = -1;
        m_controller = SelectionChainController(m_candidateContext);
        m_controller.begin();
        m_controller.setGeometrySource(static_cast<ChainGeometrySource>(m_source->currentIndex()));
        refresh();
        return;
    }
    remember();
    m_candidateIndex = m_list->currentItem()->data(Qt::UserRole).toInt();
    const auto &candidate = m_candidates.at(m_candidateIndex);
    m_controller = SelectionChainController(m_candidateContext);
    m_controller.begin();
    m_controller.setGeometrySource(candidate.source);
    m_controller.setSelectionMode(static_cast<ChainSelectionMode>(m_mode->currentIndex()));
    m_controller.setMachiningSide(static_cast<ChainMachiningSide>(m_side->currentIndex()));
    m_controller.setSelectedSurfaceNormal(candidate.surfaceNormal);
    m_controller.setClosed(candidate.closed);
    m_controller.setTopologyEvidence(candidate.outerLoopPointCount, candidate.islandCount, candidate.freeEndCount);
    m_controller.setBranchChoices(candidate.branchIds, candidate.branchIds.indexOf(candidate.geometryId));
    m_start->clear();
    const QSignalBlocker blocker(m_edges);
    m_edges->clear();
    for (int index = 0; index < candidate.points.size(); ++index) {
        if (candidate.closed || index + 1 < candidate.points.size()) {
            const QVector3D point = candidate.points.at(index);
            m_edges->addItem(tr("边 %1 · 起点 (%2, %3, %4)").arg(index + 1)
                .arg(point.x()).arg(point.y()).arg(point.z()));
        }
        m_start->addItem(QString::number(index + 1));
    }
    m_start->setCurrentIndex(-1);
    updateGeometry();
}

void SelectionChainDialog::updateGeometry()
{
    if (m_candidateIndex < 0) return;
    const auto &candidate = m_candidates.at(m_candidateIndex);
    const auto mode = static_cast<ChainSelectionMode>(m_mode->currentIndex());
    const bool edges = mode == ChainSelectionMode::Edge || mode == ChainSelectionMode::PartialChain;
    m_edges->setEnabled(edges);
    const QStringList existing = m_controller.geometryIds();
    for (const auto &id : existing) m_controller.removeGeometry(id);
    if (mode == ChainSelectionMode::Face || (!edges && candidate.points.isEmpty())) {
        m_controller.addGeometry(candidate.geometryId, candidate.referencePoint);
    } else {
        for (int offset = 0; offset < m_edges->count(); ++offset) {
            const int index = m_controller.reversed() ? m_edges->count() - 1 - offset : offset;
            if (!edges || m_edges->item(index)->isSelected()) {
                m_controller.addGeometry(candidate.geometryId + QStringLiteral("/edge:%1").arg(index), candidate.points.at(index));
            }
        }
    }
    const bool whole = !edges || m_controller.geometryIds().size() == m_edges->count();
    m_controller.setClosed(candidate.closed && whole);
    m_controller.setTopologyEvidence(whole ? candidate.outerLoopPointCount : 0,
                                    candidate.islandCount, whole ? candidate.freeEndCount : 2);
    refresh();
}

void SelectionChainDialog::navigateBranch(int offset)
{
    if (m_slotSelection) return;
    SelectionChainController navigator = m_controller;
    const QString id = offset < 0 ? navigator.previousBranch()
        : offset > 0 ? navigator.nextBranch() : navigator.switchBranch();
    for (int row = 0; row < m_list->count(); ++row) {
        const int index = m_list->item(row)->data(Qt::UserRole).toInt();
        if (m_candidates.at(index).geometryId == id) { m_list->setCurrentRow(row); return; }
    }
    m_status->setText(tr("当前候选没有可用分支。"));
}

void SelectionChainDialog::refresh()
{
    SelectionChainController preview = m_controller;
    const SelectionChain draft = preview.commit();
    const QSignalBlocker sideBlock(m_side), modeBlock(m_mode), sortBlock(m_sort), startBlock(m_start),
        sourceBlock(m_source), listBlock(m_list), edgesBlock(m_edges);
    m_source->setCurrentIndex(static_cast<int>(draft.geometrySource));
    m_list->clear();
    for (int index = 0; index < m_candidates.size(); ++index) {
        const auto &candidate = m_candidates.at(index);
        if (candidate.source != draft.geometrySource) continue;
        auto *item = new QListWidgetItem(candidate.label.isEmpty() ? candidate.geometryId : candidate.label, m_list);
        item->setData(Qt::UserRole, index);
        if (index == m_candidateIndex) m_list->setCurrentItem(item);
    }
    m_edges->clear();
    m_start->clear();
    const bool edges = draft.selectionMode == ChainSelectionMode::Edge || draft.selectionMode == ChainSelectionMode::PartialChain;
    m_edges->setEnabled(edges);
    if (m_candidateIndex >= 0) {
        const auto &candidate = m_candidates.at(m_candidateIndex);
        for (int index = 0; index < candidate.points.size(); ++index) {
            m_start->addItem(QString::number(index + 1));
            if (!candidate.closed && index + 1 == candidate.points.size()) continue;
            const auto point = candidate.points.at(index);
            auto *item = new QListWidgetItem(tr("边 %1 · 起点 (%2, %3, %4)").arg(index + 1)
                .arg(point.x()).arg(point.y()).arg(point.z()), m_edges);
            item->setSelected(draft.orderedGeometryIds.contains(candidate.geometryId + QStringLiteral("/edge:%1").arg(index)));
        }
    }
    m_side->setCurrentIndex(static_cast<int>(draft.machiningSide));
    m_mode->setCurrentIndex(static_cast<int>(draft.selectionMode));
    m_sort->setCurrentIndex(static_cast<int>(draft.sortStrategy));
    if (!draft.hasStartPoint) m_start->setCurrentIndex(-1);
    else if (m_candidateIndex >= 0) m_start->setCurrentIndex(m_candidates.at(m_candidateIndex).points.indexOf(draft.startPoint));
    QStringList order;
    for (const auto &id : draft.orderedGeometryIds) {
        const int edge = id.lastIndexOf(QStringLiteral("/edge:"));
        order.append(edge < 0 ? tr("面") : QString::number(id.mid(edge + 6).toInt() + 1));
    }
    m_status->setText(tr("%1 个几何元素 · %2\n顺序：%3\n表面法向 (%4, %5, %6) · %7")
        .arg(m_controller.geometryIds().size()).arg(draft.reversed ? tr("反向") : tr("正向"))
        .arg(order.join(QStringLiteral(" → ")))
        .arg(draft.selectedSurfaceNormal.x()).arg(draft.selectedSurfaceNormal.y())
        .arg(draft.selectedSurfaceNormal.z()).arg(draft.coordinateSystemId));
    for (const auto &name : {"selectionPreviousBranch", "selectionNextBranch", "selectionSwitchBranch"}) {
        findChild<QPushButton *>(QString::fromLatin1(name))->setEnabled(
            !m_slotSelection && m_candidateIndex >= 0 && m_candidates.at(m_candidateIndex).branchIds.size() > 1);
    }
    findChild<QPushButton *>(QStringLiteral("selectionUndo"))->setEnabled(!m_undo.isEmpty());
    findChild<QPushButton *>(QStringLiteral("selectionRedo"))->setEnabled(!m_redo.isEmpty());
    findChild<QPushButton *>(QStringLiteral("selectionReverse"))->setEnabled(
        !m_slotSelection && !draft.orderedGeometryIds.isEmpty() && draft.selectionMode != ChainSelectionMode::Face);
    m_start->setEnabled(!m_slotSelection && m_start->count() > 0 && draft.selectionMode != ChainSelectionMode::Face);
    m_sort->setEnabled(!m_slotSelection && draft.orderedGeometryIds.size() > 1);
    m_source->setEnabled(!m_slotSelection);
    m_mode->setEnabled(!m_slotSelection);
    m_side->setEnabled(!m_slotSelection);
}

void SelectionChainDialog::remember()
{
    m_undo.append({m_controller, m_candidateIndex});
    m_redo.clear();
    m_selection = {};
}

void SelectionChainDialog::restoreHistory(bool redo)
{
    auto &from = redo ? m_redo : m_undo;
    auto &to = redo ? m_undo : m_redo;
    if (from.isEmpty()) return;
    to.append({m_controller, m_candidateIndex});
    const auto previous = from.takeLast();
    m_controller = previous.controller;
    m_candidateIndex = previous.candidateIndex;
    m_selection = {};
    refresh();
}

void SelectionChainDialog::accept()
{
    if (!m_controller.finishChain()) { m_status->setText(tr("请先选择几何候选。")); return; }
    SelectionChainController preview = m_controller;
    const auto draft = preview.commit();
    if (m_candidateIndex >= 0 && !m_candidates.at(m_candidateIndex).points.isEmpty() &&
        orderedPoints(draft).isEmpty()) {
        m_status->setText(tr("所选边不连续或起点不在边界上。请撤销排序，或重新选择连续边和有效起点。"));
        return;
    }
    const auto access = evaluateSelectionAccess(draft, m_context.sourceFingerprint,
        m_context.setupFingerprint, m_context.coordinateSystemId, m_requireClosed);
    if (!access.ok()) {
        m_status->setText(tr("选择无法确认，请检查模型、Setup/WCS、方向与边界：%1").arg(access.issueCode));
        return;
    }
    m_selection = m_controller.commit();
    QDialog::accept();
}

QVector<QVector3D> SelectionChainDialog::orderedPoints(const SelectionChain &draft) const
{
    if (m_candidateIndex < 0) return {};
    const auto &candidate = m_candidates.at(m_candidateIndex);
    if (draft.selectionMode == ChainSelectionMode::Face) return candidate.points;
    const int count = candidate.points.size();
    if (count < 2) return {};
    QVector<int> indices;
    for (const QString &id : draft.orderedGeometryIds) {
        const QString prefix = candidate.geometryId + QStringLiteral("/edge:");
        if (!id.startsWith(prefix)) return {};
        bool ok = false;
        const int index = id.mid(prefix.size()).toInt(&ok);
        if (!ok || index < 0 || index >= (candidate.closed ? count : count - 1)) return {};
        if (!indices.isEmpty()) {
            int expected = indices.last() + (draft.reversed ? -1 : 1);
            if (candidate.closed) expected = (expected + count) % count;
            if (index != expected) return {};
        }
        indices.append(index);
    }
    if (indices.isEmpty()) return {};
    QVector<QVector3D> points;
    if (draft.reversed) points.append(candidate.points.at((indices.first() + 1) % count));
    for (int index : indices) points.append(candidate.points.at(index));
    if (!draft.reversed) points.append(candidate.points.at((indices.last() + 1) % count));
    if (draft.closed && points.first() == points.last()) points.removeLast();
    if (draft.hasStartPoint) {
        const int start = points.indexOf(draft.startPoint);
        if (start < 0 || (!draft.closed && start != 0)) return {};
        if (draft.closed) std::rotate(points.begin(), points.begin() + start, points.end());
    }
    return points;
}

QVector<QVector3D> SelectionChainDialog::selectedPoints() const
{
    return m_selection.explicitUserSelection ? orderedPoints(m_selection) : QVector<QVector3D>();
}

void SelectionChainDialog::reject()
{
    m_controller.cancel();
    m_selection = {};
    QDialog::reject();
}
