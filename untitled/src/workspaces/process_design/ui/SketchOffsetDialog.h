#pragma once
#include "../../../core/SketchOffset.h"
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

class SketchOffsetDialog : public QDialog
{
public:
    SketchOffsetDialog(const SketchDocument &document,int selected,QWidget *parent=nullptr)
        : QDialog(parent),m_document(document),m_candidate(document)
    {
        setObjectName("sketchOffsetDialog");setWindowTitle(tr("偏移副本"));resize(800,640);
        auto *layout=new QVBoxLayout(this);
        auto *hint=new QLabel(tr("保留原图元和标注，创建等距副本。闭合轮廓采用尖角连接；内缩分裂时会创建多个独立轮廓。"),this);
        hint->setWordWrap(true);layout->addWidget(hint);
        auto *form=new QFormLayout;
        m_target=new QComboBox(this);m_target->setObjectName("offsetTarget");
        for(int i=0;i<document.contourCount();++i) {
            const auto kind=document.contour(i).kind;
            m_target->addItem(tr("图元 %1 · %2").arg(i+1).arg(kind=="line"?tr("直线"):kind=="circle"?tr("圆"):kind=="arc"?tr("圆弧"):kind=="rectangle"?tr("矩形"):tr("闭合多段线")),i);
        }
        m_target->setCurrentIndex(selected);form->addRow(tr("目标图元"),m_target);
        m_distance=new QLineEdit("1",this);m_distance->setObjectName("offsetDistance");form->addRow(tr("偏移距离（mm）"),m_distance);
        m_side=new QComboBox(this);m_side->setObjectName("offsetSide");form->addRow(tr("偏移侧别"),m_side);layout->addLayout(form);
        auto *preview=new QPushButton(tr("预览偏移"),this);preview->setObjectName("previewSketchOffsetButton");layout->addWidget(preview);
        m_scene=new QGraphicsScene(this);m_view=new QGraphicsView(m_scene,this);m_view->setObjectName("offsetPreview");
        m_view->setAccessibleName(tr("偏移原件与全部副本预览"));m_view->setMinimumHeight(240);m_view->setRenderHint(QPainter::Antialiasing);layout->addWidget(m_view,1);
        auto *legend=new QLabel(tr("灰色虚线：原图元　蓝色实线：新增副本　平面 Z = %1 mm").arg(document.planeZ,0,'g',17),this);
        legend->setWordWrap(true);layout->addWidget(legend);
        m_status=new QLabel(this);m_status->setObjectName("offsetStatus");m_status->setWordWrap(true);layout->addWidget(m_status);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);
        m_apply=buttons->button(QDialogButtonBox::Ok);m_apply->setObjectName("applySketchOffsetButton");m_apply->setText(tr("确认创建副本"));m_apply->setAutoDefault(false);
        buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
        connect(buttons,&QDialogButtonBox::accepted,this,[this]{if(m_apply->isEnabled())accept();});
        connect(m_target,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{targetChanged();});
        connect(m_side,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{invalidate();});
        connect(m_distance,&QLineEdit::textChanged,this,[this]{invalidate();});
        connect(preview,&QPushButton::clicked,this,[this]{compute();});targetChanged();
    }
    SketchDocument document() const {return m_candidate;}
protected:
    void resizeEvent(QResizeEvent *event) override {QDialog::resizeEvent(event);QTimer::singleShot(0,this,[this]{fit();});}
    void showEvent(QShowEvent *event) override {QDialog::showEvent(event);QTimer::singleShot(0,this,[this]{fit();});}
private:
    void targetChanged()
    {
        const QSignalBlocker blocker(m_side);m_side->clear();
        if(m_target->currentIndex()>=0 && m_document.contour(m_target->currentData().toInt()).kind=="line") {
            m_side->addItem(tr("左侧 · 沿首点至末点"),"left");m_side->addItem(tr("右侧 · 沿首点至末点"),"right");
        } else {m_side->addItem(tr("向内"),"inside");m_side->addItem(tr("向外"),"outside");}
        invalidate();
    }
    void invalidate()
    {
        m_candidate=m_document;m_apply->setEnabled(false);m_status->setText(tr("请预览当前参数，检查结果后确认。"));draw();
    }
    void compute()
    {
        invalidate();bool ok=false;const double distance=m_distance->text().toDouble(&ok);
        if(!ok){m_status->setText(tr("请输入有限正数距离（mm）。"));return;}
        const auto result=sketchOffset(m_document,m_target->currentIndex()<0?-1:m_target->currentData().toInt(),distance,m_side->currentData().toString());
        if(!result.error.isEmpty()){m_status->setText(result.error);return;}
        m_candidate=result.document;m_apply->setEnabled(true);
        m_status->setText(tr("将创建 %1 个独立副本，保留原件。确认后返回草图，保存草图后生效。").arg(m_candidate.contourCount()-m_document.contourCount()));draw();
    }
    void draw()
    {
        m_scene->clear();
        for(int j=0;j<m_candidate.contourCount();++j) {
            const auto c=m_candidate.contour(j);QPainterPath path;const auto points=c.displayPoints();
            for(int i=0;i<points.size();++i){const QPointF p(points[i].x(),-points[i].y());if(i==0)path.moveTo(p);else path.lineTo(p);}
            if(c.isClosed())path.closeSubpath();
            const bool added=j>=m_document.contourCount();
            QPen pen(added?QColor("#1e40af"):palette().color(QPalette::Mid),added?2:1,added?Qt::SolidLine:Qt::DashLine);pen.setCosmetic(true);m_scene->addPath(path,pen);
        }
        const auto bounds=m_scene->itemsBoundingRect();const auto margin=std::max(1.0,std::max(bounds.width(),bounds.height())*.08);
        m_scene->setSceneRect(bounds.adjusted(-margin,-margin,margin,margin));fit();
    }
    void fit(){m_view->fitInView(m_scene->sceneRect(),Qt::KeepAspectRatio);}
    SketchDocument m_document,m_candidate;
    QComboBox *m_target,*m_side;
    QLineEdit *m_distance;
    QGraphicsScene *m_scene;
    QGraphicsView *m_view;
    QLabel *m_status;
    QPushButton *m_apply;
};
