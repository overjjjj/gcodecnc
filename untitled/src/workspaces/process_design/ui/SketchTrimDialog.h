#pragma once
#include "../../../core/SketchTrim.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

class SketchTrimDialog : public QDialog
{
public:
    SketchTrimDialog(const SketchDocument &document,int selected,bool extend,QWidget *parent=nullptr)
        : QDialog(parent),m_document(document),m_extend(extend)
    {
        setObjectName("sketchTrimDialog");setWindowTitle(extend?tr("延伸图元"):tr("修剪图元"));resize(800,640);
        auto *layout=new QVBoxLayout(this);
        auto *hint=new QLabel(extend?tr("选择开放直线或圆弧的端点，再选择延伸交点。边界按实际有限范围计算。"):
            tr("选择目标与边界，再选择要删除的段。矩形／闭合多段线修剪后会拆成独立直线；原目标标注将失效，可撤销恢复。"),this);
        hint->setWordWrap(true);layout->addWidget(hint);
        auto *form=new QFormLayout;
        const auto combo=[&](const QString &label,const char *name) {
            auto *box=new QComboBox(this);box->setObjectName(name);box->setMinimumContentsLength(30);
            box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);form->addRow(label,box);return box;
        };
        m_target=combo(tr("目标图元"),"trimTarget");m_boundary=combo(tr("边界图元"),"trimBoundary");
        m_edge=combo(tr("目标边"),"trimEdge");m_endpoint=combo(tr("延伸端点"),"trimEndpoint");
        m_endpoint->addItem(tr("起点 · 反向延伸"),0);m_endpoint->addItem(tr("终点 · 正向延伸"),1);m_endpoint->setCurrentIndex(1);
        m_choice=combo(extend?tr("延伸交点"):tr("删除段"),"trimChoice");
        layout->addLayout(form);
        form->labelForField(m_edge)->setVisible(!extend);m_edge->setVisible(!extend);
        form->labelForField(m_endpoint)->setVisible(extend);m_endpoint->setVisible(extend);
        m_scene=new QGraphicsScene(this);m_view=new QGraphicsView(m_scene,this);m_view->setObjectName("trimPreview");
        m_view->setAccessibleName(tr("修剪延伸结果预览"));m_view->setMinimumHeight(240);m_view->setRenderHint(QPainter::Antialiasing);
        layout->addWidget(m_view,1);
        auto *legend=new QLabel(tr("灰色虚线：原目标　橙色：边界　蓝色实线：确认后保留的结果　平面 Z = %1 mm").arg(document.planeZ,0,'g',17),this);
        legend->setWordWrap(true);layout->addWidget(legend);
        m_status=new QLabel(this);m_status->setObjectName("trimStatus");m_status->setWordWrap(true);layout->addWidget(m_status);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);
        m_apply=buttons->button(QDialogButtonBox::Ok);m_apply->setObjectName("applySketchTrimButton");m_apply->setText(tr("确认修改"));
        buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
        connect(buttons,&QDialogButtonBox::accepted,this,[this]{if(m_apply->isEnabled())accept();});
        for(int i=0;i<document.contourCount();++i) {
            const auto c=document.contour(i);
            if(!extend || c.kind=="line" || c.kind=="arc")m_target->addItem(contourLabel(i),i);
        }
        m_target->setCurrentIndex(std::max(0,m_target->findData(selected)));
        connect(m_target,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{targetChanged();});
        for(auto *box:{m_boundary,m_edge,m_endpoint})connect(box,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{recompute();});
        connect(m_choice,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{preview();});
        targetChanged();
    }
    SketchDocument document() const {return m_choice->currentIndex()<0?m_document:m_result.options[m_choice->currentIndex()].document;}
protected:
    void resizeEvent(QResizeEvent *event) override {QDialog::resizeEvent(event);QTimer::singleShot(0,this,[this]{fit();});}
    void showEvent(QShowEvent *event) override {QDialog::showEvent(event);QTimer::singleShot(0,this,[this]{fit();});}
private:
    QString contourLabel(int index) const
    {
        const auto kind=m_document.contour(index).kind;
        return tr("图元 %1 · %2").arg(index+1).arg(kind=="line"?tr("直线"):kind=="circle"?tr("圆"):kind=="arc"?tr("圆弧"):kind=="rectangle"?tr("矩形"):tr("闭合多段线"));
    }
    void targetChanged()
    {
        const QSignalBlocker b(m_boundary),e(m_edge);const int old=m_boundary->currentData().toInt();
        m_boundary->clear();m_edge->clear();
        const int target=m_target->currentIndex()<0?-1:m_target->currentData().toInt();
        for(int i=0;i<m_document.contourCount();++i)if(i!=target)m_boundary->addItem(contourLabel(i),i);
        m_boundary->setCurrentIndex(std::max(0,m_boundary->findData(old)));
        if(target>=0) {
            const auto edges=SketchTrimDetail::edges(m_document.contour(target));
            for(int i=0;i<edges.size();++i)m_edge->addItem(tr("边 %1 · %2 → %3").arg(i+1)
                .arg(SketchTrimDetail::pointText(SketchTrimDetail::at(edges[i],0)),SketchTrimDetail::pointText(SketchTrimDetail::at(edges[i],1))),i);
        }
        recompute();
    }
    void recompute()
    {
        const int target=m_target->currentIndex()<0?-1:m_target->currentData().toInt();
        const int boundary=m_boundary->currentIndex()<0?-1:m_boundary->currentData().toInt();
        m_result=sketchTrimOptions(m_document,target,boundary,m_edge->currentIndex(),m_extend,m_endpoint->currentData().toInt());
        {const QSignalBlocker blocker(m_choice);m_choice->clear();
        for(const auto &option:m_result.options){m_choice->addItem(option.label);m_choice->setItemData(m_choice->count()-1,option.label,Qt::ToolTipRole);}
        m_choice->setCurrentIndex(-1);}
        preview();
    }
    void preview()
    {
        m_scene->clear();const int chosen=m_choice->currentIndex();
        m_apply->setEnabled(chosen>=0);m_choice->setToolTip(chosen>=0?m_choice->currentText():QString());
        m_status->setText(!m_result.error.isEmpty()?m_result.error:chosen<0?tr("找到 %1 个结果，请明确选择%2后确认。").arg(m_result.options.size()).arg(m_extend?tr("交点"):tr("删除段")):
            m_choice->currentText()+tr("。确认后返回草图；保存草图后生效。"));
        const int target=m_target->currentIndex()<0?-1:m_target->currentData().toInt();
        const int boundary=m_boundary->currentIndex()<0?-1:m_boundary->currentData().toInt();
        const auto draw=[this](const SketchContour &c,const QColor &color,Qt::PenStyle style,int width) {
            QPainterPath path;const auto samples=c.displayPoints();
            for(int i=0;i<samples.size();++i){QPointF p(samples[i].x(),-samples[i].y());if(i==0)path.moveTo(p);else path.lineTo(p);}
            if(c.isClosed())path.closeSubpath();
            QPen pen(color,width,style);pen.setCosmetic(true);m_scene->addPath(path,pen);
        };
        for(int i=0;i<m_document.contourCount();++i)draw(m_document.contour(i),i==boundary?QColor("#9a4e00"):palette().color(QPalette::Mid),i==target?Qt::DashLine:Qt::SolidLine,i==boundary?2:1);
        if(chosen>=0) {
            const auto candidate=document();
            for(int i=0;i<candidate.contourCount();++i) {
                const auto c=candidate.contour(i);bool unchanged=false;
                for(int j=0;j<m_document.contourCount();++j)if(j!=target && c.id==m_document.contour(j).id)unchanged=true;
                if(!unchanged)draw(c,QColor("#1e40af"),Qt::SolidLine,2);
            }
        }
        auto bounds=m_scene->itemsBoundingRect();const auto margin=std::max(1.0,std::max(bounds.width(),bounds.height())*.08);
        m_scene->setSceneRect(bounds.adjusted(-margin,-margin,margin,margin));fit();
    }
    void fit(){if(m_view)m_view->fitInView(m_scene->sceneRect(),Qt::KeepAspectRatio);}
    SketchDocument m_document;
    bool m_extend;
    SketchTrimResult m_result;
    QComboBox *m_target,*m_boundary,*m_edge,*m_endpoint,*m_choice;
    QGraphicsScene *m_scene=nullptr;
    QGraphicsView *m_view=nullptr;
    QLabel *m_status;
    QPushButton *m_apply;
};
