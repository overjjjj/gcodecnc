#include "SelectionChainDialog.h"
#include "../selection/CoplanarSelection.h"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QVBoxLayout>
#include <algorithm>

static SelectionBoundaryLoop machiningLoop(const SelectionCandidate &candidate,
                                           const SelectionBoundaryLoop &source)
{
    for (const auto &proof : candidate.machiningBoundaries)
        if (proof.faceId == source.faceId && proof.orderedEdgeIds == source.orderedEdgeIds &&
            proof.orderedForward == source.orderedForward && proof.outer == source.outer) return proof;
    return source;
}

SelectionChainDialog::SelectionChainDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("选择加工区域"));
    setModal(false);
    setWindowFlag(Qt::Tool, true);
    setMinimumWidth(340);
    resize(380, 590);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("在模型上选择线或面。选择期间可旋转、缩放模型。"), this);
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
    m_start = combo("selectionStart", tr("起点"), {});
    m_sort = combo("selectionSort", tr("排序"), {tr("选择顺序"), tr("X 双向"), tr("Y 双向"), tr("X 单向"), tr("Y 单向"), tr("对角线"), tr("最近点")});
    m_topologyContext = combo("selectionTopologyContext", tr("拓扑范围"), {});
    m_topologyContextLabel = qobject_cast<QLabel *>(form->labelForField(m_topologyContext));
    m_topologyContext->hide();
    if (m_topologyContextLabel) m_topologyContextLabel->hide();
    layout->addLayout(form);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("selectionCandidates"));
    layout->addWidget(m_list);
    m_edges = new QListWidget(this);
    m_edges->setObjectName(QStringLiteral("selectionEdges"));
    m_edges->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_edges);
    m_edges->hide();

    auto *boundaryOffer = new QHBoxLayout;
    m_useFullBoundary = new QPushButton(tr("使用完整边界"), this);
    m_useFullBoundary->setObjectName(QStringLiteral("selectionUseFullBoundary"));
    m_adjustBoundary = new QPushButton(tr("调整边界"), this);
    m_adjustBoundary->setObjectName(QStringLiteral("selectionAdjustBoundary"));
    boundaryOffer->addWidget(m_useFullBoundary);
    boundaryOffer->addWidget(m_adjustBoundary);
    layout->addLayout(boundaryOffer);

    auto *adjustModes = new QHBoxLayout;
    m_connectEdges = new QPushButton(tr("沿边连接"), this);
    m_connectEdges->setObjectName(QStringLiteral("selectionConnectEdges"));
    m_extractSegment = new QPushButton(tr("截取一段"), this);
    m_extractSegment->setObjectName(QStringLiteral("selectionExtractSegment"));
    m_acceptNextEdge = new QPushButton(tr("接下一段"), this);
    m_acceptNextEdge->setObjectName(QStringLiteral("selectionAcceptNextEdge"));
    adjustModes->addWidget(m_connectEdges);
    adjustModes->addWidget(m_extractSegment);
    adjustModes->addWidget(m_acceptNextEdge);
    layout->addLayout(adjustModes);
    auto *actions = new QGridLayout;
    int actionIndex = 0;
    auto button = [this, actions, &actionIndex](const char *name, const QString &label, auto action) {
        auto *widget = new QPushButton(label, this);
        widget->setObjectName(QString::fromLatin1(name));
        actions->addWidget(widget, actionIndex / 3, actionIndex % 3);
        ++actionIndex;
        connect(widget, &QPushButton::clicked, this, action);
        return widget;
    };
    button("selectionReverse", tr("反向"), [this]() { remember(); clearTopologyPreview(); m_controller.reverse(); refresh(); });
    button("selectionUndo", tr("撤销上一步"), [this]() { restoreHistory(false); });
    m_lastSelection = button("selectionLast", tr("选择上次"), [this]() { restorePreviousSelection(); });
    m_lastSelection->setEnabled(false);
    m_lastSelection->setToolTip(tr("恢复当前项目最近建立工序的选择，核对后再次确认。"));
    m_coplanar = button("selectionCoplanar", tr("选择同平面区域"), [this]() { selectCoplanarRegions(); });
    auto *redoButton = button("selectionRedo", tr("重做"), [this]() { restoreHistory(true); });
    auto *previousBranch = button("selectionPreviousBranch", tr("上一分支"), [this]() { navigateBranch(-1); });
    auto *nextBranch = button("selectionNextBranch", tr("下一分支"), [this]() { navigateBranch(1); });
    auto *switchBranch = button("selectionSwitchBranch", tr("确认分支"), [this]() { navigateBranch(0); });
    m_finishBoundary = button("selectionFinishChain", tr("完成边界"), [this]() {
        if (m_controller.geometryIds().isEmpty()) return;
        if (m_controller.state() != SelectionChainState::Ordered) {
            remember();
            if (!m_controller.finishChain()) return;
        }
        m_adjustingBoundary = false;
        m_hasAdjustBaseline = false;
        refresh();
    });
    auto *pickStart = button("selectionPickStart", tr("拾取起点"), [this]() {
        if (m_pickingStart) return;
        remember();
        m_pickingStart = true;
        syncPickMode();
        m_status->setText(tr("请在模型上点击一个拓扑顶点作为起点。"));
    });
    redoButton->hide();
    previousBranch->setToolTip(tr("预览上一条合法延续，不改变已选边。"));
    nextBranch->setToolTip(tr("预览下一条合法延续，不改变已选边。"));
    switchBranch->setToolTip(tr("将当前预览的合法延续接入边界。"));
    pickStart->setToolTip(tr("闭合链可选任一顶点；开放链可选首末端点。"));
    m_confirmTopologyContext = new QPushButton(tr("确认此范围"), this);
    m_confirmTopologyContext->setObjectName(QStringLiteral("selectionConfirmTopologyContext"));
    m_confirmTopologyContext->hide();
    layout->addWidget(m_confirmTopologyContext);
    layout->addLayout(actions);
    m_regions = new QListWidget(this);
    m_regions->setObjectName(QStringLiteral("selectionRegions"));
    m_regions->setMaximumHeight(120);
    layout->addWidget(m_regions);
    m_removeRegion = new QPushButton(tr("移除选中区域"), this);
    m_removeRegion->setObjectName(QStringLiteral("selectionRemoveRegion"));
    layout->addWidget(m_removeRegion);
    connect(m_removeRegion, &QPushButton::clicked, this, [this]() {
        const int row = m_regions->currentRow();
        if (row < 0 || m_regionSelections.size() < 2) return;
        remember();
        m_regionSelections.removeAt(row);
        m_controller = SelectionChainController(m_regionSelections.first());
        for (int i = 0; i < m_candidates.size(); ++i)
            if (m_candidates.at(i).geometryId == m_regionSelections.first().candidateGeometryId)
                m_candidateIndex = i;
        refresh();
        syncTopologyScope();
    });
    m_chainStatus = new QLabel(this);
    m_chainStatus->setObjectName(QStringLiteral("selectionChainStatus"));
    m_chainStatus->setWordWrap(true);
    layout->addWidget(m_chainStatus);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("selectionStatus"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_nextButton = buttons->button(QDialogButtonBox::Ok);
    m_nextButton->setObjectName(QStringLiteral("selectionNext"));
    m_nextButton->setText(tr("完成选择"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SelectionChainDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SelectionChainDialog::reject);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &SelectionChainDialog::pickCandidate);
    connect(m_edges, &QListWidget::itemSelectionChanged, this, [this]() { remember(); updateGeometry(); });
    connect(m_topologyContext, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) {
        m_pendingTopologyPick.matchIndex = index;
        previewPendingTopologyMatch();
    });
    connect(m_confirmTopologyContext, &QPushButton::clicked, this,
            &SelectionChainDialog::confirmPendingTopologyMatch);
    connect(m_useFullBoundary, &QPushButton::clicked,
            this, &SelectionChainDialog::useFullBoundary);
    connect(m_adjustBoundary, &QPushButton::clicked, this, [this]() {
        beginBoundaryAdjustment(ChainSelectionMode::Edge);
    });
    connect(m_connectEdges, &QPushButton::clicked, this, [this]() {
        beginBoundaryAdjustment(ChainSelectionMode::Edge);
    });
    connect(m_extractSegment, &QPushButton::clicked, this, [this]() {
        beginBoundaryAdjustment(ChainSelectionMode::PartialChain);
    });
    connect(m_acceptNextEdge, &QPushButton::clicked,
            this, &SelectionChainDialog::acceptUniqueNextEdge);
    connect(m_source, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        remember();
        resetTransientSession();
        m_candidateIndex = -1;
        m_controller = SelectionChainController(m_context);
        m_controller.begin();
        m_controller.setGeometrySource(static_cast<ChainGeometrySource>(index));
        refresh();
    });
    connect(m_mode, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_edgePickingOnly && index == static_cast<int>(ChainSelectionMode::Face)) {
            index = static_cast<int>(ChainSelectionMode::Chain);
            m_mode->setCurrentIndex(index);
        }
        SelectionChainController preview = m_controller;
        const SelectionChain previous = preview.commit();
        remember();
        resetTransientSession();
        if (previous.evidenceVersion >= 2 && !previous.orderedTopologyRefs.isEmpty() &&
            m_candidateIndex >= 0) {
            const SelectionCandidate &candidate = m_candidates.at(m_candidateIndex);
            m_controller = SelectionChainController(m_candidateContext);
            m_controller.begin();
            m_controller.setGeometrySource(candidate.source);
            m_controller.setSelectionMode(static_cast<ChainSelectionMode>(index));
            m_controller.setMachiningSide(previous.machiningSide);
            m_controller.setSelectedSurfaceNormal(candidate.surfaceNormal);
            m_controller.setClosed(false);
            m_controller.setTopologyEvidence(0, 0, 0);
            refresh();
        } else {
            m_controller.setSelectionMode(static_cast<ChainSelectionMode>(index));
            updateGeometry();
        }
        if (m_candidateIndex >= 0 && m_candidates.at(m_candidateIndex).topologyOnly &&
            (index == static_cast<int>(ChainSelectionMode::Edge) ||
             index == static_cast<int>(ChainSelectionMode::PartialChain))) {
            m_adjustingBoundary = true;
            refresh();
        }
        syncPickMode();
    });
    connect(m_side, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        remember(); m_controller.setMachiningSide(static_cast<ChainMachiningSide>(index));
        for (auto &region : m_regionSelections) region.machiningSide = static_cast<ChainMachiningSide>(index);
        refresh();
    });
    connect(m_sort, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        remember(); m_controller.sort(static_cast<ChainSortStrategy>(index)); refresh();
    });
    connect(m_start, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (m_candidateIndex < 0) return;
        SelectionChainController preview = m_controller;
        const SelectionChain draft = preview.commit();
        if (draft.evidenceVersion >= 2) {
            if (!m_start->isEnabled()) return;
            const QString id = m_start->itemData(index).toString();
            const auto *vertex = vertexForId(m_candidates.at(m_candidateIndex), id);
            if (!vertex) return;
            selectStartVertex(id);
            return;
        }
        remember();
        const auto &points = m_candidates.at(m_candidateIndex).points;
        if (index >= 0 && index < points.size()) m_controller.setStartPoint(points.at(index));
        refresh();
    });

    m_moreDetails = new QPushButton(tr("更多设置与边列表"), this);
    m_moreDetails->setObjectName(QStringLiteral("selectionMoreDetails"));
    m_moreDetails->setCheckable(true);
    layout->insertWidget(layout->indexOf(m_edges), m_moreDetails);
    const auto setAdvancedVisible = [this, form](bool visible) {
        for (QComboBox *combo : {m_source, m_mode, m_side, m_start, m_sort}) {
            combo->setVisible(visible);
            if (QWidget *label = form->labelForField(combo)) label->setVisible(visible);
        }
        m_edges->setVisible(visible);
    };
    connect(m_moreDetails, &QPushButton::toggled, this,
            [this, setAdvancedVisible](bool visible) {
        setAdvancedVisible(visible);
        refresh();
    });
    setAdvancedVisible(false);
}

void SelectionChainDialog::setCandidates(const QList<SelectionCandidate> &candidates)
{
    m_candidates = candidates;
    if (!candidates.isEmpty() && std::none_of(candidates.cbegin(),candidates.cend(),[this](const SelectionCandidate &candidate) {
            return int(candidate.source)==m_source->currentIndex();
        })) m_source->setCurrentIndex(int(candidates.first().source));
    if (auto *model = qobject_cast<QStandardItemModel *>(m_source->model())) {
        for (int source = 0; source < m_source->count(); ++source) {
            bool available = false;
            for (const auto &candidate : candidates)
                if (int(candidate.source) == source) available = true;
            model->item(source)->setEnabled(available);
            model->item(source)->setToolTip(available ? QString() : tr("当前模型没有此来源的几何候选"));
        }
    }
    m_candidateContext = m_context;
    m_selection = {};
    m_candidateIndex = -1;
    m_controller = SelectionChainController(m_context);
    m_controller.begin();
    m_controller.setGeometrySource(static_cast<ChainGeometrySource>(m_source->currentIndex()));
    m_undo.clear();
    m_redo.clear();
    resetTransientSession();
    setResult(QDialog::Rejected);
    refresh();
    syncPickMode();
    syncTopologyScope();
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
        resetTransientSession();
        m_candidateIndex = -1;
        m_controller = SelectionChainController(m_candidateContext);
        m_controller.begin();
        m_controller.setGeometrySource(static_cast<ChainGeometrySource>(m_source->currentIndex()));
        refresh();
        syncTopologyScope();
        return;
    }
    remember();
    resetTransientSession();
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
    syncTopologyScope();
}

void SelectionChainDialog::setEdgePickingOnly(bool required)
{
    m_edgePickingOnly = required;
    if (auto *model = qobject_cast<QStandardItemModel *>(m_mode->model())) {
        if (QStandardItem *faceItem = model->item(static_cast<int>(ChainSelectionMode::Face)))
            faceItem->setEnabled(!required);
    }
    if (required && m_mode->currentIndex() == static_cast<int>(ChainSelectionMode::Face)) {
        m_mode->setCurrentIndex(static_cast<int>(ChainSelectionMode::Chain));
        m_controller.setSelectionMode(ChainSelectionMode::Chain);
        refresh();
    }
    syncPickMode();
}

void SelectionChainDialog::updateGeometry()
{
    if (m_candidateIndex < 0) return;
    const auto &candidate = m_candidates.at(m_candidateIndex);
    const auto mode = static_cast<ChainSelectionMode>(m_mode->currentIndex());
    if (candidate.topologyOnly) {
        if (mode == ChainSelectionMode::Face) {
            clearTopologyPreview();
            QList<SelectionTopologyRef> refs;
            QList<SelectionBoundaryLoop> loops;
            for (const QString &id : candidate.faceIds)
                refs.append({id, SelectionTopologyKind::Face, true});
            for (const MeshBoundaryLoop &loop : candidate.boundaryLoops)
                loops.append({loop.faceId, loop.outer, loop.orderedEdgeIds, loop.orderedForward});
            applyTopologySelection(refs, {}, false, {}, false, loops, true);
        } else if (mode == ChainSelectionMode::Chain) {
            prepareFullBoundaryOffer();
        } else {
            clearTopologyPreview();
            refresh();
            m_status->setText(tr("已定位真实模型区域。请在模型上点击起始边；范围外误点不会清空当前链。"));
        }
        return;
    }
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

void SelectionChainDialog::clearTopologyPreview()
{
    m_topologyPreviewKind = TopologyPreviewKind::None;
    m_topologyPreviewDraft = {};
    m_topologyPreviewTerminalEdgeId.clear();
}

bool SelectionChainDialog::buildLoopDraft(int loopIndex, int firstIndex, int step,
                                          int count, SelectionChain *draft) const
{
    if (!draft || m_candidateIndex < 0 || m_candidateIndex >= m_candidates.size()) return false;
    const SelectionCandidate &candidate = m_candidates.at(m_candidateIndex);
    if (loopIndex < 0 || loopIndex >= candidate.boundaryLoops.size()) return false;
    const MeshBoundaryLoop &loop = candidate.boundaryLoops.at(loopIndex);
    const int edgeCount = loop.orderedEdgeIds.size();
    if (edgeCount == 0 || firstIndex < 0 || firstIndex >= edgeCount || count <= 0 ||
        count > edgeCount || (step != 1 && step != -1)) return false;

    SelectionChainController preview = m_controller;
    SelectionChain value = preview.commit();
    value.evidenceVersion = 2;
    value.orderedGeometryIds.clear();
    value.orderedTopologyRefs.clear();
    int cursor = firstIndex;
    for (int offset = 0; offset < count; ++offset) {
        const QString edgeId = loop.orderedEdgeIds.at(cursor);
        const MeshPickEdge *picked = edgeForId(candidate, edgeId);
        if (!picked || picked->points.isEmpty()) return false;
        const bool forward = step > 0
            ? loop.orderedForward.value(cursor, true)
            : !loop.orderedForward.value(cursor, true);
        value.orderedGeometryIds.append(edgeId);
        value.orderedTopologyRefs.append({edgeId, SelectionTopologyKind::Edge, forward});
        cursor = (cursor + step + edgeCount) % edgeCount;
    }
    const SelectionTopologyRef &firstRef = value.orderedTopologyRefs.first();
    const SelectionTopologyRef &lastRef = value.orderedTopologyRefs.last();
    const MeshPickEdge *firstEdge = edgeForId(candidate, firstRef.id);
    const MeshPickEdge *lastEdge = edgeForId(candidate, lastRef.id);
    if (!firstEdge || !lastEdge) return false;
    value.startAnchor = {
        firstRef.forward ? firstEdge->firstVertexId : firstEdge->lastVertexId,
        firstRef.forward ? firstEdge->points.first() : firstEdge->points.last(), false, 0.0};
    value.hasStartAnchor = true;
    value.endAnchor = {
        lastRef.forward ? lastEdge->lastVertexId : lastEdge->firstVertexId,
        lastRef.forward ? lastEdge->points.last() : lastEdge->points.first(), false, 0.0};
    value.closed = value.endAnchor.topologyId == value.startAnchor.topologyId &&
                   (value.endAnchor.point - value.startAnchor.point).length() <= 1.0e-4f;
    value.hasEndAnchor = !value.closed;
    const auto boundary = machiningLoop(candidate, {loop.faceId, loop.outer,
                                                     loop.orderedEdgeIds, loop.orderedForward});
    value.boundaryLoops = {boundary};
    value.outerLoopPointCount = value.closed && (loop.outer || (boundary.machiningRole == MachiningBoundaryRole::Outer)) ? edgeCount : 0;
    value.islandCount = boundary.machiningRole == MachiningBoundaryRole::Island ? 1 : 0;
    value.freeEndCount = value.closed ? 0 : 2;
    value.startPoint = value.startAnchor.point;
    value.hasStartPoint = true;
    if (!topologyRefsContinuous(candidate, value)) return false;
    *draft = value;
    return true;
}

void SelectionChainDialog::prepareFullBoundaryOffer()
{
    clearTopologyPreview();
    m_adjustingBoundary = false;
    if (m_candidateIndex < 0 || m_candidateIndex >= m_candidates.size()) {
        refresh();
        return;
    }
    const SelectionCandidate &candidate = m_candidates.at(m_candidateIndex);
    if (candidate.boundaryLoops.size() == 1) {
        SelectionChain draft;
        const int count = candidate.boundaryLoops.first().orderedEdgeIds.size();
        if (buildLoopDraft(0, 0, 1, count, &draft) && draft.closed) {
            m_topologyPreviewKind = TopologyPreviewKind::FullBoundary;
            m_topologyPreviewDraft = draft;
        }
    }
    refresh();
    if (m_topologyPreviewKind == TopologyPreviewKind::None) {
        m_status->setText(candidate.boundaryLoops.size() > 1
            ? tr("此范围包含多个外环/内环。请在模型上选择具体边界，系统不会默认采用第一环。")
            : tr("没有唯一且闭合的真实边界。请调整边界；开放槽不会自动补线。"));
    }
}

void SelectionChainDialog::useFullBoundary()
{
    if (m_topologyPreviewKind != TopologyPreviewKind::FullBoundary) return;
    remember();
    const SelectionChain draft = m_topologyPreviewDraft;
    clearTopologyPreview();
    m_adjustingBoundary = false;
    applyTopologySelection(draft.orderedTopologyRefs, draft.startAnchor,
                           draft.hasStartAnchor, draft.endAnchor,
                           draft.hasEndAnchor, draft.boundaryLoops, draft.closed);
    const auto selected = m_controller.commit();
    const auto access = evaluateSelectionAccess(selected, m_context.sourceFingerprint,
        m_context.setupFingerprint, m_context.coordinateSystemId, m_requireClosed);
    const QString error = !access.ok() ? accessFailureText(selected, access)
        : acceptanceError ? acceptanceError(selected, selectedCandidateGeometryId()) : QString();
    m_status->setText(error.isEmpty()
        ? tr("已采用完整真实边界。可反向或完成边界。") : error);
}

void SelectionChainDialog::beginBoundaryAdjustment(ChainSelectionMode mode)
{
    if (m_candidateIndex < 0) return;
    if (!m_adjustingBoundary) {
        m_adjustBaseline = snapshot();
        m_hasAdjustBaseline = true;
    }
    clearTopologyPreview();
    m_adjustingBoundary = true;
    m_controller.finishChain();
    m_mode->setCurrentIndex(static_cast<int>(mode));
    m_controller.setSelectionMode(mode);
    m_partialStartEdgeId.clear();
    m_partialLoopFaceId.clear();
    m_partialLoopCandidateIndex = -1;
    m_partialLoopIndex = -1;
    m_partialStartIndex = -1;
    m_partialStep = 1;
    m_partialStartAnchor = {};
    refresh();
    syncPickMode();
    m_status->setText(mode == ChainSelectionMode::PartialChain
        ? tr("截取一段：依次在模型上选择起始边和终止边。第二击前可预览方向。")
        : tr("沿边连接：选择起始边；悬停后续边可预览连续区间，单击确认。"));
}

void SelectionChainDialog::acceptUniqueNextEdge()
{
    if (m_candidateIndex < 0 || m_branchEdgeIds.size() != 1) return;
    const MeshPickEdge *edge = edgeForId(m_candidates.at(m_candidateIndex),
                                         m_branchEdgeIds.first());
    if (!edge) return;
    SelectionChainController preview = m_controller;
    const SelectionChain draft = preview.commit();
    if (!draft.hasEndAnchor) return;
    const bool atFirst = edge->firstVertexId == draft.endAnchor.topologyId;
    if (!atFirst && edge->lastVertexId != draft.endAnchor.topologyId) return;
    selectTopology(edge->id, static_cast<int>(SelectionTopologyKind::Edge),
                   atFirst ? edge->points.first() : edge->points.last(),
                   atFirst ? edge->parameters.value(0, 0.0)
                           : edge->parameters.value(edge->parameters.size() - 1, 1.0));
}

bool SelectionChainDialog::buildIntervalPreview(const QString &terminalEdgeId,
                                                SelectionChain *result) const
{
    if (!result || m_candidateIndex < 0 || m_candidateIndex >= m_candidates.size()) return false;
    const SelectionCandidate &candidate = m_candidates.at(m_candidateIndex);
    const MeshPickEdge *terminal = edgeForId(candidate, terminalEdgeId);
    if (!terminal) return false;
    SelectionChainController preview = m_controller;
    const SelectionChain draft = preview.commit();
    if (draft.closed || !draft.hasEndAnchor || draft.orderedTopologyRefs.isEmpty()) return false;
    for (const SelectionTopologyRef &ref : draft.orderedTopologyRefs)
        if (ref.id == terminalEdgeId) return false;

    QList<SelectionTopologyRef> appended;
    QString expectedVertex = draft.endAnchor.topologyId;
    QVector3D expectedPoint = draft.endAnchor.point;
    QList<SelectionBoundaryLoop> loops = draft.boundaryLoops;

    if (m_activeLoopIndex >= 0 && m_activeLoopIndex < candidate.boundaryLoops.size()) {
        const MeshBoundaryLoop &loop = candidate.boundaryLoops.at(m_activeLoopIndex);
        const int lastIndex = loop.orderedEdgeIds.indexOf(draft.orderedTopologyRefs.last().id);
        const int terminalIndex = loop.orderedEdgeIds.indexOf(terminalEdgeId);
        if (lastIndex >= 0 && terminalIndex >= 0) {
            const bool followsLoop = draft.orderedTopologyRefs.last().forward ==
                                     loop.orderedForward.value(lastIndex, true);
            const int step = followsLoop ? 1 : -1;
            int cursor = (lastIndex + step + loop.orderedEdgeIds.size()) %
                         loop.orderedEdgeIds.size();
            for (int count = 0; count < loop.orderedEdgeIds.size(); ++count) {
                const QString id = loop.orderedEdgeIds.at(cursor);
                bool alreadySelected = false;
                for (const SelectionTopologyRef &ref : draft.orderedTopologyRefs)
                    alreadySelected |= ref.id == id;
                if (alreadySelected) return false;
                const MeshPickEdge *edge = edgeForId(candidate, id);
                if (!edge) return false;
                const bool forward = step > 0
                    ? loop.orderedForward.value(cursor, true)
                    : !loop.orderedForward.value(cursor, true);
                const QString startVertex = forward ? edge->firstVertexId : edge->lastVertexId;
                const QVector3D startPoint = forward ? edge->points.first() : edge->points.last();
                if (startVertex != expectedVertex ||
                    (startPoint - expectedPoint).length() > 1.0e-4f) return false;
                appended.append({id, SelectionTopologyKind::Edge, forward});
                expectedVertex = forward ? edge->lastVertexId : edge->firstVertexId;
                expectedPoint = forward ? edge->points.last() : edge->points.first();
                if (cursor == terminalIndex) break;
                cursor = (cursor + step + loop.orderedEdgeIds.size()) %
                         loop.orderedEdgeIds.size();
            }
        }
    }

    if (appended.isEmpty()) {
        if (!loops.isEmpty() || !candidate.boundaryLoops.isEmpty()) return false;
        const bool forward = terminal->firstVertexId == expectedVertex;
        if (!forward && terminal->lastVertexId != expectedVertex) return false;
        const QVector3D startPoint = forward ? terminal->points.first() : terminal->points.last();
        if ((startPoint - expectedPoint).length() > 1.0e-4f) return false;
        appended.append({terminalEdgeId, SelectionTopologyKind::Edge, forward});
        expectedVertex = forward ? terminal->lastVertexId : terminal->firstVertexId;
        expectedPoint = forward ? terminal->points.last() : terminal->points.first();
        loops.clear();
    }

    SelectionChain value = draft;
    value.orderedTopologyRefs += appended;
    for (const SelectionTopologyRef &ref : appended) value.orderedGeometryIds.append(ref.id);
    value.endAnchor = {expectedVertex, expectedPoint, false, 0.0};
    value.closed = value.hasStartAnchor &&
                   expectedVertex == value.startAnchor.topologyId &&
                   (expectedPoint - value.startAnchor.point).length() <= 1.0e-4f;
    value.hasEndAnchor = !value.closed;
    value.boundaryLoops = loops;
    value.outerLoopPointCount = value.closed && !loops.isEmpty()
        ? loops.first().orderedEdgeIds.size() : 0;
    value.freeEndCount = value.closed ? 0 : 2;
    if (!topologyRefsContinuous(candidate, value)) return false;
    *result = value;
    return true;
}

void SelectionChainDialog::previewTopology(const QString &id, int kind,
                                           const QVector3D &point,
                                           double curveParameter)
{
    Q_UNUSED(point);
    Q_UNUSED(curveParameter);
    if (topologyHoverChanged) topologyHoverChanged({});
    if (kind != static_cast<int>(SelectionTopologyKind::Edge) ||
        m_pickingStart || !m_pendingTopologyPick.isEmpty()) return;
    if (id.isEmpty()) {
        if (m_topologyPreviewKind == TopologyPreviewKind::Interval)
            clearTopologyPreview();
        refresh();
        return;
    }
    if (!m_adjustingBoundary) {
        const QList<TopologyMatch> matches = topologyMatches(id, kind);
        for (const TopologyMatch &match : matches) {
            if (m_candidateIndex >= 0 && match.candidateIndex != m_candidateIndex) continue;
            const SelectionCandidate &candidate = m_candidates.at(match.candidateIndex);
            if (const MeshPickEdge *edge = edgeForId(candidate, id)) {
                if (topologyHoverChanged) topologyHoverChanged(edge->points);
                return;
            }
        }
        return;
    }
    SelectionChain draft;
    if (!buildIntervalPreview(id, &draft)) {
        if (m_topologyPreviewKind == TopologyPreviewKind::Interval) {
            clearTopologyPreview();
            refresh();
        }
        return;
    }
    m_topologyPreviewKind = TopologyPreviewKind::Interval;
    m_topologyPreviewDraft = draft;
    m_topologyPreviewTerminalEdgeId = id;
    refresh();
    m_status->setText(tr("区间预览到 %1：虚线仅为候选；单击该边后一次加入。")
                      .arg(id));
}

void SelectionChainDialog::applyPreviewDraft()
{
    if (m_topologyPreviewKind != TopologyPreviewKind::Interval) return;
    remember();
    const SelectionChain draft = m_topologyPreviewDraft;
    clearTopologyPreview();
    applyTopologySelection(draft.orderedTopologyRefs, draft.startAnchor,
                           draft.hasStartAnchor, draft.endAnchor,
                           draft.hasEndAnchor, draft.boundaryLoops, draft.closed);
}

void SelectionChainDialog::navigateBranch(int offset)
{
    SelectionChainController current = m_controller;
    updateTopologyBranches(current.commit());
    if (!m_branchEdgeIds.isEmpty() && m_candidateIndex >= 0) {
        if (offset != 0) {
            m_branchEdgeIndex = (m_branchEdgeIndex + offset) % m_branchEdgeIds.size();
            if (m_branchEdgeIndex < 0) m_branchEdgeIndex += m_branchEdgeIds.size();
        }
        if (m_branchEdgeIndex < 0) m_branchEdgeIndex = 0;
        const MeshPickEdge *edge = edgeForId(m_candidates.at(m_candidateIndex),
                                             m_branchEdgeIds.at(m_branchEdgeIndex));
        SelectionChainController preview = m_controller;
        const SelectionChain draft = preview.commit();
        if (edge && draft.hasEndAnchor &&
            (edge->firstVertexId == draft.endAnchor.topologyId ||
             edge->lastVertexId == draft.endAnchor.topologyId)) {
            const bool atFirst = edge->firstVertexId == draft.endAnchor.topologyId;
            if (offset == 0) {
                clearTopologyPreview();
                selectTopology(edge->id, static_cast<int>(SelectionTopologyKind::Edge),
                               atFirst ? edge->points.first() : edge->points.last(),
                               atFirst ? edge->parameters.value(0, 0.0)
                                       : edge->parameters.value(edge->parameters.size() - 1, 1.0));
            } else {
                QList<SelectionTopologyRef> refs = draft.orderedTopologyRefs;
                refs.append({edge->id, SelectionTopologyKind::Edge, atFirst});
                SelectionChain tentative = draft;
                tentative.orderedTopologyRefs = refs;
                tentative.endAnchor = {
                    atFirst ? edge->lastVertexId : edge->firstVertexId,
                    atFirst ? edge->points.last() : edge->points.first(), false, 0.0};
                tentative.closed = tentative.hasStartAnchor &&
                    tentative.endAnchor.topologyId == tentative.startAnchor.topologyId;
                tentative.hasEndAnchor = !tentative.closed;
                m_topologyPreviewKind = TopologyPreviewKind::Branch;
                m_topologyPreviewDraft = tentative;
                m_topologyPreviewTerminalEdgeId = edge->id;
                refresh();
                m_status->setText(tr("分支预览：%1。点击“确认分支”后加入当前链。")
                                  .arg(edge->id));
            }
        }
        return;
    }
    m_status->setText(tr("当前候选没有可用分支。"));
}

void SelectionChainDialog::refresh()
{
    if (topologyHoverChanged) topologyHoverChanged({});
    SelectionChainController preview = m_controller;
    const SelectionChain draft = preview.commit();
    updateTopologyBranches(draft);
    const QSignalBlocker sideBlock(m_side), modeBlock(m_mode), sortBlock(m_sort), startBlock(m_start),
        sourceBlock(m_source), listBlock(m_list), edgesBlock(m_edges);
    const int selectedRegion = m_regions->currentRow();
    m_regions->clear();
    for (const auto &region : m_regionSelections) {
        QString label = region.candidateGeometryId;
        for (const auto &candidate : m_candidates)
            if (candidate.geometryId == region.candidateGeometryId) label = candidate.label;
        if (acceptanceError) {
            const QString error = acceptanceError(region, region.candidateGeometryId);
            if (!error.isEmpty()) label += tr(" · 需移除或更换工艺：%1").arg(error);
        }
        m_regions->addItem(label);
    }
    m_regions->setCurrentRow(m_regions->count() ? qBound(0, selectedRegion, m_regions->count() - 1) : -1);
    m_regions->setVisible(!m_regionSelections.isEmpty());
    m_removeRegion->setVisible(!m_regionSelections.isEmpty());
    m_removeRegion->setEnabled(m_regionSelections.size() > 1);
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
    const bool topologyEvidence = draft.evidenceVersion >= 2 && !draft.orderedTopologyRefs.isEmpty();
    m_edges->setEnabled(edges || topologyEvidence);
    m_edges->setSelectionMode(topologyEvidence
        ? QAbstractItemView::NoSelection
        : QAbstractItemView::ExtendedSelection);
    if (m_candidateIndex >= 0) {
        const auto &candidate = m_candidates.at(m_candidateIndex);
        if (draft.evidenceVersion >= 2 && !draft.orderedTopologyRefs.isEmpty()) {
            if (draft.selectionMode == ChainSelectionMode::Face && !draft.boundaryLoops.isEmpty()) {
                for (const SelectionBoundaryLoop &loop : draft.boundaryLoops) {
                    new QListWidgetItem(tr("%1 · %2 · %3 条边")
                        .arg(loop.faceId)
                        .arg(loop.outer ? tr("外环") : tr("内环"))
                        .arg(loop.orderedEdgeIds.size()), m_edges);
                }
            } else {
                for (const SelectionTopologyRef &ref : draft.orderedTopologyRefs) {
                    if (ref.kind != SelectionTopologyKind::Edge) continue;
                    auto *item = new QListWidgetItem(
                        tr("%1 · %2").arg(ref.id, ref.forward ? tr("正向") : tr("反向")), m_edges);
                    item->setSelected(true);
                }
            }
            if (draft.selectionMode != ChainSelectionMode::Face) {
                QStringList vertexIds;
                for (const auto &ref : draft.orderedTopologyRefs) {
                    const auto *edge = edgeForId(candidate, ref.id);
                    if (!edge || ref.kind != SelectionTopologyKind::Edge) continue;
                    const QString first = ref.forward ? edge->firstVertexId : edge->lastVertexId;
                    if (draft.closed || vertexIds.isEmpty()) vertexIds.append(first);
                }
                if (!draft.closed && !draft.orderedTopologyRefs.isEmpty()) {
                    const auto &last = draft.orderedTopologyRefs.last();
                    const auto *edge = edgeForId(candidate, last.id);
                    if (edge) vertexIds.append(last.forward ? edge->lastVertexId : edge->firstVertexId);
                }
                vertexIds.removeDuplicates();
                for (const auto &id : vertexIds) {
                    const auto *vertex = vertexForId(candidate, id);
                    if (!vertex) continue;
                    m_start->addItem(tr("顶点 %1 (X%2, Y%3, Z%4)").arg(m_start->count() + 1)
                        .arg(vertex->point.x()).arg(vertex->point.y()).arg(vertex->point.z()), id);
                }
            }
        } else {
            for (int index = 0; index < candidate.points.size(); ++index) {
                m_start->addItem(QString::number(index + 1));
                if (!candidate.closed && index + 1 == candidate.points.size()) continue;
                const auto point = candidate.points.at(index);
                auto *item = new QListWidgetItem(tr("边 %1 · 起点 (%2, %3, %4)").arg(index + 1)
                    .arg(point.x()).arg(point.y()).arg(point.z()), m_edges);
                item->setSelected(draft.orderedGeometryIds.contains(candidate.geometryId + QStringLiteral("/edge:%1").arg(index)));
            }
        }
    }
    m_side->setCurrentIndex(static_cast<int>(draft.machiningSide));
    m_mode->setCurrentIndex(static_cast<int>(draft.selectionMode));
    m_sort->setCurrentIndex(static_cast<int>(draft.sortStrategy));
    if (draft.evidenceVersion >= 2 && draft.hasStartAnchor)
        m_start->setCurrentIndex(m_start->findData(draft.startAnchor.topologyId));
    else if (!draft.hasStartPoint) m_start->setCurrentIndex(-1);
    else if (m_candidateIndex >= 0)
        m_start->setCurrentIndex(m_candidates.at(m_candidateIndex).points.indexOf(draft.startPoint));
    QStringList order;
    for (const auto &id : draft.orderedGeometryIds) {
        const int edge = id.lastIndexOf(QStringLiteral("/edge:"));
        if (id.startsWith(QStringLiteral("edge:")) || id.startsWith(QStringLiteral("vertex:")))
            order.append(id);
        else
            order.append(edge < 0 ? tr("面") : QString::number(id.mid(edge + 6).toInt() + 1));
    }
    m_status->setText(tr("%1 个几何元素 · %2\n顺序：%3\n表面法向 (%4, %5, %6) · %7")
        .arg(m_controller.geometryIds().size()).arg(draft.reversed ? tr("反向") : tr("正向"))
        .arg(order.join(QStringLiteral(" → ")))
        .arg(draft.selectedSurfaceNormal.x()).arg(draft.selectedSurfaceNormal.y())
        .arg(draft.selectedSurfaceNormal.z()).arg(draft.coordinateSystemId));
    const bool fullBoundaryOffer =
        m_topologyPreviewKind == TopologyPreviewKind::FullBoundary;
    const bool hasSelectedRange = m_candidateIndex >= 0 && topologyEvidence;
    const SetupAccessResult access = evaluateSelectionAccess(
        draft, m_context.sourceFingerprint, m_context.setupFingerprint,
        m_context.coordinateSystemId, m_requireClosed);
    const bool continuous = m_candidateIndex < 0 ||
        draft.evidenceVersion < 2 || draft.selectionMode == ChainSelectionMode::Face ||
        topologyRefsContinuous(m_candidates.at(m_candidateIndex), draft);
    const QString strategyError = hasSelectedRange && acceptanceError
        ? acceptanceError(draft, selectedCandidateGeometryId()) : QString();
    const bool rangeConfirmed = hasSelectedRange && !m_adjustingBoundary &&
                                m_topologyPreviewKind == TopologyPreviewKind::None &&
                                access.ok() && continuous && strategyError.isEmpty();
    QString nextStep;
    if (fullBoundaryOffer) {
        nextStep = tr("使用完整边界，或进入调整边界");
    } else if (rangeConfirmed) {
        nextStep = tr("加工参数");
    } else if (hasSelectedRange && !strategyError.isEmpty()) {
        nextStep = tr("检查区域验证结果");
    } else if (hasSelectedRange && (!access.ok() || !continuous)) {
        nextStep = access.issueCode == QStringLiteral("SELECTION_TOPOLOGY_INSUFFICIENT")
            ? tr("检查边界角色与闭合证据") : tr("检查模型、Setup/WCS 与方向");
    } else if (draft.closed) {
        nextStep = tr("完成边界");
    } else if (draft.orderedTopologyRefs.isEmpty()) {
        nextStep = m_adjustingBoundary ? tr("在模型上选择起始边") : tr("先选择具体槽或槽底");
    } else if (m_branchEdgeIds.size() == 1) {
        nextStep = tr("接下一段 %1，或在模型上选择终止边").arg(m_branchEdgeIds.first());
    } else if (m_branchEdgeIds.size() > 1) {
        QStringList branches;
        for (int index = 0; index < m_branchEdgeIds.size(); ++index)
            branches.append(tr("%1: %2").arg(QChar(QLatin1Char('A').unicode() + index))
                            .arg(m_branchEdgeIds.at(index)));
        nextStep = tr("在模型端点选择分支（%1）").arg(branches.join(QStringLiteral(" / ")));
    } else {
        nextStep = tr("选择连续边，或完成边界");
    }
    QString rangeName;
    if (m_candidateIndex >= 0 && m_candidateIndex < m_candidates.size()) {
        const SelectionCandidate &candidate = m_candidates.at(m_candidateIndex);
        rangeName = candidate.label.isEmpty() ? candidate.geometryId : candidate.label;
    }
    m_chainStatus->setText(tr("当前范围：%1\n%2 · 已选 %3 段 · 下一步：%4")
        .arg(rangeName.isEmpty() ? tr("尚未选择") : rangeName)
        .arg(draft.closed ? tr("闭合") : tr("未闭合"))
        .arg(draft.orderedTopologyRefs.size())
        .arg(nextStep));
    for (const auto &loop : draft.boundaryLoops)
        if (loop.candidateRule == "source-section-inset-candidate-v1" &&
            openingEvidenceSupported(loop) && selectionContainsCompleteOpening(draft, loop)) {
            m_chainStatus->setText(m_chainStatus->text() + tr("\n仅主体核心区；原入口其余区域保留，底面需单独验证。预览显示实际加工边界。"));
            break;
        }
    m_list->setVisible(!hasSelectedRange || m_moreDetails->isChecked());
    m_moreDetails->setText(hasSelectedRange
        ? tr("更换范围 / 更多设置") : tr("更多设置与边列表"));
    m_useFullBoundary->setVisible(fullBoundaryOffer);
    m_adjustBoundary->setVisible(m_regionSelections.isEmpty() && (fullBoundaryOffer || rangeConfirmed));
    m_coplanar->setEnabled(!m_edgePickingOnly && rangeConfirmed &&
        draft.selectionMode == ChainSelectionMode::Face && m_candidateIndex >= 0 &&
        m_candidates.at(m_candidateIndex).sourcePlane.verified);
    if (!m_regionSelections.isEmpty())
        m_chainStatus->setText(tr("已选 %1 个同平面独立区域 · 每个区域建立一道工序").arg(m_regionSelections.size()));
    if (!m_regionSelections.isEmpty())
        m_status->setText(tr("%1 个独立区域 · 加工侧统一应用于整组\n%2 · 表面法向 (%3, %4, %5)")
            .arg(m_regionSelections.size()).arg(draft.coordinateSystemId)
            .arg(draft.selectedSurfaceNormal.x()).arg(draft.selectedSurfaceNormal.y())
            .arg(draft.selectedSurfaceNormal.z()));
    m_finishBoundary->setVisible(m_adjustingBoundary);
    m_nextButton->setVisible(!m_adjustingBoundary && !fullBoundaryOffer);
    m_nextButton->setText(rangeConfirmed ? tr("下一步：加工参数") : tr("完成选择"));
    m_nextButton->setEnabled(!resolveBoundary || rangeConfirmed || !m_regionSelections.isEmpty());
    m_nextButton->setToolTip(rangeConfirmed || !hasSelectedRange
        ? QString() : !strategyError.isEmpty() ? strategyError : accessFailureText(draft, access));
    m_finishBoundary->setDefault(m_adjustingBoundary);
    m_nextButton->setDefault(!m_adjustingBoundary && !fullBoundaryOffer);
    m_connectEdges->setVisible(m_adjustingBoundary);
    m_extractSegment->setVisible(m_adjustingBoundary);
    m_acceptNextEdge->setVisible(m_adjustingBoundary && m_branchEdgeIds.size() == 1);
    m_acceptNextEdge->setEnabled(m_adjustingBoundary && m_branchEdgeIds.size() == 1);
    findChild<QPushButton *>(QStringLiteral("selectionPreviousBranch"))->setEnabled(m_branchEdgeIds.size() > 1);
    findChild<QPushButton *>(QStringLiteral("selectionNextBranch"))->setEnabled(m_branchEdgeIds.size() > 1);
    auto *confirmBranch = findChild<QPushButton *>(QStringLiteral("selectionSwitchBranch"));
    confirmBranch->setVisible(!m_adjustingBoundary || m_branchEdgeIds.size() != 1);
    confirmBranch->setEnabled(!m_branchEdgeIds.isEmpty());
    confirmBranch->setText(m_branchEdgeIds.size() == 1 ? tr("接下一段") : tr("确认分支"));
    findChild<QPushButton *>(QStringLiteral("selectionUndo"))->setEnabled(!m_undo.isEmpty());
    findChild<QPushButton *>(QStringLiteral("selectionRedo"))->setEnabled(!m_redo.isEmpty());
    findChild<QPushButton *>(QStringLiteral("selectionReverse"))->setEnabled(
        !draft.orderedGeometryIds.isEmpty() && draft.selectionMode != ChainSelectionMode::Face);
    m_start->setEnabled(m_start->count() > 0 && draft.selectionMode != ChainSelectionMode::Face);
    m_sort->setEnabled(m_regionSelections.isEmpty() && draft.evidenceVersion < 2 &&
                       draft.orderedGeometryIds.size() > 1);
    findChild<QPushButton *>(QStringLiteral("selectionPickStart"))->setEnabled(
        m_regionSelections.isEmpty() && draft.evidenceVersion >= 2 &&
        draft.selectionMode != ChainSelectionMode::Face && m_start->count() > 0);
    const auto previewPoints = m_regionSelections.isEmpty() ? orderedPoints(draft) : QVector<QVector3D>();
    if (previewChanged) previewChanged(previewPoints);
    if (confirmedTopologyPreviewChanged)
        confirmedTopologyPreviewChanged(previewPoints, draft.closed);
    if (facePreviewChanged) {
        QStringList faces;
        for (const SelectionTopologyRef &ref : draft.orderedTopologyRefs)
            if (ref.kind == SelectionTopologyKind::Face) faces.append(ref.id);
        for (const auto &region : m_regionSelections)
            for (const auto &ref : region.orderedTopologyRefs)
                if (ref.kind == SelectionTopologyKind::Face && !faces.contains(ref.id)) faces.append(ref.id);
        facePreviewChanged(faces);
    }
    if (topologyPreviewChanged || topologyHoverChanged) {
        if (m_topologyPreviewKind == TopologyPreviewKind::Interval &&
            m_candidateIndex >= 0) {
            const MeshPickEdge *hovered = edgeForId(
                m_candidates.at(m_candidateIndex), m_topologyPreviewTerminalEdgeId);
            if (topologyPreviewChanged)
                topologyPreviewChanged(orderedPoints(m_topologyPreviewDraft), m_topologyPreviewDraft.closed);
            if (topologyHoverChanged)
                topologyHoverChanged(hovered ? hovered->points : QVector<QVector3D>());
        } else if (topologyPreviewChanged && m_topologyPreviewKind != TopologyPreviewKind::None) {
            topologyPreviewChanged(orderedPoints(m_topologyPreviewDraft),
                                   m_topologyPreviewDraft.closed);
        } else if (topologyPreviewChanged) {
            topologyPreviewChanged(previewPoints, draft.closed);
        }
    }
    if (!m_pendingTopologyPick.isEmpty()) {
        const QSignalBlocker blocker(m_topologyContext);
        m_topologyContext->clear();
        for (const TopologyMatch &match : m_pendingTopologyPick.matches) {
            const SelectionCandidate &candidate = m_candidates.at(match.candidateIndex);
            QString label = candidate.label.isEmpty() ? candidate.geometryId : candidate.label;
            if (match.loopIndex >= 0) {
                const MeshBoundaryLoop &loop = candidate.boundaryLoops.at(match.loopIndex);
                label += tr(" · %1 · %2").arg(
                    loop.faceId, loop.outer ? tr("外环") : tr("内环"));
            }
            m_topologyContext->addItem(label);
        }
        m_topologyContext->setCurrentIndex(m_pendingTopologyPick.matchIndex);
        m_topologyContext->show();
        if (m_topologyContextLabel) m_topologyContextLabel->show();
        m_confirmTopologyContext->show();
    } else {
        m_topologyContext->hide();
        if (m_topologyContextLabel) m_topologyContextLabel->hide();
        m_confirmTopologyContext->hide();
    }
}

void SelectionChainDialog::remember()
{
    m_undo.append(snapshot());
    m_redo.clear();
    m_selection = {};
}

SelectionChainDialog::Snapshot SelectionChainDialog::snapshot() const
{
    Snapshot value;
    value.controller = m_controller;
    value.regionSelections = m_regionSelections;
    value.candidateIndex = m_candidateIndex;
    value.pickingStart = m_pickingStart;
    value.partialStartEdgeId = m_partialStartEdgeId;
    value.partialLoopFaceId = m_partialLoopFaceId;
    value.partialLoopCandidateIndex = m_partialLoopCandidateIndex;
    value.partialLoopIndex = m_partialLoopIndex;
    value.partialStartIndex = m_partialStartIndex;
    value.partialStep = m_partialStep;
    value.partialStartAnchor = m_partialStartAnchor;
    value.branchEdgeIds = m_branchEdgeIds;
    value.branchEdgeIndex = m_branchEdgeIndex;
    value.activeLoopCandidateIndex = m_activeLoopCandidateIndex;
    value.activeLoopIndex = m_activeLoopIndex;
    value.pendingTopologyPick = m_pendingTopologyPick;
    return value;
}

void SelectionChainDialog::restoreSnapshot(const Snapshot &value)
{
    m_controller = value.controller;
    m_regionSelections = value.regionSelections;
    m_candidateIndex = value.candidateIndex;
    m_pickingStart = value.pickingStart;
    m_partialStartEdgeId = value.partialStartEdgeId;
    m_partialLoopFaceId = value.partialLoopFaceId;
    m_partialLoopCandidateIndex = value.partialLoopCandidateIndex;
    m_partialLoopIndex = value.partialLoopIndex;
    m_partialStartIndex = value.partialStartIndex;
    m_partialStep = value.partialStep;
    m_partialStartAnchor = value.partialStartAnchor;
    m_branchEdgeIds = value.branchEdgeIds;
    m_branchEdgeIndex = value.branchEdgeIndex;
    m_activeLoopCandidateIndex = value.activeLoopCandidateIndex;
    m_activeLoopIndex = value.activeLoopIndex;
    m_pendingTopologyPick = value.pendingTopologyPick;
}

void SelectionChainDialog::resetTransientSession()
{
    m_regionSelections.clear();
    if (topologyHoverChanged) topologyHoverChanged({});
    m_pickingStart = false;
    m_partialStartEdgeId.clear();
    m_partialLoopFaceId.clear();
    m_partialLoopCandidateIndex = -1;
    m_partialLoopIndex = -1;
    m_partialStartIndex = -1;
    m_partialStep = 1;
    m_partialStartAnchor = {};
    m_branchEdgeIds.clear();
    m_branchEdgeIndex = -1;
    m_activeLoopCandidateIndex = -1;
    m_activeLoopIndex = -1;
    m_pendingTopologyPick = {};
    m_topologyPreviewKind = TopologyPreviewKind::None;
    m_topologyPreviewDraft = {};
    m_topologyPreviewTerminalEdgeId.clear();
    m_adjustingBoundary = false;
    m_hasAdjustBaseline = false;
    if (m_topologyContext) {
        m_topologyContext->clear();
        m_topologyContext->hide();
    }
    if (m_topologyContextLabel) m_topologyContextLabel->hide();
    if (m_confirmTopologyContext) m_confirmTopologyContext->hide();
}

void SelectionChainDialog::syncPickMode()
{
    if (!pickModeChanged) return;
    if (m_pickingStart) pickModeChanged(2);
    else if (m_edgePickingOnly) pickModeChanged(1);
    else pickModeChanged(m_mode->currentIndex() == static_cast<int>(ChainSelectionMode::Face) ? 0 : 1);
}

void SelectionChainDialog::syncTopologyScope()
{
    if (!topologyScopeChanged) return;
    QStringList edges;
    QStringList vertices;
    QVector<int> faces;
    const int first = m_candidateIndex >= 0 ? m_candidateIndex : 0;
    const int last = m_candidateIndex >= 0 ? m_candidateIndex + 1 : m_candidates.size();
    for (int index = first; index < last; ++index) {
        const SelectionCandidate &candidate = m_candidates.at(index);
        for (const QString &faceId : candidate.faceIds) {
            const int faceIndex = faceId.section(QLatin1Char(':'), 1).toInt();
            if (faceIndex > 0 && !faces.contains(faceIndex)) faces.append(faceIndex);
        }
        for (const MeshPickEdge &edge : candidate.topologyEdges)
            if (!edges.contains(edge.id)) edges.append(edge.id);
        for (const MeshPickVertex &vertex : candidate.topologyVertices)
            if (!vertices.contains(vertex.id)) vertices.append(vertex.id);
    }
    topologyScopeChanged(edges, vertices, faces);
}

void SelectionChainDialog::restoreHistory(bool redo)
{
    auto &from = redo ? m_redo : m_undo;
    auto &to = redo ? m_undo : m_redo;
    if (from.isEmpty()) return;
    to.append(snapshot());
    const auto previous = from.takeLast();
    restoreSnapshot(previous);
    clearTopologyPreview();
    m_selection = {};
    refresh();
    syncPickMode();
    syncTopologyScope();
}

void SelectionChainDialog::accept()
{
    if (!m_pendingTopologyPick.isEmpty()) {
        m_status->setText(tr("请先确认当前拓扑所属的候选、面和边界环。"));
        return;
    }
    if (!m_regionSelections.isEmpty()) {
        for (const auto &region : m_regionSelections)
            if (validatePreviousSelection(region) < 0) return;
        for (auto &region : m_regionSelections)
            region = SelectionChainController(region).commit();
        m_selection = m_regionSelections.first();
        QDialog::accept();
        return;
    }
    if (!m_controller.finishChain()) { m_status->setText(tr("请先选择几何候选。")); return; }
    SelectionChainController preview = m_controller;
    const auto draft = preview.commit();
    if (m_candidateIndex >= 0 &&
        ((draft.evidenceVersion >= 2 && draft.selectionMode != ChainSelectionMode::Face) ||
         !m_candidates.at(m_candidateIndex).points.isEmpty()) &&
        orderedPoints(draft).isEmpty()) {
        m_status->setText(tr("所选边不连续或起点不在边界上。请撤销排序，或重新选择连续边和有效起点。"));
        return;
    }
    const auto access = evaluateSelectionAccess(draft, m_context.sourceFingerprint,
        m_context.setupFingerprint, m_context.coordinateSystemId, m_requireClosed);
    if (!access.ok()) {
        m_status->setText(accessFailureText(draft, access));
        return;
    }
    if (acceptanceError) {
        const QString error = acceptanceError(draft, selectedCandidateGeometryId());
        if (!error.isEmpty()) { m_status->setText(error); return; }
    }
    m_selection = m_controller.commit();
    m_selection.candidateGeometryId = selectedCandidateGeometryId();
    QDialog::accept();
}

void SelectionChainDialog::setPreviousSelection(const SelectionChain &selection)
{
    m_previousSelections = selection.explicitUserSelection ? QList<SelectionChain>{selection} : QList<SelectionChain>();
    m_lastSelection->setEnabled(selection.explicitUserSelection &&
                                !selection.orderedGeometryIds.isEmpty());
}

void SelectionChainDialog::setPreviousSelections(const QList<SelectionChain> &selections)
{
    setPreviousSelection(selections.isEmpty() ? SelectionChain() : selections.first());
    m_previousSelections = selections;
}

int SelectionChainDialog::validatePreviousSelection(const SelectionChain &previous, bool checkStrategy)
{
    if (!previous.explicitUserSelection || previous.orderedGeometryIds.isEmpty()) return -1;
    if (m_context.sourceFingerprint.isEmpty() || m_context.setupFingerprint.isEmpty() ||
        m_context.coordinateSystemId.isEmpty() || previous.toolAxis != m_context.toolAxis) {
        m_status->setText(tr("当前模型、装夹或刀轴信息不完整或已改变，请重新选择。"));
        return -1;
    }
    const auto access = evaluateSelectionAccess(previous, m_context.sourceFingerprint,
        m_context.setupFingerprint, m_context.coordinateSystemId, m_requireClosed);
    if (!access.ok()) { m_status->setText(accessFailureText(previous, access)); return -1; }
    if (m_edgePickingOnly && previous.selectionMode == ChainSelectionMode::Face) {
        m_status->setText(tr("当前工序需要边界选择，不能恢复面选择。"));
        return -1;
    }
    int candidateIndex = -1;
    for (int i = 0; i < m_candidates.size(); ++i) {
        const auto &candidate = m_candidates.at(i);
        if (candidate.geometryId != previous.candidateGeometryId ||
            candidate.source != previous.geometrySource) continue;
        if (candidateIndex >= 0) {
            m_status->setText(tr("上次选择对应多个区域，请重新选择。"));
            return -1;
        }
        candidateIndex = i;
    }
    if (candidateIndex < 0) {
        m_status->setText(tr("上次选择缺少可追溯区域或区域已不存在，请重新选择。"));
        return -1;
    }
    resolveSelectedBoundary(candidateIndex, previous);
    const auto &candidate = m_candidates.at(candidateIndex);
    bool valid = candidate.surfaceNormal == previous.selectedSurfaceNormal;
    auto finitePoint = [](const QVector3D &point) {
        return std::isfinite(point.x()) && std::isfinite(point.y()) && std::isfinite(point.z());
    };
    if (previous.hasStartPoint) valid = valid && finitePoint(previous.startPoint);
    for (const auto &anchor : {previous.startAnchor, previous.endAnchor})
        valid = valid && finitePoint(anchor.point) && std::isfinite(anchor.curveParameter);
    if (previous.evidenceVersion >= 2) {
        valid = valid && !previous.orderedTopologyRefs.isEmpty() &&
                topologyRefsContinuous(candidate, previous);
        for (const auto &ref : previous.orderedTopologyRefs)
            if (previous.selectionMode == ChainSelectionMode::Face)
                valid = valid && ref.kind == SelectionTopologyKind::Face && candidate.faceIds.contains(ref.id);
        for (const auto &loop : previous.boundaryLoops) {
            bool found = false;
            for (const auto &current : candidate.boundaryLoops) {
                SelectionBoundaryLoop resolved;
                resolved.faceId = current.faceId;
                resolved.outer = current.outer;
                resolved.orderedEdgeIds = current.orderedEdgeIds;
                resolved.orderedForward = current.orderedForward;
                if (machiningLoop(candidate, resolved) == loop) { found = true; break; }
            }
            valid = valid && found;
        }
    }
    const int oldIndex = m_candidateIndex;
    m_candidateIndex = candidateIndex;
    valid = valid && !orderedPoints(previous).isEmpty();
    m_candidateIndex = oldIndex;
    if (!valid) {
        m_status->setText(tr("上次选择的边、方向、锚点或边界证据已失效，请重新选择。"));
        return -1;
    }
    if (checkStrategy && acceptanceError) {
        const QString error = acceptanceError(previous, candidate.geometryId);
        if (!error.isEmpty()) { m_status->setText(error); return -1; }
    }
    return candidateIndex;
}

void SelectionChainDialog::restorePreviousSelection()
{
    if (m_previousSelections.isEmpty()) return;
    if (m_adjustingBoundary || m_hasAdjustBaseline || !m_pendingTopologyPick.isEmpty() ||
        m_topologyPreviewKind != TopologyPreviewKind::None) {
        m_status->setText(tr("请先完成或取消当前边界调整、预览或分支选择，再选择上次。"));
        return;
    }
    QList<int> indices;
    QSet<QString> faces;
    for (const auto &previous : m_previousSelections) {
        const int index = validatePreviousSelection(previous, m_previousSelections.size() == 1);
        if (index < 0) return;
        if (m_previousSelections.size() > 1) {
            const auto &candidate = m_candidates.at(index);
            if (previous.selectionMode != ChainSelectionMode::Face || candidate.faceIds.size() != 1 ||
                faces.contains(candidate.faceIds.first()) ||
                !selectionPlanesCoplanar(candidate.sourcePlane,
                    m_candidates.at(indices.isEmpty() ? index : indices.first()).sourcePlane)) {
                m_status->setText(tr("上次整组选区的平面或区域证据已失效，请重新选择。"));
                return;
            }
            faces.insert(candidate.faceIds.first());
        }
        indices.append(index);
    }
    remember();
    resetTransientSession();
    m_candidateIndex = indices.first();
    m_candidateContext = m_context;
    m_controller = SelectionChainController(m_previousSelections.first());
    if (m_previousSelections.size() > 1) m_regionSelections = m_previousSelections;
    refresh();
    syncPickMode();
    syncTopologyScope();
    m_status->setText(tr("已恢复上次工序的完整选区，请核对并完成选择。"));
}

QList<SelectionChain> SelectionChainDialog::selections() const
{
    if (!m_selection.explicitUserSelection) return {};
    return m_regionSelections.isEmpty() ? QList<SelectionChain>{m_selection} : m_regionSelections;
}

QVector<QVector3D> SelectionChainDialog::selectedPoints(const SelectionChain &selection)
{
    const int oldIndex = m_candidateIndex;
    QVector<QVector3D> points;
    for (int i = 0; i < m_candidates.size(); ++i) {
        if (m_candidates.at(i).geometryId != selection.candidateGeometryId) continue;
        m_candidateIndex = i;
        points = orderedPoints(selection);
        break;
    }
    m_candidateIndex = oldIndex;
    return points;
}

void SelectionChainDialog::selectCoplanarRegions()
{
    if (!m_coplanar->isEnabled() || m_candidateIndex < 0) return;
    const auto seed = m_candidates.at(m_candidateIndex);
    SelectionChainController preview = m_controller;
    const auto current = preview.commit();
    QList<SelectionChain> regions;
    QSet<QString> faces;
    for (const auto &candidate : m_candidates) {
        if (candidate.source != seed.source || candidate.faceIds.size() != 1 ||
            !selectionPlanesCoplanar(seed.sourcePlane, candidate.sourcePlane) ||
            faces.contains(candidate.faceIds.first())) continue;
        SelectionChain region = m_context;
        region.candidateGeometryId = candidate.geometryId;
        region.geometrySource = candidate.source;
        region.selectionMode = ChainSelectionMode::Face;
        region.machiningSide = current.machiningSide;
        region.selectedSurfaceNormal = candidate.surfaceNormal;
        region.evidenceVersion = 2;
        region.explicitUserSelection = true;
        region.closed = true;
        region.orderedGeometryIds = candidate.faceIds;
        region.orderedTopologyRefs = {{candidate.faceIds.first(), SelectionTopologyKind::Face, true}};
        for (const auto &loop : candidate.boundaryLoops) {
            if (loop.faceId != candidate.faceIds.first()) continue;
            region.boundaryLoops.append(machiningLoop(candidate,
                {loop.faceId, loop.outer, loop.orderedEdgeIds, loop.orderedForward}));
            if (loop.outer) region.outerLoopPointCount += loop.orderedEdgeIds.size();
            else ++region.islandCount;
        }
        SelectionChainController controller(region);
        region = controller.commit();
        region.candidateGeometryId = candidate.geometryId;
        if (validatePreviousSelection(region, false) < 0) return;
        regions.append(region);
        faces.insert(candidate.faceIds.first());
    }
    if (regions.size() < 2) {
        m_status->setText(tr("没有找到其他可用于当前工序的同平面区域。"));
        return;
    }
    remember();
    m_regionSelections = regions;
    m_controller = SelectionChainController(regions.first());
    for (int i = 0; i < m_candidates.size(); ++i)
        if (m_candidates.at(i).geometryId == regions.first().candidateGeometryId) m_candidateIndex = i;
    refresh();
    syncTopologyScope();
}

QString SelectionChainDialog::accessFailureText(
    const SelectionChain &draft, const SetupAccessResult &access) const
{
    if (access.ok()) return QString();
    if (access.issueCode != QStringLiteral("SELECTION_TOPOLOGY_INSUFFICIENT")) {
        return tr("选择无法确认，请检查模型、Setup/WCS 与加工方向：%1")
            .arg(access.issueCode);
    }

    if(draft.closed && draft.freeEndCount==0 && m_candidateIndex>=0){
        const auto &errors=m_candidates[m_candidateIndex].regionErrors;
        for(const auto &loop:draft.boundaryLoops){const auto reason=errors.value(loop.faceId+"/"+loop.orderedEdgeIds.join(','));if(!reason.isEmpty())return tr("选择已闭合；区域未通过验证：%1。请核对源曲线、支撑几何和当前 Setup。").arg(reason);}
    }
    QStringList faces;
    QStringList roles;
    bool hasOuter = false;
    bool hasInner = false;
    for (const SelectionBoundaryLoop &loop : draft.boundaryLoops) {
        if (!loop.faceId.isEmpty() && !faces.contains(loop.faceId)) faces.append(loop.faceId);
        if (loop.outer || loop.machiningRole==MachiningBoundaryRole::Outer) hasOuter = true;
        else hasInner = true;
        roles.append(tr("%1:%2(%3段)")
            .arg(loop.faceId.isEmpty() ? tr("未知面") : loop.faceId,
                 loop.outer ? tr("外环") : tr("内环"))
            .arg(loop.orderedEdgeIds.size()));
    }

    QString reason;
    if (!draft.closed || draft.freeEndCount != 0) {
        reason = tr("所选边链尚未形成无自由端的闭合边界。");
    } else if (draft.boundaryLoops.isEmpty()) {
        reason = tr("闭链没有所属面和源 BRep 边界环证据，不能伪造为加工外边界。");
    } else if (!hasOuter && hasInner) {
        reason = tr("所选闭链是源 BRep 内环，尚未取得可加工槽口证据；需确认完整平面边界、向内开放的邻壁、共同底面及无岛屿的公共深度，不能仅凭闭合放行。");
    } else if (!hasOuter) {
        reason = tr("没有找到与已选边一一对应的源 BRep 外环。");
    } else {
        reason = tr("闭合边界的拓扑证据不完整。");
    }
    return tr("%1 所属面：%2；环：%3；closed=%4，outerLoopPointCount=%5，freeEndCount=%6。")
        .arg(reason,
             faces.isEmpty() ? tr("无") : faces.join(QStringLiteral(", ")),
             roles.isEmpty() ? tr("无") : roles.join(QStringLiteral(", ")),
             draft.closed ? tr("true") : tr("false"))
        .arg(draft.outerLoopPointCount)
        .arg(draft.freeEndCount);
}

bool SelectionChainDialog::topologyRefsContinuous(
    const SelectionCandidate &candidate, const SelectionChain &draft,
    QString *reason) const
{
    if (draft.selectionMode == ChainSelectionMode::Face) return true;
    if (draft.orderedTopologyRefs.isEmpty()) {
        if (reason) *reason = tr("没有拓扑边引用。");
        return false;
    }
    QString firstVertex;
    QString previousEnd;
    QVector3D firstPoint;
    QVector3D previousEndPoint;
    for (int index = 0; index < draft.orderedTopologyRefs.size(); ++index) {
        const SelectionTopologyRef &ref = draft.orderedTopologyRefs.at(index);
        if (ref.kind != SelectionTopologyKind::Edge) {
            if (reason) *reason = tr("边链中包含非边拓扑：%1").arg(ref.id);
            return false;
        }
        const MeshPickEdge *edge = edgeForId(candidate, ref.id);
        if (!edge || edge->points.isEmpty() || edge->firstVertexId.isEmpty() ||
            edge->lastVertexId.isEmpty()) {
            if (reason) *reason = tr("无法解析拓扑边：%1").arg(ref.id);
            return false;
        }
        const QString startVertex = ref.forward ? edge->firstVertexId : edge->lastVertexId;
        const QString endVertex = ref.forward ? edge->lastVertexId : edge->firstVertexId;
        const QVector3D startPoint = ref.forward ? edge->points.first() : edge->points.last();
        const QVector3D endPoint = ref.forward ? edge->points.last() : edge->points.first();
        if (index == 0) {
            firstVertex = startVertex;
            firstPoint = startPoint;
        } else if (previousEnd != startVertex ||
                   (previousEndPoint - startPoint).length() > 1.0e-4f) {
            if (reason) {
                *reason = tr("%1 的终点 %2 与 %3 的起点 %4 不连续。")
                    .arg(draft.orderedTopologyRefs.at(index - 1).id, previousEnd,
                         ref.id, startVertex);
            }
            return false;
        }
        previousEnd = endVertex;
        previousEndPoint = endPoint;
    }
    if (!draft.hasStartAnchor || draft.startAnchor.topologyId != firstVertex ||
        (draft.startAnchor.point - firstPoint).length() > 1.0e-4f) {
        if (reason) *reason = tr("起点锚点与首条有向边不一致。");
        return false;
    }
    if (draft.closed) {
        if (previousEnd != firstVertex ||
            (previousEndPoint - firstPoint).length() > 1.0e-4f) {
            if (reason) *reason = tr("闭环末端没有回到起点。");
            return false;
        }
    } else if (!draft.hasEndAnchor || draft.endAnchor.topologyId != previousEnd ||
               (draft.endAnchor.point - previousEndPoint).length() > 1.0e-4f) {
        if (reason) *reason = tr("终点锚点与末条有向边不一致。");
        return false;
    }
    if (!draft.boundaryLoops.isEmpty()) {
        bool belongsToConfirmedLoop = false;
        for (const SelectionBoundaryLoop &loop : draft.boundaryLoops) {
            bool containsAll = true;
            for (const SelectionTopologyRef &ref : draft.orderedTopologyRefs)
                if (!loop.orderedEdgeIds.contains(ref.id)) { containsAll = false; break; }
            if (containsAll) { belongsToConfirmedLoop = true; break; }
        }
        if (!belongsToConfirmedLoop) {
            if (reason) *reason = tr("边链跨出了已确认的面/环范围。");
            return false;
        }
    }
    return true;
}

QVector<QVector3D> SelectionChainDialog::orderedPoints(const SelectionChain &draft) const
{
    if (m_candidateIndex < 0) return {};
    const auto &candidate = m_candidates.at(m_candidateIndex);
    if (draft.evidenceVersion >= 2 && !draft.orderedTopologyRefs.isEmpty()) {
        for (const auto &loop : draft.boundaryLoops)
            if (loop.machiningRole == MachiningBoundaryRole::Outer &&
                openingEvidenceSupported(loop) && selectionContainsCompleteOpening(draft, loop))
                return loop.machiningBoundary;
        if (draft.selectionMode == ChainSelectionMode::Face) {
            auto machiningOrder = [&draft](QVector<QVector3D> points) {
                if (points.isEmpty()) return points;
                int start = 0;
                if (draft.hasStartPoint) {
                    if (points.count(draft.startPoint) != 1) return QVector<QVector3D>();
                    start = points.indexOf(draft.startPoint);
                }
                QVector<QVector3D> ordered;
                for (int i = 0; i < points.size(); ++i)
                    ordered.append(points.at((start + (draft.reversed ? -i : i) + points.size()) % points.size()));
                return ordered;
            };
            int outerCount = 0;
            for (const auto &loop : draft.boundaryLoops) outerCount += loop.outer;
            if (outerCount != 1) return {};
            if (!candidate.topologyOnly) return machiningOrder(candidate.points);
            QVector<QVector3D> points;
            for (const SelectionBoundaryLoop &loop : draft.boundaryLoops) {
                if (!loop.outer) continue;
                for (int i = 0; i < loop.orderedEdgeIds.size(); ++i) {
                    const MeshPickEdge *edge = edgeForId(candidate, loop.orderedEdgeIds.at(i));
                    if (!edge) return {};
                    QVector<QVector3D> samples = edge->points;
                    if (!loop.orderedForward.value(i, true)) std::reverse(samples.begin(), samples.end());
                    if (samples.isEmpty() || (!points.isEmpty() &&
                        (points.last() - samples.first()).length() > 1.0e-4f)) return {};
                    for (const QVector3D &point : samples)
                        if (points.isEmpty() || (points.last() - point).length() > 1.0e-5f)
                            points.append(point);
                }
                if (points.size() < 2 || (points.first() - points.last()).length() > 1.0e-4f)
                    return {};
                points.removeLast();
                return machiningOrder(points);
            }
            return points;
        }
        if (!topologyRefsContinuous(candidate, draft)) return {};
        QVector<QVector3D> points;
        for (const SelectionTopologyRef &ref : draft.orderedTopologyRefs) {
            if (ref.kind != SelectionTopologyKind::Edge) continue;
            const MeshPickEdge *edge = edgeForId(candidate, ref.id);
            if (!edge || edge->points.isEmpty()) return {};
            if (ref.forward) {
                for (const QVector3D &point : edge->points)
                    if (points.isEmpty() || points.last() != point) points.append(point);
            } else {
                for (auto it = edge->points.crbegin(); it != edge->points.crend(); ++it)
                    if (points.isEmpty() || points.last() != *it) points.append(*it);
            }
        }
        if (draft.closed && points.size() > 1 && points.first() == points.last())
            points.removeLast();
        return points;
    }
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
    resetTransientSession();
    syncPickMode();
    QDialog::reject();
}

bool SelectionChainDialog::handleSessionKey(QKeyEvent *event)
{
    QWidget *focused = QApplication::focusWidget();
    if (qobject_cast<QLineEdit *>(focused) || qobject_cast<QTextEdit *>(focused) ||
        qobject_cast<QPlainTextEdit *>(focused)) return false;
    if (event->matches(QKeySequence::Undo)) {
        restoreHistory(false);
        event->accept();
        return true;
    }
    if (event->matches(QKeySequence::Redo)) {
        restoreHistory(true);
        event->accept();
        return true;
    }
    if (event->key() == Qt::Key_Escape) {
        if (!m_pendingTopologyPick.isEmpty()) {
            m_pendingTopologyPick = {};
            refresh();
            m_status->setText(tr("已取消候选范围预览，当前边界未改变。"));
        } else if (m_topologyPreviewKind != TopologyPreviewKind::None) {
            clearTopologyPreview();
            refresh();
            m_status->setText(tr("已取消当前预览，正式边界未改变。"));
        } else if (m_adjustingBoundary && m_hasAdjustBaseline) {
            restoreSnapshot(m_adjustBaseline);
            m_adjustingBoundary = false;
            m_hasAdjustBaseline = false;
            prepareFullBoundaryOffer();
            m_status->setText(tr("已退出边界调整，并恢复进入调整前的范围。"));
            syncPickMode();
        } else {
            reject();
        }
        event->accept();
        return true;
    }
    return false;
}

bool SelectionChainDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (isVisible() && event->type() == QEvent::KeyPress &&
        handleSessionKey(static_cast<QKeyEvent *>(event))) return true;
    return QDialog::eventFilter(watched, event);
}

void SelectionChainDialog::keyPressEvent(QKeyEvent *event)
{
    if (handleSessionKey(event)) return;
    QDialog::keyPressEvent(event);
}

void SelectionChainDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!isWindow()) return;
    if (!parentWidget()) return;
    const QPoint parentTopLeft = parentWidget()->mapToGlobal(QPoint(0, 0));
    QPoint target(parentTopLeft.x() + parentWidget()->width() - width() - 16,
                  parentTopLeft.y() + 82);
    if (QScreen *screen = parentWidget()->screen()) {
        const QRect available = screen->availableGeometry();
        target.setX(qBound(available.left(), target.x(), available.right() - width()));
        target.setY(qBound(available.top(), target.y(), available.bottom() - height()));
    }
    move(target);
}

void SelectionChainDialog::selectGeometry(const QString &id)
{
    for (int row = 0; row < m_list->count(); ++row) {
        auto *item = m_list->item(row);
        const int index = item->data(Qt::UserRole).toInt();
        if (index >= 0 && index < m_candidates.size() && m_candidates[index].geometryId == id) {
            m_list->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
            return;
        }
    }
}

QString SelectionChainDialog::selectedCandidateGeometryId() const
{
    return m_candidateIndex >= 0 && m_candidateIndex < m_candidates.size()
        ? m_candidates.at(m_candidateIndex).geometryId : QString();
}

bool SelectionChainDialog::hasCandidatePreview() const
{
    return m_topologyPreviewKind != TopologyPreviewKind::None;
}

QList<SelectionChainDialog::TopologyMatch> SelectionChainDialog::topologyMatches(
    const QString &id, int kind) const
{
    QList<TopologyMatch> matches;
    for (int index = 0; index < m_candidates.size(); ++index) {
        const SelectionCandidate &candidate = m_candidates.at(index);
        if (kind == static_cast<int>(SelectionTopologyKind::Face) && candidate.faceIds.contains(id)) {
            matches.append({index, -1});
        }
        if (kind == static_cast<int>(SelectionTopologyKind::Edge) && edgeForId(candidate, id)) {
            bool foundLoop = false;
            for (int loopIndex = 0; loopIndex < candidate.boundaryLoops.size(); ++loopIndex) {
                if (candidate.boundaryLoops.at(loopIndex).orderedEdgeIds.contains(id)) {
                    matches.append({index, loopIndex});
                    foundLoop = true;
                }
            }
            if (!foundLoop) matches.append({index, -1});
        }
        if (kind == static_cast<int>(SelectionTopologyKind::Vertex)) {
            for (const MeshPickVertex &vertex : candidate.topologyVertices)
                if (vertex.id == id) { matches.append({index, -1}); break; }
        }
    }
    return matches;
}

void SelectionChainDialog::setPendingTopologyPick(
    const QString &id, int kind, const QVector3D &point, double curveParameter,
    const QList<TopologyMatch> &matches)
{
    m_pendingTopologyPick = {id, kind, point, curveParameter, matches, 0};
    const QSignalBlocker blocker(m_topologyContext);
    m_topologyContext->clear();
    for (const TopologyMatch &match : matches) {
        const SelectionCandidate &candidate = m_candidates.at(match.candidateIndex);
        QString label = candidate.label.isEmpty() ? candidate.geometryId : candidate.label;
        if (match.loopIndex >= 0) {
            const MeshBoundaryLoop &loop = candidate.boundaryLoops.at(match.loopIndex);
            label += tr(" · %1 · %2").arg(loop.faceId, loop.outer ? tr("外环") : tr("内环"));
        }
        m_topologyContext->addItem(label);
    }
    m_topologyContext->setCurrentIndex(0);
    m_topologyContext->show();
    if (m_topologyContextLabel) m_topologyContextLabel->show();
    m_confirmTopologyContext->show();
    previewPendingTopologyMatch();
}

void SelectionChainDialog::previewPendingTopologyMatch()
{
    if (m_pendingTopologyPick.isEmpty() ||
        m_pendingTopologyPick.matchIndex < 0 ||
        m_pendingTopologyPick.matchIndex >= m_pendingTopologyPick.matches.size()) return;
    const TopologyMatch match =
        m_pendingTopologyPick.matches.at(m_pendingTopologyPick.matchIndex);
    const SelectionCandidate &candidate = m_candidates.at(match.candidateIndex);
    QVector<QVector3D> points;
    bool closed = false;
    if (m_pendingTopologyPick.kind == static_cast<int>(SelectionTopologyKind::Edge)) {
        if (const MeshPickEdge *edge = edgeForId(candidate, m_pendingTopologyPick.id))
            points = edge->points;
    } else if (match.loopIndex >= 0) {
        const MeshBoundaryLoop &loop = candidate.boundaryLoops.at(match.loopIndex);
        for (int i = 0; i < loop.orderedEdgeIds.size(); ++i) {
            const MeshPickEdge *edge = edgeForId(candidate, loop.orderedEdgeIds.at(i));
            if (!edge) { points.clear(); break; }
            const bool forward = loop.orderedForward.value(i, true);
            if (forward) {
                for (const QVector3D &point : edge->points)
                    if (points.isEmpty() || points.last() != point) points.append(point);
            } else {
                for (auto it = edge->points.crbegin(); it != edge->points.crend(); ++it)
                    if (points.isEmpty() || points.last() != *it) points.append(*it);
            }
        }
        closed = true;
    } else if (const MeshPickEdge *edge = edgeForId(candidate, m_pendingTopologyPick.id)) {
        points = edge->points;
    }
    if (topologyPreviewChanged) topologyPreviewChanged(points, closed);
    m_status->setText(tr("该拓扑属于多个加工范围。请预览并确认：%1")
                      .arg(m_topologyContext->currentText()));
}

void SelectionChainDialog::confirmPendingTopologyMatch()
{
    if (m_pendingTopologyPick.isEmpty() ||
        m_pendingTopologyPick.matchIndex < 0 ||
        m_pendingTopologyPick.matchIndex >= m_pendingTopologyPick.matches.size()) return;
    const PendingTopologyPick pending = m_pendingTopologyPick;
    const TopologyMatch match = pending.matches.at(pending.matchIndex);
    if (candidateChangeWouldDiscardDraft(match.candidateIndex, pending.kind)) {
        m_status->setText(pending.kind == static_cast<int>(SelectionTopologyKind::Vertex)
            ? tr("所选顶点不属于当前链，起点未改变。")
            : tr("所选边属于另一候选且与当前选择会话不连续，草稿未改变。"));
        return;
    }
    m_pendingTopologyPick = {};
    m_topologyContext->clear();
    m_topologyContext->hide();
    if (m_topologyContextLabel) m_topologyContextLabel->hide();
    m_confirmTopologyContext->hide();
    selectTopologyMatch(pending.id, pending.kind, pending.point,
                        pending.curveParameter, match);
}

bool SelectionChainDialog::candidateChangeWouldDiscardDraft(int candidateIndex, int kind) const
{
    if (m_candidateIndex < 0 || m_candidateIndex == candidateIndex) return false;
    SelectionChainController preview = m_controller;
    const SelectionChain draft = preview.commit();
    if (draft.evidenceVersion < 2 || draft.orderedTopologyRefs.isEmpty()) return false;
    if (kind == static_cast<int>(SelectionTopologyKind::Vertex)) return true;
    return kind == static_cast<int>(SelectionTopologyKind::Edge) &&
           m_controller.state() != SelectionChainState::Ordered;
}

const MeshPickEdge *SelectionChainDialog::edgeForId(const SelectionCandidate &candidate,
                                                     const QString &id) const
{
    for (const MeshPickEdge &edge : candidate.topologyEdges)
        if (edge.id == id) return &edge;
    return nullptr;
}

const MeshPickVertex *SelectionChainDialog::vertexForId(
    const SelectionCandidate &candidate, const QString &id) const
{
    for (const MeshPickVertex &vertex : candidate.topologyVertices)
        if (vertex.id == id) return &vertex;
    return nullptr;
}

void SelectionChainDialog::resolveSelectedBoundary(int candidateIndex, const SelectionChain &selection)
{
    if (!resolveBoundary || candidateIndex < 0 || candidateIndex >= m_candidates.size() ||
        !selection.closed || selection.boundaryLoops.size() != 1) return;
    const auto &selected = selection.boundaryLoops.first();
    if (selected.orderedEdgeIds.isEmpty() ||
        selection.orderedTopologyRefs.size() != selected.orderedEdgeIds.size()) return;
    QSet<QString> ids;
    for (const auto &ref : selection.orderedTopologyRefs) {
        if (ref.kind != SelectionTopologyKind::Edge || !selected.orderedEdgeIds.contains(ref.id)) return;
        ids.insert(ref.id);
    }
    if (ids.size() != selected.orderedEdgeIds.size()) return;
    auto &candidate = m_candidates[candidateIndex];
    for (const auto &source : candidate.boundaryLoops) {
        if (source.faceId == selected.faceId && source.outer == selected.outer &&
            source.orderedEdgeIds == selected.orderedEdgeIds && source.orderedForward == selected.orderedForward) {
            m_status->setText(tr("正在验证所选边界，请稍候…"));
            m_status->repaint();
            resolveBoundary(candidate, source);
            return;
        }
    }
}

void SelectionChainDialog::applyTopologySelection(
    const QList<SelectionTopologyRef> &refs,
    const SelectionAnchor &startAnchor,
    bool hasStartAnchor,
    const SelectionAnchor &endAnchor,
    bool hasEndAnchor,
    const QList<SelectionBoundaryLoop> &loops,
    bool closed)
{
    clearTopologyPreview();
    const QStringList existing = m_controller.geometryIds();
    for (const QString &id : existing) m_controller.removeGeometry(id);
    for (const SelectionTopologyRef &ref : refs) {
        QVector3D referencePoint;
        if (m_candidateIndex >= 0) {
            const SelectionCandidate &candidate = m_candidates.at(m_candidateIndex);
            if (const MeshPickEdge *edge = edgeForId(candidate, ref.id))
                referencePoint = edge->points.value(0);
            else
                referencePoint = candidate.referencePoint;
        }
        m_controller.addGeometry(ref.id, referencePoint);
    }
    auto selectedLoops = refs.isEmpty() ? QList<SelectionBoundaryLoop>() : loops;
    SelectionChain requested;
    requested.orderedTopologyRefs = refs;
    requested.boundaryLoops = selectedLoops;
    requested.closed = closed;
    resolveSelectedBoundary(m_candidateIndex, requested);
    if (m_candidateIndex >= 0 && !refs.isEmpty() && refs.first().kind == SelectionTopologyKind::Edge)
        for (auto &loop : selectedLoops) loop = machiningLoop(m_candidates[m_candidateIndex],loop);
    m_controller.setTopologySelection(refs, startAnchor, hasStartAnchor,
                                      endAnchor, hasEndAnchor, selectedLoops);
    m_controller.setClosed(closed);
    int outerCount = 0;
    int islandCount = 0;
    for (const SelectionBoundaryLoop &loop : selectedLoops) {
        if (loop.outer || (loop.machiningRole == MachiningBoundaryRole::Outer)) outerCount = qMax(outerCount, loop.orderedEdgeIds.size());
        else if (loop.machiningRole == MachiningBoundaryRole::Island) ++islandCount;
    }
    m_controller.setTopologyEvidence(closed ? outerCount : 0, islandCount,
                                     closed ? 0 : (refs.isEmpty() ? 0 : 2));
    if (hasStartAnchor) m_controller.setStartPoint(startAnchor.point);
    refresh();
}

void SelectionChainDialog::updateTopologyBranches(const SelectionChain &draft)
{
    const QString previous = m_branchEdgeIds.value(m_branchEdgeIndex);
    m_branchEdgeIds.clear();
    m_branchEdgeIndex = -1;
    if (m_candidateIndex >= 0 && m_controller.state() != SelectionChainState::Ordered &&
        !draft.closed && draft.hasEndAnchor &&
        draft.selectionMode != ChainSelectionMode::Face) {
        const SelectionCandidate &candidate = m_candidates.at(m_candidateIndex);
        QSet<QString> selected;
        for (const SelectionTopologyRef &ref : draft.orderedTopologyRefs) selected.insert(ref.id);
        for (const MeshPickVertex &vertex : candidate.topologyVertices) {
            if (vertex.id != draft.endAnchor.topologyId) continue;
            for (const QString &edgeId : vertex.incidentEdgeIds) {
                const MeshPickEdge *edge = edgeForId(candidate, edgeId);
                if (!edge || selected.contains(edgeId) || edge->points.size() < 2 ||
                    (edge->firstVertexId != vertex.id && edge->lastVertexId != vertex.id)) continue;
                bool inScope = draft.boundaryLoops.isEmpty() && candidate.boundaryLoops.isEmpty();
                for (const SelectionBoundaryLoop &loop : draft.boundaryLoops) {
                    bool containsAll = loop.orderedEdgeIds.contains(edgeId);
                    for (const SelectionTopologyRef &ref : draft.orderedTopologyRefs)
                        containsAll = containsAll && loop.orderedEdgeIds.contains(ref.id);
                    inScope = inScope || containsAll;
                }
                if (inScope && !m_branchEdgeIds.contains(edgeId)) m_branchEdgeIds.append(edgeId);
            }
            break;
        }
    }
    m_branchEdgeIndex = m_branchEdgeIds.indexOf(previous);
    if (m_branchEdgeIndex < 0 && !m_branchEdgeIds.isEmpty()) m_branchEdgeIndex = 0;
    m_controller.setBranchChoices(m_branchEdgeIds, qMax(0, m_branchEdgeIndex));
    if (m_topologyPreviewKind == TopologyPreviewKind::Branch &&
        !m_branchEdgeIds.contains(m_topologyPreviewTerminalEdgeId)) clearTopologyPreview();
}

void SelectionChainDialog::selectTopology(const QString &id, int kind,
                                           const QVector3D &point,
                                           double curveParameter)
{
    const QList<TopologyMatch> matches = topologyMatches(id, kind);
    if (matches.isEmpty()) return;
    QList<TopologyMatch> preferred;
    for (const TopologyMatch &match : matches) {
        if (match.candidateIndex == m_activeLoopCandidateIndex &&
            match.loopIndex == m_activeLoopIndex) {
            preferred = {match};
            break;
        }
    }
    if (preferred.isEmpty() && m_candidateIndex >= 0) {
        for (const TopologyMatch &match : matches)
            if (match.candidateIndex == m_candidateIndex) preferred.append(match);
    }
    if (preferred.isEmpty()) preferred = matches;
    if (preferred.size() != 1) {
        setPendingTopologyPick(id, kind, point, curveParameter, preferred);
        return;
    }
    selectTopologyMatch(id, kind, point, curveParameter, preferred.first());
}

void SelectionChainDialog::selectTopologyMatch(
    const QString &id, int kind, const QVector3D &point,
    double curveParameter, const TopologyMatch &match)
{
    Q_UNUSED(point);
    const int candidateIndex = match.candidateIndex;
    if (candidateChangeWouldDiscardDraft(candidateIndex, kind)) {
        m_status->setText(kind == static_cast<int>(SelectionTopologyKind::Vertex)
            ? tr("所选顶点不属于当前链，起点未改变。")
            : tr("所选边属于另一候选且与当前选择会话不连续，草稿未改变。"));
        return;
    }
    const bool changedCandidate=m_candidateIndex != candidateIndex;
    if (changedCandidate) {
        selectGeometry(m_candidates.at(candidateIndex).geometryId);
    }
    if (m_candidateIndex != candidateIndex) return;
    if (!changedCandidate && kind == static_cast<int>(SelectionTopologyKind::Edge) &&
        m_topologyPreviewKind == TopologyPreviewKind::FullBoundary) {
        beginBoundaryAdjustment(ChainSelectionMode::Edge);
    }
    if (kind == static_cast<int>(SelectionTopologyKind::Edge) &&
        m_topologyPreviewKind == TopologyPreviewKind::Interval &&
        id == m_topologyPreviewTerminalEdgeId) {
        applyPreviewDraft();
        return;
    }
    if (m_topologyPreviewKind == TopologyPreviewKind::Interval) clearTopologyPreview();
    const SelectionCandidate &candidate = m_candidates.at(candidateIndex);
    const ChainSelectionMode mode = static_cast<ChainSelectionMode>(m_mode->currentIndex());

    if (kind == static_cast<int>(SelectionTopologyKind::Face) &&
        mode == ChainSelectionMode::Face) {
        remember();
        m_regionSelections.clear();
        QList<SelectionBoundaryLoop> loops;
        for (const MeshBoundaryLoop &meshLoop : candidate.boundaryLoops) {
            if (meshLoop.faceId != id) continue;
            loops.append({meshLoop.faceId, meshLoop.outer,
                          meshLoop.orderedEdgeIds, meshLoop.orderedForward});
        }
        applyTopologySelection({{id, SelectionTopologyKind::Face, true}},
                               {}, false, {}, false, loops, true);
        return;
    }

    if (kind == static_cast<int>(SelectionTopologyKind::Vertex)) {
        if (m_pickingStart) selectStartVertex(id);
        return;
    }

    if (kind != static_cast<int>(SelectionTopologyKind::Edge) ||
        mode == ChainSelectionMode::Face) return;
    const MeshPickEdge *clickedEdge = edgeForId(candidate, id);
    if (!clickedEdge || clickedEdge->points.size() < 2) return;

    const double firstParameter = clickedEdge->parameters.value(0, 0.0);
    const double lastParameter = clickedEdge->parameters.value(
        clickedEdge->parameters.size() - 1, 1.0);
    const bool nearFirst = std::abs(curveParameter - firstParameter) <=
                           std::abs(curveParameter - lastParameter);
    const QString nearVertexId = nearFirst ? clickedEdge->firstVertexId
                                           : clickedEdge->lastVertexId;
    const QString farVertexId = nearFirst ? clickedEdge->lastVertexId
                                          : clickedEdge->firstVertexId;
    const QVector3D nearPoint = nearFirst ? clickedEdge->points.first()
                                          : clickedEdge->points.last();
    const QVector3D farPoint = nearFirst ? clickedEdge->points.last()
                                         : clickedEdge->points.first();
    SelectionAnchor start{nearVertexId, nearPoint, false, 0.0};
    SelectionAnchor end{farVertexId, farPoint, false, 0.0};

    const MeshBoundaryLoop *meshLoop = nullptr;
    int clickedIndex = -1;
    if (match.loopIndex >= 0 && match.loopIndex < candidate.boundaryLoops.size()) {
        meshLoop = &candidate.boundaryLoops.at(match.loopIndex);
        clickedIndex = meshLoop->orderedEdgeIds.indexOf(id);
    }
    QList<SelectionBoundaryLoop> matchedLoops;
    if (meshLoop) {
        matchedLoops.append({meshLoop->faceId, meshLoop->outer,
                             meshLoop->orderedEdgeIds, meshLoop->orderedForward});
    }
    const bool loopForward = meshLoop
        ? meshLoop->orderedForward.value(clickedIndex, true) : true;
    const int step = loopForward == nearFirst ? 1 : -1;

    if (mode == ChainSelectionMode::Chain && candidate.topologyOnly &&
        !m_adjustingBoundary && meshLoop) {
        SelectionChain fullBoundary;
        if (!buildLoopDraft(match.loopIndex, clickedIndex, step,
                            meshLoop->orderedEdgeIds.size(), &fullBoundary) ||
            !fullBoundary.closed) {
            m_status->setText(tr("所选环不是连续闭环；开放槽不会自动补线。请使用“调整边界”。"));
            return;
        }
        m_activeLoopCandidateIndex = candidateIndex;
        m_activeLoopIndex = match.loopIndex;
        m_topologyPreviewKind = TopologyPreviewKind::FullBoundary;
        m_topologyPreviewDraft = fullBoundary;
        m_topologyPreviewTerminalEdgeId.clear();
        refresh();
        m_status->setText(tr("已在模型上确定具体%1。请使用完整边界，或调整边界。")
                          .arg(meshLoop->outer ? tr("外环") : tr("内环")));
        return;
    }

    if (mode == ChainSelectionMode::Edge) {
        SelectionChainController preview = m_controller;
        const SelectionChain draft = preview.commit();
        for (int index = 0; index < draft.orderedTopologyRefs.size(); ++index) {
            if (draft.orderedTopologyRefs.at(index).id != id) continue;
            if (!draft.closed && index > 0 && index + 1 < draft.orderedTopologyRefs.size()) {
                m_status->setText(tr("取消此边会断开当前链；请从端部取消，草稿未改变。"));
                return;
            }
            remember();
            QList<SelectionTopologyRef> refs = draft.orderedTopologyRefs;
            refs.removeAt(index);
            if (draft.closed && !refs.isEmpty())
                std::rotate(refs.begin(), refs.begin() + index % refs.size(), refs.end());
            SelectionAnchor remainingStart, remainingEnd;
            if (!refs.isEmpty()) {
                const auto &firstRef = refs.first();
                const auto &lastRef = refs.last();
                const auto *first = edgeForId(candidate, firstRef.id);
                const auto *last = edgeForId(candidate, lastRef.id);
                remainingStart = {firstRef.forward ? first->firstVertexId : first->lastVertexId,
                                  firstRef.forward ? first->points.first() : first->points.last(), false, 0.0};
                remainingEnd = {lastRef.forward ? last->lastVertexId : last->firstVertexId,
                                lastRef.forward ? last->points.last() : last->points.first(), false, 0.0};
            }
            clearTopologyPreview();
            applyTopologySelection(refs, remainingStart, !refs.isEmpty(),
                                   remainingEnd, !refs.isEmpty(), draft.boundaryLoops, false);
            return;
        }
        const bool startNew = draft.orderedTopologyRefs.isEmpty() ||
                              m_controller.state() == SelectionChainState::Ordered;
        if (startNew) {
            remember();
            const bool adjustingBoundary = m_adjustingBoundary;
            const bool hasAdjustBaseline = m_hasAdjustBaseline;
            const Snapshot adjustBaseline = m_adjustBaseline;
            resetTransientSession();
            m_adjustingBoundary = adjustingBoundary;
            m_hasAdjustBaseline = hasAdjustBaseline;
            m_adjustBaseline = adjustBaseline;
            m_activeLoopCandidateIndex = candidateIndex;
            m_activeLoopIndex = match.loopIndex;
            applyTopologySelection({{id, SelectionTopologyKind::Edge, nearFirst}},
                                   start, true, end, true, matchedLoops, false);
            return;
        }
        if (!draft.hasEndAnchor ||
            (clickedEdge->firstVertexId != draft.endAnchor.topologyId &&
             clickedEdge->lastVertexId != draft.endAnchor.topologyId)) {
            m_status->setText(tr("所选边与当前链终点不连续，草稿未改变。"));
            return;
        }
        updateTopologyBranches(draft);
        if (!m_branchEdgeIds.contains(id)) {
            m_status->setText(tr("所选边不在当前边界环内，草稿未改变。"));
            return;
        }
        remember();
        clearTopologyPreview();
        QList<SelectionTopologyRef> refs = draft.orderedTopologyRefs;
        const bool forward = clickedEdge->firstVertexId == draft.endAnchor.topologyId;
        refs.append({id, SelectionTopologyKind::Edge, forward});
        end.topologyId = forward ? clickedEdge->lastVertexId : clickedEdge->firstVertexId;
        end.point = forward ? clickedEdge->points.last() : clickedEdge->points.first();
        end.hasCurveParameter = false;
        end.curveParameter = 0.0;
        const bool closed = draft.hasStartAnchor &&
                            end.topologyId == draft.startAnchor.topologyId;
        QList<SelectionBoundaryLoop> loops = draft.boundaryLoops;
        if (loops.isEmpty()) loops = matchedLoops;
        if (!loops.isEmpty()) {
            bool oneLoopContainsChain = false;
            for (const SelectionBoundaryLoop &loop : loops) {
                bool containsAll = true;
                for (const SelectionTopologyRef &ref : refs)
                    if (!loop.orderedEdgeIds.contains(ref.id)) { containsAll = false; break; }
                if (containsAll) { oneLoopContainsChain = true; break; }
            }
            if (!oneLoopContainsChain) loops.clear();
        }
        m_activeLoopCandidateIndex = candidateIndex;
        m_activeLoopIndex = match.loopIndex;
        applyTopologySelection(refs, draft.startAnchor, true, end, !closed,
                               loops, closed);
        return;
    }

    if (!meshLoop) {
        SelectionChainController preview = m_controller;
        const SelectionChain draft = preview.commit();
        QList<SelectionTopologyRef> refs;
        SelectionAnchor chainStart = start;
        if ((mode == ChainSelectionMode::Chain || mode == ChainSelectionMode::PartialChain) &&
            draft.evidenceVersion >= 2 && !draft.orderedTopologyRefs.isEmpty() &&
            draft.hasEndAnchor &&
            (clickedEdge->firstVertexId == draft.endAnchor.topologyId ||
             clickedEdge->lastVertexId == draft.endAnchor.topologyId)) {
            updateTopologyBranches(draft);
            if (!m_branchEdgeIds.contains(id)) {
                m_status->setText(tr("所选边不在当前边界环内，草稿未改变。"));
                return;
            }
            refs = draft.orderedTopologyRefs;
            const bool forward = clickedEdge->firstVertexId == draft.endAnchor.topologyId;
            refs.append({id, SelectionTopologyKind::Edge, forward});
            chainStart = draft.startAnchor;
            end.topologyId = forward ? clickedEdge->lastVertexId : clickedEdge->firstVertexId;
            end.point = forward ? clickedEdge->points.last() : clickedEdge->points.first();
            end.curveParameter = forward
                ? clickedEdge->parameters.value(clickedEdge->parameters.size() - 1, 1.0)
                : clickedEdge->parameters.value(0, 0.0);
        } else {
            refs.append({id, SelectionTopologyKind::Edge, nearFirst});
        }
        const bool closed = !refs.isEmpty() && draft.hasStartAnchor &&
                            end.topologyId == chainStart.topologyId;
        remember();
        applyTopologySelection(refs, chainStart, true, end, !closed, {}, closed);
        return;
    }
    QList<SelectionBoundaryLoop> loops = matchedLoops;

    if (mode == ChainSelectionMode::PartialChain && m_partialStartEdgeId.isEmpty()) {
        remember();
        m_partialStartEdgeId = id;
        m_partialLoopFaceId = meshLoop->faceId;
        m_partialLoopCandidateIndex = candidateIndex;
        m_partialLoopIndex = match.loopIndex;
        m_partialStartIndex = clickedIndex;
        m_partialStep = step;
        m_partialStartAnchor = start;
        m_activeLoopCandidateIndex = candidateIndex;
        m_activeLoopIndex = match.loopIndex;
        applyTopologySelection({{id, SelectionTopologyKind::Edge, nearFirst}},
                               start, true, end, true, loops, false);
        return;
    }

    int firstIndex = clickedIndex;
    int traversalStep = step;
    SelectionAnchor traversalStart = start;
    int edgeCount = meshLoop->orderedEdgeIds.size();
    int traversalCount = edgeCount;
    if (mode == ChainSelectionMode::PartialChain) {
        if (m_partialLoopCandidateIndex != candidateIndex ||
            m_partialLoopIndex != match.loopIndex ||
            m_partialLoopFaceId != meshLoop->faceId ||
            m_partialStartIndex < 0 || m_partialStartIndex >= edgeCount ||
            meshLoop->orderedEdgeIds.at(m_partialStartIndex) != m_partialStartEdgeId) {
            m_status->setText(tr("终止边不在局部串联的起始环上，草稿未改变。"));
            return;
        }
        firstIndex = m_partialStartIndex;
        traversalStep = m_partialStep;
        traversalStart = m_partialStartAnchor;
        traversalCount = 1;
        int cursor = firstIndex;
        while (cursor != clickedIndex && traversalCount <= edgeCount) {
            cursor = (cursor + traversalStep + edgeCount) % edgeCount;
            ++traversalCount;
        }
    }

    QList<SelectionTopologyRef> refs;
    int cursor = firstIndex;
    for (int count = 0; count < traversalCount; ++count) {
        const bool forward = traversalStep > 0
            ? meshLoop->orderedForward.value(cursor, true)
            : !meshLoop->orderedForward.value(cursor, true);
        refs.append({meshLoop->orderedEdgeIds.at(cursor), SelectionTopologyKind::Edge, forward});
        cursor = (cursor + traversalStep + edgeCount) % edgeCount;
    }
    const MeshPickEdge *lastEdge = edgeForId(candidate, refs.last().id);
    if (lastEdge) {
        end.topologyId = refs.last().forward ? lastEdge->lastVertexId : lastEdge->firstVertexId;
        end.point = refs.last().forward ? lastEdge->points.last() : lastEdge->points.first();
        end.curveParameter = refs.last().forward
            ? lastEdge->parameters.value(lastEdge->parameters.size() - 1, 1.0)
            : lastEdge->parameters.value(0, 0.0);
    }
    const bool closed = mode == ChainSelectionMode::Chain && refs.size() == edgeCount;
    remember();
    m_activeLoopCandidateIndex = candidateIndex;
    m_activeLoopIndex = match.loopIndex;
    applyTopologySelection(refs, traversalStart, true, end, !closed, loops, closed);
}

void SelectionChainDialog::selectStartVertex(const QString &id)
{
    if (m_candidateIndex < 0) return;
    const auto &candidate = m_candidates.at(m_candidateIndex);
    SelectionChainController preview = m_controller;
    const SelectionChain draft = preview.commit();
    const MeshPickVertex *pickedVertex = vertexForId(candidate, id);
    if (!pickedVertex || draft.evidenceVersion < 2 ||
        draft.orderedTopologyRefs.isEmpty()) return;
    QList<SelectionTopologyRef> refs = draft.orderedTopologyRefs;
    auto directedStart = [&candidate, this](const SelectionTopologyRef &ref) {
        const MeshPickEdge *edge = edgeForId(candidate, ref.id);
        return edge ? (ref.forward ? edge->firstVertexId : edge->lastVertexId) : QString();
    };
    auto directedEnd = [&candidate, this](const SelectionTopologyRef &ref) {
        const MeshPickEdge *edge = edgeForId(candidate, ref.id);
        return edge ? (ref.forward ? edge->lastVertexId : edge->firstVertexId) : QString();
    };
    int startIndex = -1;
    if (draft.closed) {
        for (int i = 0; i < refs.size(); ++i)
            if (directedStart(refs.at(i)) == id) { startIndex = i; break; }
        if (startIndex < 0) {
            m_status->setText(tr("所选顶点不属于当前链，起点未改变。"));
            return;
        }
        remember();
        std::rotate(refs.begin(), refs.begin() + startIndex, refs.end());
    } else {
        const QString firstId = directedStart(refs.first());
        const QString lastId = directedEnd(refs.last());
        if (id != firstId && id != lastId) {
            m_status->setText(tr("开放链只能从首端点或末端点开始，内部顶点未采用。"));
            return;
        }
        remember();
        if (id == lastId) {
            m_controller.reverse();
            SelectionChainController reversedPreview = m_controller;
            refs = reversedPreview.commit().orderedTopologyRefs;
        }
    }
    const SelectionAnchor start{id, pickedVertex->point, false, 0.0};
    SelectionAnchor endAnchor;
    bool hasEndAnchor = false;
    if (!draft.closed) {
        const QString endId = directedEnd(refs.last());
        const MeshPickVertex *endVertex = vertexForId(candidate, endId);
        if (endVertex) {
            endAnchor = {endId, endVertex->point, false, 0.0};
            hasEndAnchor = true;
        }
    }
    applyTopologySelection(refs, start, true, endAnchor, hasEndAnchor,
                           draft.boundaryLoops, draft.closed);
    m_pickingStart = false;
    syncPickMode();
    refresh();
    return;
}
