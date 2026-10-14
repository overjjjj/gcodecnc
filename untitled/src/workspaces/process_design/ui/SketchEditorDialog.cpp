#include "SketchEditorDialog.h"
#include "SketchMeasurementDialog.h"
#include "SketchTrimDialog.h"
#include "SketchOffsetDialog.h"
#include "SketchFilletDialog.h"
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

SketchEditorDialog::SketchEditorDialog(const SketchDocument &document, QWidget *parent)
    : QDialog(parent), m_document(document)
{
    setObjectName("sketchEditorDialog");
    setWindowTitle(tr("二维绘图"));
    resize(1000, 680);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("在 XY 平面绘制或替换当前图元；选择右侧图元后编辑、复制或变换。画布点击吸附到 0.1 mm，数值可精确编辑。保存后生效。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *toolbar = new QHBoxLayout;
    auto button = [this, &toolbar](const QString &text, const char *name) {
        auto *b = new QPushButton(text, this); b->setObjectName(name); toolbar->addWidget(b); return b;
    };
    auto *rectangle = button(tr("矩形：两角"), "sketchRectangleButton");
    auto *polyline = button(tr("闭合多段线"), "sketchPolylineButton");
    for (const auto &kind : QStringList{"line","circle","arc"}) {
        auto *primitive=button(kind=="line"?tr("直线"):kind=="circle"?tr("圆"):tr("圆弧"),
                              kind=="line"?"sketchLineButton":kind=="circle"?"sketchCircleButton":"sketchArcButton");
        m_primitives.append(primitive);connect(primitive,&QPushButton::clicked,this,[this,kind]{createPrimitive(kind);});
    }
    toolbar->addStretch();layout->addLayout(toolbar);toolbar=new QHBoxLayout;
    m_finish = button(tr("闭合轮廓"), "finishSketchContourButton");
    m_undo = button(tr("撤销末点"), "undoSketchPointButton");
    auto *clear = button(tr("删除图元"), "deleteSketchContourButton");
    auto *fit = button(tr("适合视图"), "fitSketchButton");
    m_undoEdit = button(tr("撤销修改"), "undoSketchEditButton");
    m_measurements = button(tr("测量与标注"), "sketchMeasurementsButton");
    toolbar->addStretch(); layout->addLayout(toolbar);
    auto *split = new QSplitter(this);
    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene, split);
    m_view->setObjectName("sketchCanvas");
    m_view->setAccessibleName(tr("XY 草图画布"));
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->viewport()->installEventFilter(this);
    auto *coordinates = new QWidget(split);
    auto *coordinateLayout = new QVBoxLayout(coordinates);
    auto *selectionLabel = new QLabel(tr("当前图元"), coordinates);
    m_contours = new QComboBox(coordinates); m_contours->setObjectName("sketchContourSelection");
    selectionLabel->setBuddy(m_contours);
    coordinateLayout->addWidget(selectionLabel); coordinateLayout->addWidget(m_contours);
    auto *transformLayout = new QGridLayout;
    const QStringList operations{"move","copy","rotate","mirror"};
    const QStringList labels{tr("移动"),tr("复制"),tr("旋转"),tr("镜像")};
    for (int i=0;i<operations.size();++i) {
        const auto operation=operations[i];
        auto *button=new QPushButton(labels[i],coordinates);
        button->setObjectName(operation+"SketchContourButton");
        transformLayout->addWidget(button,i/2,i%2);m_transforms.append(button);
        connect(button,&QPushButton::clicked,this,[this,operation]{transformSelected(operation);});
    }
    coordinateLayout->addLayout(transformLayout);
    for(int i=0;i<2;++i) {
        auto *button=new QPushButton(i?tr("延伸"):tr("修剪"),coordinates);
        button->setObjectName(i?"extendSketchContourButton":"trimSketchContourButton");
        transformLayout->addWidget(button,2,i);m_trimButtons.append(button);
        connect(button,&QPushButton::clicked,this,[this,i]{trimSelected(i!=0);});
    }
    m_offset=new QPushButton(tr("偏移副本"),coordinates);m_offset->setObjectName("offsetSketchContourButton");
    transformLayout->addWidget(m_offset,3,0);connect(m_offset,&QPushButton::clicked,this,&SketchEditorDialog::offsetSelected);
    m_fillet=new QPushButton(tr("圆角"),coordinates);m_fillet->setObjectName("filletSketchContourButton");
    transformLayout->addWidget(m_fillet,3,1);connect(m_fillet,&QPushButton::clicked,this,&SketchEditorDialog::filletSelected);
    auto *zLabel = new QLabel(tr("平面 Z（mm）"), coordinates);
    m_planeZ = new QLineEdit(QString::number(document.planeZ, 'g', 17), coordinates);
    m_planeZ->setObjectName("sketchPlaneZ"); zLabel->setBuddy(m_planeZ);
    coordinateLayout->addWidget(zLabel); coordinateLayout->addWidget(m_planeZ);
    m_curveProperties=new QWidget(coordinates);
    auto *curveForm=new QFormLayout(m_curveProperties);curveForm->setContentsMargins(0,0,0,0);
    m_curveRadius=new QLineEdit("0",m_curveProperties);m_curveRadius->setObjectName("sketchCurveRadius");
    m_curveStart=new QLineEdit("0",m_curveProperties);m_curveStart->setObjectName("sketchCurveStart");
    m_curveSweep=new QLineEdit("0",m_curveProperties);m_curveSweep->setObjectName("sketchCurveSweep");
    curveForm->addRow(tr("半径（mm）"),m_curveRadius);curveForm->addRow(tr("起始角（°）"),m_curveStart);
    curveForm->addRow(tr("扫角（°）"),m_curveSweep);coordinateLayout->addWidget(m_curveProperties);
    coordinateLayout->addWidget(new QLabel(tr("点坐标（mm；圆／弧为圆心）"), coordinates));
    m_points = new QTableWidget(0, 2, coordinates);
    m_points->setObjectName("sketchPointsTable");
    m_points->setAccessibleName(tr("图元点坐标，圆与圆弧为圆心，矩形联动相邻角点"));
    m_points->setHorizontalHeaderLabels({"X", "Y"});
    m_points->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    coordinateLayout->addWidget(m_points);
    m_annotationList=new QListWidget(coordinates);m_annotationList->setObjectName("sketchAnnotationList");
    m_annotationList->setAccessibleName(tr("当前草图标注结果"));m_annotationList->setMaximumHeight(140);
    coordinateLayout->addWidget(m_annotationList);
    split->setStretchFactor(0, 3); split->setStretchFactor(1, 1);
    split->setSizes({700, 280}); layout->addWidget(split, 1);
    m_status = new QLabel(this); m_status->setObjectName("sketchStatus");
    m_status->setWordWrap(true); layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_save = buttons->button(QDialogButtonBox::Save); m_save->setObjectName("saveSketchButton");
    m_save->setText(tr("保存草图")); m_save->setDefault(false); m_save->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        readCoordinates();
        if (m_save->isEnabled()) accept();
    });
    connect(m_contours,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int index) {
        if (index<0 || !m_drawingKind.isEmpty()) return;
        m_selected=index;refresh();
    });
    connect(m_undoEdit,&QPushButton::clicked,this,&SketchEditorDialog::undoChange);
    connect(m_measurements,&QPushButton::clicked,this,&SketchEditorDialog::editMeasurements);
    connect(m_annotationList,&QListWidget::currentRowChanged,this,[this](int){refresh(false);});
    connect(rectangle, &QPushButton::clicked, this, [this] { startDrawing("rectangle"); });
    connect(polyline, &QPushButton::clicked, this, [this] { startDrawing("polyline"); });
    connect(m_finish, &QPushButton::clicked, this, [this] {
        if (m_drawingKind != "polyline" || m_pending.size() < 3) return;
        SketchDocument candidate = m_document;
        auto value=selectedContour();value.points=m_pending;value.kind="polyline";value.radius=value.startAngle=value.sweepAngle=0;
        if (value.id.isEmpty()) value.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        candidate.setContour(m_selected,value);
        const QString error = candidate.validationError();
        if (!error.isEmpty()) { m_status->setText(error + tr(" 请撤销末点或重新绘制。")); return; }
        rememberChange();m_document = candidate; m_drawingKind.clear(); m_pending.clear(); refresh();
    });
    connect(m_undo, &QPushButton::clicked, this, [this] {
        if (!m_pending.isEmpty()) m_pending.removeLast();
        refresh();
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (!m_document.isEmpty()) rememberChange();
        m_drawingKind.clear();m_pending.clear();m_document.removeContour(m_selected);
        m_selected=std::max(0,std::min(m_selected,m_document.contourCount()-1));
        m_coordinatesValid=true;refresh();
    });
    connect(fit, &QPushButton::clicked, this, &SketchEditorDialog::fitDrawing);
    connect(m_planeZ, &QLineEdit::textChanged, this, [this] { readCoordinates(); });
    for (auto *field : {m_curveRadius,m_curveStart,m_curveSweep})
        connect(field,&QLineEdit::textChanged,this,[this]{readCoordinates();});
    connect(m_points, &QTableWidget::cellChanged, this, [this](int row, int column) {
        const auto selected=selectedContour();
        if (selected.kind == "rectangle" && selected.points.size() == 4) {
            bool ok = false;
            const double value = m_points->item(row, column)->text().toDouble(&ok);
            if (ok && std::isfinite(value)) {
                const QSignalBlocker blocker(m_points);
                const bool useOppositePair = (column == 0) == m_rectangleDirections.value(selected.id,true);
                const int partner = useOppositePair ? 3 - row : (row ^ 1);
                m_points->item(partner, column)->setText(QString::number(value, 'g', 17));
            }
        }
        readCoordinates();
    });
    refresh();
}

void SketchEditorDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    fitDrawing();
}

void SketchEditorDialog::startDrawing(const QString &kind)
{
    m_drawingKind = kind; m_pending.clear(); refresh(); m_view->setFocus();
}

bool SketchEditorDialog::eventFilter(QObject *object, QEvent *event)
{
    if(object==m_view->viewport() && event->type()==QEvent::Resize)
        QTimer::singleShot(0,this,[this]{refresh(false);});
    if (object == m_view->viewport() && event->type() == QEvent::MouseButtonPress && !m_drawingKind.isEmpty()) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        QPointF point = m_view->mapToScene(mouse->pos());
        point = QPointF(std::round(point.x() * 10) / 10, -std::round(point.y() * 10) / 10);
        if (m_pending.size() >= 4096) return true;
        m_pending.append(point);
        if (m_drawingKind == "rectangle" && m_pending.size() == 2) {
            const auto a = m_pending[0], b = m_pending[1];
            auto value=selectedContour();
            value.points={a,QPointF(b.x(),a.y()),b,QPointF(a.x(),b.y())};value.kind="rectangle";value.radius=value.startAngle=value.sweepAngle=0;
            if (value.id.isEmpty()) value.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            rememberChange();m_rectangleDirections.insert(value.id,true);m_document.setContour(m_selected,value);
            m_pending.clear(); m_drawingKind.clear(); m_coordinatesValid = true;
        }
        refresh(); return true;
    }
    return QDialog::eventFilter(object, event);
}

void SketchEditorDialog::readCoordinates()
{
    SketchDocument candidate = m_document;
    auto selected=selectedContour();
    bool ok = false;
    candidate.planeZ = m_planeZ->text().toDouble(&ok);
    m_coordinatesValid = ok && std::isfinite(candidate.planeZ);
    for (int row = 0; row < m_points->rowCount(); ++row) {
        const double x = m_points->item(row, 0)->text().toDouble(&ok);
        m_coordinatesValid = m_coordinatesValid && ok && std::isfinite(x);
        const double y = m_points->item(row, 1)->text().toDouble(&ok);
        m_coordinatesValid = m_coordinatesValid && ok && std::isfinite(y);
        selected.points[row] = QPointF(x, y);
    }
    if (selected.isCurve()) {
        selected.radius=m_curveRadius->text().toDouble(&ok);m_coordinatesValid &= ok && std::isfinite(selected.radius);
        selected.startAngle=m_curveStart->text().toDouble(&ok);m_coordinatesValid &= ok && std::isfinite(selected.startAngle);
        selected.sweepAngle=m_curveSweep->text().toDouble(&ok);m_coordinatesValid &= ok && std::isfinite(selected.sweepAngle);
    }
    if (m_coordinatesValid) {
        if (!m_document.isEmpty()) candidate.setContour(m_selected,selected);
        if (candidate.toJson()!=m_document.toJson()) { rememberChange();m_document=candidate; }
    }
    refresh(false);
}

void SketchEditorDialog::refresh(bool rebuildTable)
{
    const auto selected=selectedContour();
    if (selected.kind=="rectangle" && selected.points.size()==4) {
        for (int i=0;i<4;++i) {
            const auto &a=selected.points[i], &b=selected.points[(i+1)%4];
            if (a==b || (a.x()!=b.x() && a.y()!=b.y())) continue;
            const bool horizontal=a.y()==b.y();
            m_rectangleDirections.insert(selected.id,i%2==0?horizontal:!horizontal);break;
        }
    }
    if (rebuildTable) {
        const QSignalBlocker selectionBlocker(m_contours);
        m_contours->clear();
        for (int i=0;i<m_document.contourCount();++i)
        {
            const auto kind=m_document.contour(i).kind;
            m_contours->addItem(tr("图元 %1 · %2").arg(i+1).arg(kind=="rectangle"?tr("矩形"):kind=="polyline"?tr("闭合多段线"):
                kind=="line"?tr("直线（开放）"):kind=="circle"?tr("圆"):tr("圆弧（开放）")));
        }
        m_contours->setCurrentIndex(m_document.isEmpty()?-1:m_selected);
        bool validPlane = false;
        const double planeZ = m_planeZ->text().toDouble(&validPlane);
        m_coordinatesValid = validPlane && std::isfinite(planeZ);
        { const QSignalBlocker r(m_curveRadius),a(m_curveStart),s(m_curveSweep);
          m_curveRadius->setText(QString::number(selected.radius,'g',17));
          m_curveStart->setText(QString::number(selected.startAngle,'g',17));
          m_curveSweep->setText(QString::number(selected.sweepAngle,'g',17)); }
        const QSignalBlocker blocker(m_points);
        m_points->setRowCount(selected.points.size());
        for (int i = 0; i < selected.points.size(); ++i) {
            m_points->setItem(i, 0, new QTableWidgetItem(QString::number(selected.points[i].x(), 'g', 17)));
            m_points->setItem(i, 1, new QTableWidgetItem(QString::number(selected.points[i].y(), 'g', 17)));
        }
    }
    m_scene->clear();
    QPen axis(palette().color(QPalette::Mid)); axis.setCosmetic(true);
    const QRectF bounds = m_scene->sceneRect();
    m_scene->addLine(bounds.left(), 0, bounds.right(), 0, axis);
    m_scene->addLine(0, bounds.top(), 0, bounds.bottom(), axis);
    const auto draw=[this](const QVector<QPointF> &points, bool closed, bool selected, const QVector<QPointF> &markers) {
        QPainterPath path;
        for (int i=0;i<points.size();++i) {
            const QPointF p(points[i].x(),-points[i].y());
            if (i==0) path.moveTo(p);else path.lineTo(p);
        }
        if (closed && !points.isEmpty()) path.closeSubpath();
        QPen pen(selected?QColor("#1e40af"):palette().color(QPalette::Mid),selected?2:1);
        pen.setCosmetic(true);m_scene->addPath(path,pen);
        if (selected) for (const auto &point : markers) {
            auto *vertex=m_scene->addEllipse(-3,-3,6,6,pen,QBrush(pen.color()));
            vertex->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            vertex->setPos(point.x(),-point.y());
        }
    };
    for (int i=0;i<m_document.contourCount();++i) {
        if (!m_drawingKind.isEmpty() && i==m_selected) continue;
        const auto value=m_document.contour(i);const auto samples=value.displayPoints();
        auto markers=value.points;
        if (value.isCurve() && !samples.isEmpty()) {
            markers.append(samples.first());if (value.kind=="arc") markers.append(samples.last());
        }
        draw(samples,value.isClosed(),i==m_selected,markers);
    }
    if (!m_drawingKind.isEmpty()) draw(m_pending,false,true,m_pending);
    const QSignalBlocker annotationBlocker(m_annotationList);
    const int selectedAnnotation=std::max(0,std::min(m_annotationList->currentRow(),m_document.annotations.size()-1));
    m_annotationList->clear();m_annotationList->setVisible(!m_document.annotations.isEmpty());
    for(int i=0;i<m_document.annotations.size();++i) {
        const auto measurement=measureSketch(m_document,m_document.annotations[i]);
        const auto text=tr("标注 %1 · %2").arg(i+1).arg(measurement.text);
        auto *row=new QListWidgetItem(text,m_annotationList);row->setToolTip(text);
        if(measurement.valid && i==selectedAnnotation) {
            auto *label=m_scene->addSimpleText(text);label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            label->setToolTip(text);
            const auto viewport=m_view->viewport()->rect();
            label->setText(QFontMetricsF(label->font()).elidedText(text,Qt::ElideRight,std::max(20,viewport.width()-20)));
            const auto anchor=m_view->mapFromScene(QPointF(measurement.point.x(),-measurement.point.y()));
            const int maxX=std::max(8,viewport.width()-int(std::ceil(label->boundingRect().width()))-8);
            const int maxY=std::max(8,viewport.height()-int(std::ceil(label->boundingRect().height()))-8);
            label->setBrush(palette().color(QPalette::Text));
            label->setPos(m_view->mapToScene(QPoint(std::max(8,std::min(anchor.x()+8,maxX)),std::max(8,std::min(anchor.y()+8,maxY)))));
            label->setData(0,m_document.annotations[i].id);
        } else if(!measurement.valid)row->setForeground(QColor("#b91c1c"));
    }
    m_annotationList->setCurrentRow(m_document.annotations.isEmpty()?-1:selectedAnnotation);
    const QString error = m_coordinatesValid ? m_document.validationError() : tr("请输入有限数值坐标。");
    m_measurements->setEnabled(m_drawingKind.isEmpty() && error.isEmpty());
    m_save->setEnabled(m_drawingKind.isEmpty() && error.isEmpty());
    m_undo->setEnabled(!m_pending.isEmpty());
    m_undoEdit->setEnabled(m_drawingKind.isEmpty() && !m_history.isEmpty());
    m_contours->setEnabled(m_drawingKind.isEmpty() && m_coordinatesValid);
    for (auto *button : m_transforms)
        button->setEnabled(m_drawingKind.isEmpty() && !m_document.isEmpty() && error.isEmpty());
    for (auto *button : m_primitives) button->setEnabled(m_drawingKind.isEmpty());
    for(auto *button:m_trimButtons)button->setEnabled(m_drawingKind.isEmpty() && m_document.contourCount()>1 && error.isEmpty());
    m_offset->setEnabled(m_drawingKind.isEmpty() && !m_document.isEmpty() && error.isEmpty());
    m_fillet->setEnabled(m_drawingKind.isEmpty() && !m_document.isEmpty() && !selected.isCurve() && error.isEmpty());
    m_curveProperties->setVisible(selected.isCurve());m_curveProperties->setEnabled(m_drawingKind.isEmpty());
    m_curveSweep->setEnabled(selected.kind=="arc");
    m_finish->setEnabled(m_drawingKind == "polyline" && m_pending.size() >= 3);
    m_points->setEnabled(m_drawingKind.isEmpty());
    m_status->setText(!m_drawingKind.isEmpty()
        ? (m_drawingKind == "rectangle" ? (m_pending.isEmpty() ? tr("请点击矩形的第一个角点。") : tr("首角已输入，请点击对角点。")) : tr("逐点点击，完成后选择“闭合轮廓”。已输入 %1 点。").arg(m_pending.size()))
        : !error.isEmpty() ? error : m_document.isEmpty() ? tr("空草图：选择绘图工具开始，或保存空文档。")
        : m_document.contourCount()>1 ? tr("共 %1 个图元，当前图元 %2。多图元可编辑保存；加工前请只保留一个凸直线闭合轮廓。").arg(m_document.contourCount()).arg(m_selected+1)
        : selected.kind=="line" || selected.isCurve() ? tr("当前图元可编辑和保存；直线、圆和圆弧加工尚未验证。圆弧正扫角逆时针，负扫角顺时针。")
        : tr("闭合轮廓，共 %1 个顶点；加工方向固定为 +Z。矩形坐标编辑会联动对应角点。").arg(selected.points.size()));
}

void SketchEditorDialog::fitDrawing()
{
    QRectF bounds(-50, -50, 100, 100);
    const auto points=m_document.boundingPoints()+m_document.allPoints();
    if (!points.isEmpty()) {
        const auto &first = points.first();
        double minX = first.x(), maxX = minX, minY = -first.y(), maxY = minY;
        for (const auto &p : points) {
            minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
            minY = std::min(minY, -p.y()); maxY = std::max(maxY, -p.y());
        }
        const double margin = std::max(1.0, std::max(maxX-minX, maxY-minY) * .15);
        bounds = QRectF(QPointF(minX-margin, minY-margin), QPointF(maxX+margin, maxY+margin));
    }
    m_scene->setSceneRect(bounds); m_view->fitInView(bounds, Qt::KeepAspectRatio); refresh(false);
}

SketchContour SketchEditorDialog::selectedContour() const
{
    return m_document.isEmpty()?SketchContour{}:m_document.contour(m_selected);
}

void SketchEditorDialog::editMeasurements()
{
    SketchMeasurementDialog dialog(m_document,this);
    if(dialog.exec()!=QDialog::Accepted)return;
    auto candidate=m_document;candidate.annotations=dialog.annotations();
    if(candidate.toJson()==m_document.toJson())return;
    rememberChange();m_document=candidate;refresh(false);
}

void SketchEditorDialog::rememberChange()
{
    m_history.append({m_document,m_selected,m_rectangleDirections});
}

void SketchEditorDialog::trimSelected(bool extend)
{
    if(!m_drawingKind.isEmpty() || !m_coordinatesValid || !m_document.validationError().isEmpty())return;
    const auto before=m_document.toJson();SketchTrimDialog dialog(m_document,m_selected,extend,this);
    if(dialog.exec()!=QDialog::Accepted || m_document.toJson()!=before)return;
    const auto candidate=dialog.document();if(candidate.toJson()==before)return;
    rememberChange();m_document=candidate;m_selected=std::min(m_selected,m_document.contourCount()-1);refresh();fitDrawing();
}

void SketchEditorDialog::offsetSelected()
{
    if(!m_drawingKind.isEmpty() || !m_coordinatesValid || m_document.isEmpty() || !m_document.validationError().isEmpty())return;
    const auto before=m_document.toJson();SketchOffsetDialog dialog(m_document,m_selected,this);
    if(dialog.exec()!=QDialog::Accepted || m_document.toJson()!=before)return;
    const auto candidate=dialog.document();if(candidate.toJson()==before)return;
    rememberChange();m_selected=m_document.contourCount();m_document=candidate;refresh();fitDrawing();
}

void SketchEditorDialog::filletSelected()
{
    if(!m_drawingKind.isEmpty() || !m_coordinatesValid || m_document.isEmpty() || !m_document.validationError().isEmpty())return;
    const auto before=m_document.toJson();SketchFilletDialog dialog(m_document,m_selected,this);
    if(dialog.exec()!=QDialog::Accepted || m_document.toJson()!=before)return;
    const auto candidate=dialog.document();if(candidate.toJson()==before)return;
    rememberChange();m_document=candidate;m_selected=std::min(m_selected,m_document.contourCount()-1);refresh();fitDrawing();
}

void SketchEditorDialog::undoChange()
{
    if (m_history.isEmpty() || !m_drawingKind.isEmpty()) return;
    const auto previous=m_history.takeLast();
    m_document=previous.document;m_selected=previous.selected;m_rectangleDirections=previous.rectangleDirections;
    { const QSignalBlocker blocker(m_planeZ);m_planeZ->setText(QString::number(m_document.planeZ,'g',17)); }
    m_coordinatesValid=true;refresh();fitDrawing();
}

void SketchEditorDialog::transformSelected(const QString &operation)
{
    if (!m_drawingKind.isEmpty() || !m_coordinatesValid || m_document.isEmpty() ||
        !m_document.validationError().isEmpty()) return;
    QDialog dialog(this);dialog.setObjectName("sketchTransformDialog");
    const bool translation=operation=="move" || operation=="copy";
    dialog.setWindowTitle(operation=="move"?tr("移动图元"):operation=="copy"?tr("复制图元"):
                          operation=="rotate"?tr("旋转图元"):tr("镜像图元"));
    auto *layout=new QVBoxLayout(&dialog);
    auto *hint=new QLabel(translation?tr("输入当前图元的位移量（mm）。"):
        operation=="rotate"?tr("输入旋转中心（mm）和逆时针角度（°）。"):
        tr("输入镜像轴经过点（mm）及方向角（°，从 +X 逆时针计）。"),&dialog);
    hint->setWordWrap(true);layout->addWidget(hint);
    auto *form=new QFormLayout;
    auto *x=new QLineEdit("0",&dialog);x->setObjectName("sketchTransformX");
    auto *y=new QLineEdit("0",&dialog);y->setObjectName("sketchTransformY");
    form->addRow(translation?tr("ΔX（mm）"):operation=="rotate"?tr("中心 X（mm）"):tr("轴点 X（mm）"),x);
    form->addRow(translation?tr("ΔY（mm）"):operation=="rotate"?tr("中心 Y（mm）"):tr("轴点 Y（mm）"),y);
    QLineEdit *angle=nullptr;
    if (!translation) {
        angle=new QLineEdit(operation=="rotate"?"90":"0",&dialog);angle->setObjectName("sketchTransformAngle");
        form->addRow(tr("角度（°）"),angle);
    }
    layout->addLayout(form);
    auto *status=new QLabel(&dialog);status->setObjectName("sketchTransformStatus");status->setWordWrap(true);
    layout->addWidget(status);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("应用"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("applySketchTransformButton");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));layout->addWidget(buttons);
    const auto before=m_document.toJson();SketchDocument candidate=m_document;
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        bool validX=false,validY=false,validAngle=true;
        const double dx=x->text().toDouble(&validX),dy=y->text().toDouble(&validY);
        const double degrees=angle?angle->text().toDouble(&validAngle):0;
        if (!validX || !validY || !validAngle) { status->setText(tr("请输入有限数值。"));return; }
        candidate=m_document;
        const auto error=candidate.transformContour(m_selected,operation,{dx,dy},degrees);
        if (!error.isEmpty()) {status->setText(error);return;}
        dialog.accept();
    });
    if (dialog.exec()!=QDialog::Accepted || m_document.toJson()!=before) return;
    if (candidate.toJson()==before) return;
    rememberChange();m_document=candidate;
    if (operation=="copy") m_selected=m_document.contourCount()-1;
    refresh();fitDrawing();
}

void SketchEditorDialog::createPrimitive(const QString &kind)
{
    if (!m_drawingKind.isEmpty()) return;
    QDialog dialog(this);dialog.setObjectName("sketchPrimitiveDialog");
    dialog.setWindowTitle(kind=="line"?tr("绘制直线"):kind=="circle"?tr("绘制圆"):tr("绘制圆弧"));
    auto *layout=new QVBoxLayout(&dialog);
    auto *hint=new QLabel(kind=="line"?tr("输入首尾坐标（mm）。应用后替换当前图元，保存草图后生效。"):
        kind=="circle"?tr("输入圆心（mm）和半径（mm）。应用后替换当前图元，保存草图后生效。"):
        tr("输入圆心、半径及角度。起始角从 +X 逆时针计；正扫角逆时针，负扫角顺时针。应用后替换当前图元。"),&dialog);
    hint->setWordWrap(true);layout->addWidget(hint);
    auto *form=new QFormLayout;
    const auto field=[&](const QString &label,const char *name,const QString &initial) {
        auto *edit=new QLineEdit(initial,&dialog);edit->setObjectName(name);form->addRow(label,edit);return edit;
    };
    auto *x=field(kind=="line"?tr("首点 X（mm）"):tr("圆心 X（mm）"),"primitiveX","0");
    auto *y=field(kind=="line"?tr("首点 Y（mm）"):tr("圆心 Y（mm）"),"primitiveY","0");
    QLineEdit *endX=nullptr,*endY=nullptr,*radius=nullptr,*start=nullptr,*sweep=nullptr;
    if (kind=="line") {
        endX=field(tr("末点 X（mm）"),"primitiveEndX","10");endY=field(tr("末点 Y（mm）"),"primitiveEndY","0");
    } else {
        radius=field(tr("半径（mm）"),"primitiveRadius","10");
        if (kind=="arc") {
            start=field(tr("起始角（°）"),"primitiveStartAngle","0");sweep=field(tr("扫角（°）"),"primitiveSweepAngle","90");
        }
    }
    layout->addLayout(form);
    auto *status=new QLabel(&dialog);status->setObjectName("sketchPrimitiveStatus");status->setWordWrap(true);layout->addWidget(status);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("应用"));buttons->button(QDialogButtonBox::Ok)->setObjectName("applySketchPrimitiveButton");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));layout->addWidget(buttons);
    const auto before=m_document.toJson();SketchDocument candidate=m_document;
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        bool valid=true;
        const auto read=[&](QLineEdit *edit) {bool ok=false;const double value=edit->text().toDouble(&ok);valid &= ok && std::isfinite(value);return value;};
        SketchContour value;value.id=selectedContour().id;
        if (value.id.isEmpty()) value.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        value.kind=kind;const double px=read(x),py=read(y);value.points={{px,py}};
        if (kind=="line") {const double ex=read(endX),ey=read(endY);value.points.append({ex,ey});}
        else {value.radius=read(radius);value.startAngle=start?read(start):0;value.sweepAngle=sweep?read(sweep):360;}
        if (!valid) {status->setText(tr("请输入有限数值。"));return;}
        candidate=m_document;candidate.setContour(m_selected,value);
        const auto error=candidate.validationError();if (!error.isEmpty()) {status->setText(error);return;}
        dialog.accept();
    });
    if (dialog.exec()!=QDialog::Accepted || m_document.toJson()!=before || candidate.toJson()==before) return;
    rememberChange();m_document=candidate;refresh();fitDrawing();
}
