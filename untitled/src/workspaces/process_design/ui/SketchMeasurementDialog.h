#pragma once
#include "../../../core/SketchMeasurement.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

class SketchMeasurementDialog : public QDialog
{
public:
    explicit SketchMeasurementDialog(const SketchDocument &document,QWidget *parent=nullptr)
        : QDialog(parent),m_document(document)
    {
        setObjectName("sketchMeasurementDialog");setWindowTitle(tr("测量与标注"));resize(660,500);
        auto *layout=new QVBoxLayout(this);
        auto *hint=new QLabel(tr("选择真实图元的点或直线边。距离／半径单位为 mm；坐标为模型坐标，Z 为草图平面。角度为按顶点顺序的两条直线边的较小夹角（0–180°）。"),this);
        hint->setWordWrap(true);layout->addWidget(hint);
        auto *form=new QFormLayout;
        m_kind=new QComboBox(this);m_kind->setObjectName("measurementKind");
        m_kind->addItem(tr("距离"),"distance");m_kind->addItem(tr("角度"),"angle");
        m_kind->addItem(tr("半径"),"radius");m_kind->addItem(tr("坐标"),"coordinate");
        m_first=new QComboBox(this);m_first->setObjectName("measurementFirst");
        m_second=new QComboBox(this);m_second->setObjectName("measurementSecond");
        form->addRow(tr("测量类型"),m_kind);form->addRow(tr("第一项"),m_first);form->addRow(tr("第二项"),m_second);layout->addLayout(form);
        m_preview=new QLabel(this);m_preview->setObjectName("measurementPreview");m_preview->setWordWrap(true);
        m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(m_preview);
        auto *actions=new QHBoxLayout;
        m_add=new QPushButton(tr("添加标注"),this);m_add->setObjectName("addSketchAnnotationButton");
        m_update=new QPushButton(tr("更新选中标注"),this);m_update->setObjectName("updateSketchAnnotationButton");
        m_remove=new QPushButton(tr("删除选中标注"),this);m_remove->setObjectName("deleteSketchAnnotationButton");
        actions->addWidget(m_add);actions->addWidget(m_update);actions->addWidget(m_remove);layout->addLayout(actions);
        m_list=new QListWidget(this);m_list->setObjectName("measurementAnnotations");layout->addWidget(m_list,1);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("完成"));buttons->button(QDialogButtonBox::Ok)->setObjectName("finishSketchAnnotationsButton");
        buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
        connect(m_kind,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{populate();});
        for(auto *combo:{m_first,m_second})connect(combo,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{preview();});
        connect(m_add,&QPushButton::clicked,this,[this]{auto value=candidate();value.id=QUuid::createUuid().toString(QUuid::WithoutBraces);m_document.annotations.append(value);refreshList(m_document.annotations.size()-1);});
        connect(m_update,&QPushButton::clicked,this,[this]{const int row=m_list->currentRow();if(row<0)return;auto value=candidate();value.id=m_document.annotations[row].id;m_document.annotations[row]=value;refreshList(row);});
        connect(m_remove,&QPushButton::clicked,this,[this]{const int row=m_list->currentRow();if(row<0)return;m_document.annotations.removeAt(row);refreshList(-1);});
        connect(m_list,&QListWidget::currentRowChanged,this,[this](int row){
            if(row>=0) {
                const auto annotation=m_document.annotations[row];
                {const QSignalBlocker blocker(m_kind);m_kind->setCurrentIndex(m_kind->findData(annotation.kind));}
                populate();select(m_first,annotation.first);if(annotation.needsSecond())select(m_second,annotation.second);
            }
            preview();
        });
        populate();refreshList(-1);
    }
    QVector<SketchAnnotation> annotations() const { return m_document.annotations; }
private:
    SketchAnnotation candidate() const
    {
        SketchAnnotation result;result.id="61c99f91-29b7-4c13-a734-f24301e2cb6e";result.kind=m_kind->currentData().toString();
        SketchGeometryReference::read(m_first->currentData().toJsonObject(),&result.first);
        if(result.needsSecond())SketchGeometryReference::read(m_second->currentData().toJsonObject(),&result.second);
        return result;
    }
    void select(QComboBox *combo,const SketchGeometryReference &ref)
    {
        for(int i=0;i<combo->count();++i)if(combo->itemData(i).toJsonObject()==ref.toJson()){combo->setCurrentIndex(i);return;}
        combo->addItem(tr("失效引用（请重新选择）"),ref.toJson());combo->setCurrentIndex(combo->count()-1);
    }
    void populate()
    {
        const QSignalBlocker a(m_first),b(m_second);m_first->clear();m_second->clear();
        const auto kind=m_kind->currentData().toString();
        for(int i=0;i<m_document.contourCount();++i) {
            const auto c=m_document.contour(i);
            const auto add=[&](const QString &label,const QString &role,int index=0) {
                const auto data=SketchGeometryReference{c.id,role,index}.toJson();
                const auto type=c.kind=="line"?tr("直线"):c.kind=="circle"?tr("圆"):c.kind=="arc"?tr("圆弧"):c.kind=="rectangle"?tr("矩形"):tr("多段线");
                const auto text=tr("图元 %1（%2）· %3").arg(i+1).arg(type,label);m_first->addItem(text,data);m_second->addItem(text,data);
            };
            if(kind=="radius") {if(c.isCurve())add(c.kind=="circle"?tr("圆半径"):tr("圆弧半径"),"curve");}
            else if(kind=="angle") {
                if(!c.isCurve())for(int edge=0;edge<(c.kind=="line"?1:c.points.size());++edge)
                    add(tr("边 %1（点 %2 → 点 %3）").arg(edge+1).arg(edge+1).arg((edge+1)%c.points.size()+1),"edge",edge);
            } else if(c.isCurve()) {
                add(tr("圆心"),"center");add(c.kind=="circle"?tr("圆接缝起点"):tr("圆弧起点"),"start");
                if(c.kind=="arc")add(tr("圆弧终点"),"end");
            } else for(int point=0;point<c.points.size();++point)add(tr("顶点 %1").arg(point+1),"vertex",point);
        }
        m_second->setEnabled(kind=="distance" || kind=="angle");
        if(m_second->count()>1)m_second->setCurrentIndex(1);
        preview();
    }
    void preview()
    {
        const auto result=measureSketch(m_document,candidate());m_preview->setText(result.text);
        m_add->setEnabled(result.valid && m_document.annotations.size()<4096);
        m_update->setEnabled(result.valid && m_list->currentRow()>=0);m_remove->setEnabled(m_list->currentRow()>=0);
    }
    void refreshList(int row)
    {
        {const QSignalBlocker blocker(m_list);m_list->clear();
        for(int i=0;i<m_document.annotations.size();++i) {
            auto *item=new QListWidgetItem(tr("标注 %1 · %2").arg(i+1).arg(measureSketch(m_document,m_document.annotations[i]).text),m_list);
            item->setToolTip(item->text());
        }}
        m_list->setCurrentRow(row);preview();
    }
    SketchDocument m_document;
    QComboBox *m_kind,*m_first,*m_second;
    QLabel *m_preview;
    QPushButton *m_add,*m_update,*m_remove;
    QListWidget *m_list;
};
