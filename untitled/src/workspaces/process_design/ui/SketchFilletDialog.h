#pragma once
#include "../../../core/SketchFillet.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

class SketchFilletDialog : public QDialog
{
public:
    SketchFilletDialog(const SketchDocument &document,int selected,QWidget *parent=nullptr)
        : QDialog(parent),m_document(document),m_candidate(document)
    {
        setObjectName("sketchFilletDialog");setWindowTitle(tr("直线角点圆角"));resize(800,640);
        auto *layout=new QVBoxLayout(this);
        auto *hint=new QLabel(tr("修剪邻边并创建相切圆弧。闭合轮廓将拆成独立直线和圆弧；被替换图元的旧标注会失效，可撤销恢复。两条独立直线必须有相同端点。"),this);
        hint->setWordWrap(true);layout->addWidget(hint);
        m_form=new QFormLayout;
        const auto combo=[&](const QString &label,const char *name){auto *box=new QComboBox(this);box->setObjectName(name);box->setMinimumContentsLength(30);box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);m_form->addRow(label,box);return box;};
        m_target=combo(tr("目标图元"),"filletTarget");m_corner=combo(tr("圆角顶点"),"filletCorner");m_second=combo(tr("第二条直线"),"filletSecond");
        for(int i=0;i<document.contourCount();++i)if(!document.contour(i).isCurve())m_target->addItem(contourLabel(i),i);
        m_target->setCurrentIndex(std::max(0,m_target->findData(selected)));
        m_radius=new QLineEdit("1",this);m_radius->setObjectName("filletRadius");m_form->addRow(tr("圆角半径（mm）"),m_radius);layout->addLayout(m_form);
        auto *preview=new QPushButton(tr("预览圆角"),this);preview->setObjectName("previewSketchFilletButton");layout->addWidget(preview);
        m_scene=new QGraphicsScene(this);m_view=new QGraphicsView(m_scene,this);m_view->setObjectName("filletPreview");
        m_view->setAccessibleName(tr("原图元与圆角修剪结果预览"));m_view->setMinimumHeight(240);m_view->setRenderHint(QPainter::Antialiasing);layout->addWidget(m_view,1);
        auto *legend=new QLabel(tr("灰色虚线：原图元　蓝色实线：确认后结果　平面 Z = %1 mm").arg(document.planeZ,0,'g',17),this);
        legend->setWordWrap(true);layout->addWidget(legend);
        m_status=new QLabel(this);m_status->setObjectName("filletStatus");m_status->setWordWrap(true);layout->addWidget(m_status);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);
        m_apply=buttons->button(QDialogButtonBox::Ok);m_apply->setObjectName("applySketchFilletButton");m_apply->setText(tr("确认修剪并圆角"));m_apply->setAutoDefault(false);
        buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
        connect(buttons,&QDialogButtonBox::accepted,this,[this]{if(m_apply->isEnabled())accept();});
        connect(m_target,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{targetChanged();});
        for(auto *box:{m_corner,m_second})connect(box,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{invalidate();});
        connect(m_radius,&QLineEdit::textChanged,this,[this]{invalidate();});
        connect(preview,&QPushButton::clicked,this,[this]{compute();});targetChanged();
    }
    SketchDocument document() const {return m_candidate;}
protected:
    void resizeEvent(QResizeEvent *event) override {QDialog::resizeEvent(event);QTimer::singleShot(0,this,[this]{fit();});}
    void showEvent(QShowEvent *event) override {QDialog::showEvent(event);QTimer::singleShot(0,this,[this]{fit();});}
private:
    QString contourLabel(int i) const {const auto kind=m_document.contour(i).kind;return tr("图元 %1 · %2").arg(i+1).arg(kind=="line"?tr("直线"):kind=="rectangle"?tr("矩形"):tr("闭合多段线"));}
    void targetChanged()
    {
        const QSignalBlocker a(m_corner),b(m_second);m_corner->clear();m_second->clear();
        const int target=m_target->currentIndex()<0?-1:m_target->currentData().toInt();
        const bool line=target>=0 && m_document.contour(target).kind=="line";
        m_corner->setVisible(!line);m_form->labelForField(m_corner)->setVisible(!line);
        m_second->setVisible(line);m_form->labelForField(m_second)->setVisible(line);
        if(target>=0 && !line) {
            const auto points=m_document.contour(target).points;
            for(int i=0;i<points.size();++i){const auto text=tr("顶点 %1 · %2 mm").arg(i+1).arg(SketchTrimDetail::pointText(points[i]));m_corner->addItem(text,i);m_corner->setItemData(i,text,Qt::ToolTipRole);}
        }
        for(int i=0;i<m_document.contourCount();++i)if(i!=target && m_document.contour(i).kind=="line")m_second->addItem(contourLabel(i),i);
        invalidate();
    }
    void invalidate(){m_candidate=m_document;m_apply->setEnabled(false);m_status->setText(tr("请预览当前角点与半径，检查修剪结果后确认。"));draw();}
    void compute()
    {
        invalidate();bool ok=false;const double radius=m_radius->text().toDouble(&ok);
        if(!ok){m_status->setText(tr("请输入有限正数半径（mm）。"));return;}
        const auto result=sketchFillet(m_document,m_target->currentIndex()<0?-1:m_target->currentData().toInt(),m_corner->currentIndex(),m_second->currentIndex()<0?-1:m_second->currentData().toInt(),radius);
        if(!result.error.isEmpty()){m_status->setText(result.error);return;}
        m_candidate=result.document;m_apply->setEnabled(true);m_status->setText(tr("将替换所选图元并创建相切圆弧。各段独立保存；确认后返回草图，保存草图后生效。"));draw();
    }
    void draw()
    {
        m_scene->clear();const auto drawContour=[this](const SketchContour &c,bool result){QPainterPath path;const auto points=c.displayPoints();
            for(int i=0;i<points.size();++i){const QPointF p(points[i].x(),-points[i].y());if(i==0)path.moveTo(p);else path.lineTo(p);}
            if(c.isClosed())path.closeSubpath();
            QPen pen(result?QColor("#1e40af"):palette().color(QPalette::Mid),result?2:1,result?Qt::SolidLine:Qt::DashLine);pen.setCosmetic(true);m_scene->addPath(path,pen);};
        for(int i=0;i<m_document.contourCount();++i)drawContour(m_document.contour(i),false);
        if(m_apply->isEnabled())for(int i=0;i<m_candidate.contourCount();++i)drawContour(m_candidate.contour(i),true);
        const auto bounds=m_scene->itemsBoundingRect();const auto margin=std::max(1.0,std::max(bounds.width(),bounds.height())*.08);
        m_scene->setSceneRect(bounds.adjusted(-margin,-margin,margin,margin));fit();
    }
    void fit(){m_view->fitInView(m_scene->sceneRect(),Qt::KeepAspectRatio);}
    SketchDocument m_document,m_candidate;
    QFormLayout *m_form;
    QComboBox *m_target,*m_corner,*m_second;
    QLineEdit *m_radius;
    QGraphicsScene *m_scene;
    QGraphicsView *m_view;
    QLabel *m_status;
    QPushButton *m_apply;
};
