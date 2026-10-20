#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QDir>
#include <QImage>
#include <iostream>
#include "src/import/StepImporter.h"
#define private public
#include "src/workspaces/shared/ui/ViewportWidget.h"
#undef private
int main(int argc,char **argv) {
 QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
 QApplication app(argc,argv);QTemporaryDir settings;
 QSettings::setDefaultFormat(QSettings::IniFormat);
 QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
 if(argc!=2)return 2;const QString out=QString::fromLocal8Bit(argv[1]);QDir().mkpath(out);
 StepImporter importer;
 if(!importer.load(QString::fromUtf8("D:/codex/home/worktrees/5a59/gcodecnc/untitled/测试模型/验收/特征板.STEP")))return 3;
 ViewportWidget view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(1000,760);
 view.setMesh(importer.mesh());view.setStandardView(ViewportWidget::StandardView::Top);view.fitAll();view.show();app.processEvents();
 auto base=view.grabFramebuffer();if(base.isNull())return 4;base.save(out+"/base.png");
 QStringList edges;for(const auto &e:importer.mesh().pickEdges)edges.append(e.id);
 view.setTopologyPickFilter(edges,{},{});app.processEvents();
 auto scoped=view.grabFramebuffer();scoped.save(out+"/scope.png");
 const bool noGlobalOverlay=base==scoped;
 const bool stillPickable=view.m_topologyPickFilterActive && view.m_selectableTopologyEdges.size()==edges.size();
 QVector<QVector3D> selected;
 for(const auto &e:importer.mesh().pickEdges)if(e.id=="edge:452")selected=e.points;
 if(selected.size()<2)return 5;
 view.setTopologyHoverPreview(selected);app.processEvents();auto hovered=view.grabFramebuffer();hovered.save(out+"/hover.png");
 const bool localHover=hovered!=scoped && view.m_confirmedContourPoints.isEmpty() && view.m_topologyHoverPoints==selected;
 view.setTopologyHoverPreview({});app.processEvents();const bool leaveRestores=view.grabFramebuffer()==scoped;
 view.setConfirmedContourPreview(selected,false);app.processEvents();auto confirmed=view.grabFramebuffer();confirmed.save(out+"/confirmed.png");
 const bool confirmedVisible=confirmed!=scoped && view.m_confirmedContourPoints==selected;
 for(auto item:{std::make_pair(noGlobalOverlay,"scope does not draw every selectable edge"),
                std::make_pair(stillPickable,"all source edges remain pickable"),
                std::make_pair(localHover,"hover displays only local edge without confirmation"),
                std::make_pair(leaveRestores,"leaving restores original appearance"),
                std::make_pair(confirmedVisible,"confirmed edge remains visible")})
  std::cout<<(item.first?"PASS ":"FAIL ")<<item.second<<std::endl;
 return noGlobalOverlay && stillPickable && localHover && leaveRestores && confirmedVisible?0:1;
}
