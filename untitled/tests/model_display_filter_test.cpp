#include <QApplication>
#include <QPushButton>
#include <QToolButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QLabel>
#include <iostream>
#include "../src/import/StepImporter.h"
#define private public
#include "../src/app/MainWindow.h"
#include "../src/workspaces/shared/ui/ViewportWidget.h"
#include "../src/workspaces/process_design/ui/StrategyPanel.h"
#undef private
#include "../src/workspaces/process_design/ui/FeatureListPanel.h"
#include "../src/app/AppController.h"
#include "../src/workspaces/program_verification/fingerprint/ProgramSnapshotFingerprint.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    int failures=0;
    auto check=[&](bool ok,const char *message){std::cout<<(ok?"PASS ":"FAIL ")<<message<<std::endl;failures+=!ok;};
    QVector<MachiningFeature> features;
    for(auto kind:{FeatureKind::Hole,FeatureKind::Thread,FeatureKind::Slot,FeatureKind::Pocket,
                   FeatureKind::FlatSurface,FeatureKind::Boss,FeatureKind::Chamfer}){
        MachiningFeature feature;feature.kind=kind;feature.region=FaceRegion::Front;features.append(feature);
    }
    auto *panel=window.m_featurePanel;panel->setFeatures(features);
    check(panel->featureFilter()==FeatureDisplayFilter::All&&panel->visibleFeatureIndices().size()==7,"main window initially exposes all recognized feature kinds");
    const QStringList names={"All","Hole","Slot","Plane","Contour"};
    const QVector<QVector<int>> expected={{0,1,2,3,4,5,6},{0,1},{2,3},{4},{5,6}};
    for(int index=0;index<names.size();++index){
        auto *filter=panel->findChild<QToolButton*>("featureFilter"+names[index]);
        check(filter&&!filter->isHidden(),"existing feature filter is visible");
        if(filter)filter->click();
        check(panel->visibleFeatureIndices()==expected[index],"filter preserves original feature indices and category semantics");
    }
    MeshData mesh;mesh.bbMin={-10,-10,0};mesh.bbMax={10,10,0};
    Triangle tri;tri.v0={-10,-10,0};tri.v1={10,-10,0};tri.v2={10,10,0};tri.normal={0,0,1};tri.faceIndex=1;
    mesh.triangles.append(tri);tri.v1={10,10,0};tri.v2={-10,10,0};mesh.triangles.append(tri);
    MeshPickVertex vertex;vertex.id="vertex:1";vertex.point={-8,0,0};mesh.pickVertices.append(vertex);
    MeshPickEdge edge;edge.id="edge:1";edge.points={{-8,0,0},{8,0,0}};edge.parameters={0,1};mesh.pickEdges.append(edge);
    auto *view=window.m_viewport;view->resize(800,600);view->setMesh(mesh);
    view->setStandardView(ViewportWidget::StandardView::Top);
    auto project=[&](const QVector3D &point){const auto p=(view->m_proj*view->currentViewMatrix()).map(point);return QPoint(qRound((p.x()+1)*view->width()/2),qRound((1-p.y())*view->height()/2));};
    const auto center=project({0,0,0}),end=project(vertex.point);
    check(view->pickFaceAt(center)==1&&view->pickEdgeAt(center,nullptr,nullptr,nullptr)&&view->pickVertexAt(end,nullptr,nullptr),"visible model supports face edge and vertex picking");
    view->setToolPath({{0,0,5},{5,0,5}},{false,false});view->setToolPosition({5,0,5});view->setHighlightedFaces({1});
    view->setTopologyHoverPreview({{-8,0,0},{8,0,0}});
    const auto camera=view->currentViewMatrix();
    auto *toggle=window.findChild<QPushButton*>("modelVisibilityButton");
    check(toggle&&toggle->isCheckable()&&toggle->isChecked(),"model visibility has a checked native control");
    if(toggle){
        toggle->click();
        check(view->pickFaceAt(center)==0&&!view->pickEdgeAt(center,nullptr,nullptr,nullptr)&&!view->pickVertexAt(end,nullptr,nullptr),"hidden model cannot be picked in any topology mode");
        check(view->m_topologyHoverPoints.isEmpty(),"hiding model clears candidate hover");
        check(view->m_mesh.triangles.size()==2&&view->m_toolPath.size()==2&&view->m_hasToolPos&&view->currentViewMatrix()==camera,"hide preserves mesh toolpath tool marker and camera");
        toggle->click();
        check(view->pickFaceAt(center)==1&&view->pickEdgeAt(center,nullptr,nullptr,nullptr)&&view->pickVertexAt(end,nullptr,nullptr)&&view->m_highlightedFaces.contains(1),"restoring model restores picking and selected faces");
    }
    auto *bar=window.findChild<QWidget*>("modelViewBar");
    const QVector<QPair<float,float>> angles={{-90,0},{-90,180},{-90,-90},{-90,90},{0,0},{180,0},{-54.7356f,-45}};
    for(int index=0;index<8;++index){
        auto *button=bar->findChild<QPushButton*>("modelView_"+QString::number(index));
        check(button,"standard view has a stable real command");
        if(button){button->click();check(view->m_autoFit&&std::isfinite(view->m_zoom)&&view->m_zoom>0,"standard view and fit keep a usable camera");
            if(index<7)check(std::abs(view->m_rotX-angles[index].first)<.001&&std::abs(view->m_rotZ-angles[index].second)<.001,"standard view command selects the specified direction");}
    }
    if(toggle){
        auto *project=AppController::instance().projectManager();
        MachiningOperation op;op.id="display-stable";op.strategyId="hole_g81";op.params.set("depth",3);op.markToolpathValid();
        project->setOperations({op});
        const auto fingerprint=ProgramSnapshotFingerprint::calculate(project->operations(),{op.id});
        const QString saved=settings.filePath("display-project.json");check(project->saveToFile(saved),"save project before display-only changes");
        toggle->click();panel->setFeatureFilter(FeatureDisplayFilter::Plane);
        check(!project->isModified()&&project->operations().first().toolpathState==ToolpathState::Valid
            &&ProgramSnapshotFingerprint::calculate(project->operations(),{op.id})==fingerprint,"display preferences do not dirty project or invalidate machining");
        check(project->loadFromFile(saved),"reopen saved project");
        check(!toggle->isChecked()&&panel->featureFilter()==FeatureDisplayFilter::Plane,"same-window reload retains display preferences");
        MainWindow reopened;reopened.setAttribute(Qt::WA_DontShowOnScreen);
        auto *defaultToggle=reopened.findChild<QPushButton*>("modelVisibilityButton");
        check(defaultToggle&&defaultToggle->isChecked()&&reopened.m_featurePanel->featureFilter()==FeatureDisplayFilter::All,"new window defaults to visible model and all features");
    }
    AppController::instance().importStep(QString::fromUtf8("D:/codex/home/worktrees/5a59/gcodecnc/untitled/测试模型/槽.stp"));
    auto *realProject=AppController::instance().projectManager();
    check(!realProject->mesh().isEmpty()&&!realProject->features().isEmpty(),"real STEP import populates project and MainWindow");
    if(toggle){check(!toggle->isChecked(),"import preserves this window's hidden preference");toggle->click();}
    int nonHole=-1;
    for(int index=0;index<realProject->features().size();++index){
        const auto &feature=realProject->features()[index];
        if(feature.kind!=FeatureKind::Hole&&feature.kind!=FeatureKind::Thread&&!feature.faceIndices.isEmpty()){nonHole=index;break;}
    }
    check(nonHole>=0,"real imported model has a non-hole candidate");
    if(nonHole>=0){
        const auto feature=realProject->features()[nonHole];
        const QString filter=feature.kind==FeatureKind::FlatSurface?"Plane":
            (feature.kind==FeatureKind::Slot||feature.kind==FeatureKind::Pocket)?"Slot":"Contour";
        panel->findChild<QToolButton*>("featureFilter"+filter)->click();
        panel->selectFeature(nonHole);
        const auto faces=feature.faceIndices;
        check(panel->currentFeatureIndex()==nonHole&&view->m_highlightedFaces==QSet<int>(faces.cbegin(),faces.cend()),"filtered real non-hole selection maps original project index to exact face highlight");
        check(window.m_strategyPanel->m_hasContourFeature
            &&window.m_strategyPanel->m_contourFeature.faceIndices==faces
            &&window.m_strategyPanel->m_strategyCombo->count()>0
            &&window.m_strategyPanel->m_strategyCombo->currentData().toString().startsWith("mill_"),"filtered non-hole selection reaches milling strategy context");
    }
    return failures?1:0;
}
