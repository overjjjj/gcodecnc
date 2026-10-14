#pragma once

#include "../import/StepImporter.h"
#include "SketchAnnotation.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

enum class ProjectSourceType { Step, Sketch };

struct SketchContour
{
    QString id;
    QString kind;
    QVector<QPointF> points;
    double radius = 0;
    double startAngle = 0;
    double sweepAngle = 0;

    bool isCurve() const { return kind=="circle" || kind=="arc"; }
    bool isClosed() const { return kind=="rectangle" || kind=="polyline" || kind=="circle"; }
    double normalizedStart() const
    {
        return std::fmod(startAngle,360.0);
    }
    QPointF curvePoint(double degrees) const
    {
        const double angle=std::fmod(degrees,360.0);
        double cosine=std::cos(angle*std::acos(-1.0)/180),sine=std::sin(angle*std::acos(-1.0)/180);
        if (angle==0) {cosine=1;sine=0;}
        else if (angle==90 || angle==-270) {cosine=0;sine=1;}
        else if (angle==180 || angle==-180) {cosine=-1;sine=0;}
        else if (angle==270 || angle==-90) {cosine=0;sine=-1;}
        return {double(points[0].x()+static_cast<long double>(radius)*cosine),
                double(points[0].y()+static_cast<long double>(radius)*sine)};
    }
    // Display samples are never the source of analytic dimensions or machining permission.
    QVector<QPointF> displayPoints() const
    {
        if (!isCurve()) return points;
        if (points.size()!=1 || !std::isfinite(radius) || radius<=0 ||
            !std::isfinite(startAngle) || !std::isfinite(sweepAngle) || std::abs(sweepAngle)>360) return points;
        QVector<QPointF> result;
        for (int i=0;i<=64;++i) result.append(curvePoint(normalizedStart()+sweepAngle*i/64));
        if (kind=="circle") result.last()=result.first();
        return result;
    }
    QVector<QPointF> boundingPoints() const
    {
        if (!isCurve()) return points;
        if (points.size()!=1 || !std::isfinite(radius) || radius<=0 ||
            !std::isfinite(startAngle) || !std::isfinite(sweepAngle)) return points;
        const double start=normalizedStart();
        QVector<QPointF> result{curvePoint(start),curvePoint(start+sweepAngle)};
        for (int i=0;i<4;++i) {
            const double angle=i*90;
            double distance=std::fmod(sweepAngle>0?angle-start:start-angle,360.0);
            if (distance<0) distance+=360;
            if (kind=="circle" || distance<=std::abs(sweepAngle)) result.append(curvePoint(angle));
        }
        return result;
    }
    QJsonObject toJson() const
    {
        QJsonArray coordinates;
        for (const auto &point : points) coordinates.append(QJsonArray{point.x(),point.y()});
        QJsonObject result{{"id",id},{"kind",kind},{"points",coordinates}};
        if (isCurve()) result["curve"]=QJsonObject{{"radius",radius},{"startAngle",startAngle},{"sweepAngle",sweepAngle}};
        return result;
    }
};

struct SketchDocument
{
    QString id;
    int revision = 1;
    double planeZ = 0;
    QString contourId;
    QString contourKind;
    QVector<QPointF> points;

    double contourRadius = 0;
    double contourStartAngle = 0;
    double contourSweepAngle = 0;
    QVector<SketchContour> additionalContours;
    QVector<SketchAnnotation> annotations;
    int annotationRevision = 0;

    bool isEmpty() const { return points.isEmpty() && additionalContours.isEmpty(); }
    int contourCount() const { return points.isEmpty() ? 0 : 1 + additionalContours.size(); }
    SketchContour contour(int index) const
    {
        return index == 0 ? SketchContour{contourId,contourKind,points,contourRadius,contourStartAngle,contourSweepAngle} : additionalContours.at(index-1);
    }
    void setContour(int index, const SketchContour &value)
    {
        if (index == 0) {
            contourId=value.id;contourKind=value.kind;points=value.points;
            contourRadius=value.radius;contourStartAngle=value.startAngle;contourSweepAngle=value.sweepAngle;
        }
        else additionalContours[index-1]=value;
    }
    QVector<QPointF> allPoints() const
    {
        QVector<QPointF> result=points;
        for (const auto &value : additionalContours) result+=value.points;
        return result;
    }
    QVector<QPointF> boundingPoints() const
    {
        QVector<QPointF> result;
        for (int i=0;i<contourCount();++i) result+=contour(i).boundingPoints();
        return result;
    }
    int jsonVersion() const
    {
        for (int i=0;i<contourCount();++i)
            if (contour(i).kind!="rectangle" && contour(i).kind!="polyline") return 3;
        return additionalContours.isEmpty()?1:2;
    }
    void removeContour(int index)
    {
        if (index < 0 || index >= contourCount()) return;
        if (index > 0) additionalContours.removeAt(index-1);
        else if (!additionalContours.isEmpty()) {
            setContour(0,additionalContours.takeFirst());
        } else {
            contourId.clear();contourKind.clear();points.clear();
            contourRadius=contourStartAngle=contourSweepAngle=0;
        }
    }

    QString transformContour(int index, const QString &operation, const QPointF &reference, double degrees=0)
    {
        if (index < 0 || index >= contourCount() || !validationError().isEmpty())
            return QStringLiteral("请选择有效的草图图元。");
        if (!std::isfinite(reference.x()) || !std::isfinite(reference.y()) || !std::isfinite(degrees))
            return QStringLiteral("变换参数必须是有限数值。");
        if (operation!="move" && operation!="copy" && operation!="rotate" && operation!="mirror")
            return QStringLiteral("未知的草图变换。");
        SketchDocument candidate=*this;
        SketchContour value=contour(index);
        const double angle=std::fmod(degrees,operation=="mirror"?180.0:360.0);
        if (operation=="rotate" && angle==0) return {};
        const double radians=angle*std::acos(-1.0)/180.0;
        double cosine=std::cos(radians), sine=std::sin(radians);
        if (angle==0) { cosine=1;sine=0; }
        else if (angle==90 || angle==-270) { cosine=0;sine=1; }
        else if (angle==180 || angle==-180) { cosine=-1;sine=0; }
        else if (angle==270 || angle==-90) { cosine=0;sine=-1; }
        double reflectCos=std::cos(2*radians), reflectSin=std::sin(2*radians);
        if (std::fmod(angle,90.0)==0) { reflectCos=sine==0?1:-1;reflectSin=0; }
        for (auto &point : value.points) {
            if (operation=="move" || operation=="copy") point+=reference;
            else {
                const long double x=static_cast<long double>(point.x())-reference.x();
                const long double y=static_cast<long double>(point.y())-reference.y();
                point=operation=="rotate"
                    ? QPointF(double(reference.x()+cosine*x-sine*y),double(reference.y()+sine*x+cosine*y))
                    : QPointF(double(reference.x()+reflectCos*x+reflectSin*y),double(reference.y()+reflectSin*x-reflectCos*y));
            }
        }
        if (value.isCurve() && (operation=="rotate" || operation=="mirror")) {
            value.startAngle=std::fmod(operation=="rotate"?value.normalizedStart()+angle:2*angle-value.normalizedStart(),360.0);
            if (operation=="mirror") value.sweepAngle=-value.sweepAngle;
        }
        if (value.kind=="rectangle" && (operation=="rotate" || operation=="mirror") && std::fmod(angle,90.0)!=0)
            value.kind="polyline";
        if (operation=="copy") {
            value.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            candidate.additionalContours.append(value);
        } else candidate.setContour(index,value);
        const auto error=candidate.validationError();
        if (!error.isEmpty()) return error;
        *this=candidate;return {};
    }

    QString validationError() const
    {
        if(annotationRevision<0 || annotations.size()>4096)return QStringLiteral("标注修订或数量无效。");
        QSet<QUuid> annotationIds;
        for(const auto &annotation:annotations) {
            if(!annotation.valid() || annotationIds.contains(QUuid(annotation.id)))return QStringLiteral("标注类型、引用或身份无效。");
            annotationIds.insert(QUuid(annotation.id));
        }
        if (points.isEmpty() && !additionalContours.isEmpty())
            return QStringLiteral("多轮廓草图必须保留主轮廓。");
        QString error=contourValidationError();
        if (!error.isEmpty()) return error;
        QVector<QUuid> identities{QUuid(contourId)};
        int count=points.size();
        for (const auto &value : additionalContours) {
            count+=value.points.size();
            if (count>4096 || identities.contains(QUuid(value.id)) || value.points.isEmpty())
                return QStringLiteral("轮廓身份必须唯一，草图总顶点不能超过4096个。");
            identities.append(QUuid(value.id));
            SketchDocument single;
            single.id=id;single.revision=revision;single.planeZ=planeZ;
            single.setContour(0,value);
            error=single.contourValidationError();
            if (!error.isEmpty()) return error;
        }
        return {};
    }

    QString contourValidationError() const
    {
        if (QUuid(id).isNull() || revision < 1 || !std::isfinite(planeZ) || !std::isfinite(float(planeZ)))
            return QStringLiteral("草图身份、版本或平面无效。");
        if (points.isEmpty())
            return contourId.isEmpty() && contourKind.isEmpty() && contourRadius==0 && contourStartAngle==0 && contourSweepAngle==0 ? QString()
                : QStringLiteral("空草图不能保留轮廓身份。");
        if (contourKind=="line" || contourKind=="circle" || contourKind=="arc") {
            if (QUuid(contourId).isNull()) return QStringLiteral("草图图元身份无效。");
            for (const auto &point : points)
                if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
                    !std::isfinite(float(point.x())) || !std::isfinite(float(point.y())))
                    return QStringLiteral("图元坐标必须是有限且可显示的数值。");
            if (contourKind=="line") {
                if (points.size()!=2 || contourRadius!=0 || contourStartAngle!=0 || contourSweepAngle!=0 ||
                    points[0]==points[1] || QVector3D(points[0].x(),points[0].y(),planeZ)==QVector3D(points[1].x(),points[1].y(),planeZ))
                    return QStringLiteral("直线需要两个不同且可区分的端点，不能保留曲线参数。");
                return {};
            }
            if (points.size()!=1 || !std::isfinite(contourRadius) || contourRadius<=0 ||
                !std::isfinite(contourStartAngle) || !std::isfinite(contourSweepAngle) ||
                (contourKind=="circle"?std::abs(contourSweepAngle)!=360:contourSweepAngle==0 || std::abs(contourSweepAngle)>=360))
                return QStringLiteral("曲线需要一个圆心、正半径和有效扫角；圆为±360°，圆弧小于360°且不能为0。");
            const auto value=contour(0);
            const auto samples=value.displayPoints();
            QVector3D previous;
            for (int i=0;i<samples.size();++i) {
                const QVector3D point(samples[i].x(),samples[i].y(),planeZ);
                if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || (i>0 && point==previous))
                    return QStringLiteral("当前曲线在线框显示精度下退化，请减小坐标偏移或增大尺寸。");
                previous=point;
            }
            if (contourKind=="arc" && QVector3D(samples.first().x(),samples.first().y(),planeZ)==
                QVector3D(samples.last().x(),samples.last().y(),planeZ))
                return QStringLiteral("开放圆弧的两个端点必须在显示精度下不同。");
            for (const auto &point : value.boundingPoints())
                if (!std::isfinite(float(point.x())) || !std::isfinite(float(point.y())))
                    return QStringLiteral("曲线包围超出当前显示数值范围。");
            return {};
        }
        if (contourRadius!=0 || contourStartAngle!=0 || contourSweepAngle!=0)
            return QStringLiteral("直线闭合轮廓不能混入曲线参数。");
        if (QUuid(contourId).isNull() || (contourKind != "rectangle" && contourKind != "polyline") ||
            points.size() < 3 || points.size() > 4096)
            return QStringLiteral("草图需要有效身份及3至4096个闭合轮廓顶点。");
        if (contourKind == "rectangle" && points.size() != 4)
            return QStringLiteral("矩形必须具有四个顶点。");
        auto cross = [](const QPointF &a, const QPointF &b, const QPointF &c) {
            return (static_cast<long double>(b.x())-a.x())*(static_cast<long double>(c.y())-a.y())
                 - (static_cast<long double>(b.y())-a.y())*(static_cast<long double>(c.x())-a.x());
        };
        auto onSegment = [](const QPointF &p, const QPointF &a, const QPointF &b) {
            return p.x() >= std::min(a.x(),b.x()) && p.x() <= std::max(a.x(),b.x()) &&
                   p.y() >= std::min(a.y(),b.y()) && p.y() <= std::max(a.y(),b.y());
        };
        long double area = 0;
        for (int i=0;i<points.size();++i) {
            const auto &a=points[i];const auto &b=points[(i+1)%points.size()];
            if (!std::isfinite(a.x()) || !std::isfinite(a.y()) ||
                !std::isfinite(float(a.x())) || !std::isfinite(float(a.y())))
                return QStringLiteral("草图坐标必须是有限数值。");
            for (int j=0;j<i;++j) if (a==points[j])
                return QStringLiteral("闭合轮廓不能包含重复顶点。");
            if (contourKind=="rectangle" && ((a.x()==b.x()) == (a.y()==b.y())))
                return QStringLiteral("矩形边必须沿X或Y方向。");
            area += cross(points.first(),a,b);
        }
        if (!std::isfinite(area) || area==0)
            return QStringLiteral("闭合轮廓面积无效。");
        for (int i=0;i<points.size();++i) {
            const auto &a=points[i];const auto &b=points[(i+1)%points.size()];
            const auto &previous=points[(i+points.size()-1)%points.size()];
            if (cross(previous,a,b)==0 &&
                (static_cast<long double>(a.x())-previous.x())*(static_cast<long double>(b.x())-a.x())+
                (static_cast<long double>(a.y())-previous.y())*(static_cast<long double>(b.y())-a.y()) <= 0)
                return QStringLiteral("相邻轮廓边不能折返重叠。");
            for (int j=i+1;j<points.size();++j) {
                if (j==i+1 || (i==0 && j==points.size()-1)) continue;
                const auto &c=points[j];const auto &d=points[(j+1)%points.size()];
                const auto abC=cross(a,b,c),abD=cross(a,b,d),cdA=cross(c,d,a),cdB=cross(c,d,b);
                if (((abC>0 && abD<0)||(abC<0 && abD>0)) &&
                    ((cdA>0 && cdB<0)||(cdA<0 && cdB>0)))
                    return QStringLiteral("闭合轮廓不能自交。");
                if ((abC==0 && onSegment(c,a,b)) || (abD==0 && onSegment(d,a,b)) ||
                    (cdA==0 && onSegment(a,c,d)) || (cdB==0 && onSegment(b,c,d)))
                    return QStringLiteral("非相邻轮廓边不能接触或重叠。");
            }
        }
        // The document retains doubles; its display/picking wire must not lose topology.
        SketchDocument projected=*this;
        projected.planeZ=float(planeZ);
        bool converted=projected.planeZ!=planeZ;
        for (int i=0;i<points.size();++i) {
            projected.points[i]=QPointF(float(points[i].x()),float(points[i].y()));
            converted |= projected.points[i]!=points[i];
        }
        if (converted && !projected.contourValidationError().isEmpty())
            return QStringLiteral("当前坐标在线框显示精度下发生退化，请减小坐标偏移或增大边长。");
        return {};
    }

    QJsonObject toJson() const
    {
        const int version=jsonVersion();
        QJsonObject root{{"version",version},{"id",id},{"revision",revision},{"planeZ",planeZ}};
        root["contour"]=points.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(contour(0).toJson());
        if (version>=2) {
            QJsonArray copies;
            for (const auto &value : additionalContours) copies.append(value.toJson());
            root["additionalContours"]=copies;
        }
        if(!annotations.isEmpty() || annotationRevision>0) {
            QJsonArray items;for(const auto &annotation:annotations)items.append(annotation.toJson());
            root["annotations"]=QJsonObject{{"version",1},{"revision",annotationRevision},{"items",items}};
        }
        return root;
    }

    QString fingerprint() const
    {
        if (!validationError().isEmpty()) return {};
        auto geometry=toJson();geometry.remove("annotations");
        return QStringLiteral("sketch-v%1:").arg(jsonVersion())+QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument(geometry).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
    }

    static bool fromJson(const QJsonObject &root, SketchDocument *result, QString *error)
    {
        auto fail=[&](){*error=QStringLiteral("草图文件格式或版本无效。");return false;};
        if (!root["version"].isDouble() || (root["version"].toDouble()!=1 && root["version"].toDouble()!=2 && root["version"].toDouble()!=3) || !root["id"].isString() ||
            !root["revision"].isDouble() || root["revision"].toDouble()<1 ||
            root["revision"].toDouble()>std::numeric_limits<int>::max() ||
            root["revision"].toDouble()!=std::floor(root["revision"].toDouble()) || !root["planeZ"].isDouble()) return fail();
        SketchDocument candidate;
        candidate.id=root["id"].toString();candidate.revision=root["revision"].toInt();candidate.planeZ=root["planeZ"].toDouble();
        const int version=root["version"].toInt();
        int count=0;
        const auto readContour=[&](const QJsonValue &value,SketchContour *result) {
            if (!value.isObject()) return false;
            const auto object=value.toObject();
            if (!object["id"].isString() || !object["kind"].isString() || !object["points"].isArray()) return false;
            SketchContour parsed{object["id"].toString(),object["kind"].toString(),{}};
            count+=object["points"].toArray().size();
            if (count>4096) return false;
            for (const auto &coordinate : object["points"].toArray()) {
                if (!coordinate.isArray()) return false;
                const auto point=coordinate.toArray();
                if (point.size()!=2 || !point[0].isDouble() || !point[1].isDouble()) return false;
                parsed.points.append(QPointF(point[0].toDouble(),point[1].toDouble()));
            }
            if (parsed.isCurve()) {
                if (version!=3 || !object["curve"].isObject()) return false;
                const auto curve=object["curve"].toObject();
                if (!curve["radius"].isDouble() || !curve["startAngle"].isDouble() || !curve["sweepAngle"].isDouble()) return false;
                parsed.radius=curve["radius"].toDouble();parsed.startAngle=curve["startAngle"].toDouble();parsed.sweepAngle=curve["sweepAngle"].toDouble();
            } else if (object.contains("curve") || (parsed.kind=="line" && version!=3)) return false;
            *result=parsed;return true;
        };
        if (!root.contains("contour")) return fail();
        if (!root["contour"].isNull()) {
            SketchContour primary;
            if (!readContour(root["contour"],&primary)) return fail();
            candidate.setContour(0,primary);
        }
        if (version==1 && root.contains("additionalContours")) return fail();
        if (version>=2) {
            if (!root["additionalContours"].isArray() || root["additionalContours"].toArray().size()>4096) return fail();
            for (const auto &value : root["additionalContours"].toArray()) {
                SketchContour copy;
                if (!readContour(value,&copy)) return fail();
                candidate.additionalContours.append(copy);
            }
        }
        if(root.contains("annotations")) {
            if(!root["annotations"].isObject())return fail();
            const auto block=root["annotations"].toObject();const auto revision=block["revision"];
            if(!block["version"].isDouble() || block["version"].toDouble()!=1 || !revision.isDouble() ||
               revision.toDouble()<0 || revision.toDouble()>std::numeric_limits<int>::max() ||
               revision.toDouble()!=std::floor(revision.toDouble()) || !block["items"].isArray() || block["items"].toArray().size()>4096)return fail();
            candidate.annotationRevision=revision.toInt();
            for(const auto &item:block["items"].toArray()) {
                SketchAnnotation annotation;if(!SketchAnnotation::read(item,&annotation))return fail();
                candidate.annotations.append(annotation);
            }
        }
        *error=candidate.validationError();
        if (!error->isEmpty()) return false;
        *result=candidate;return true;
    }

    MeshData wireGeometry() const
    {
        MeshData mesh;
        if (points.isEmpty() || !validationError().isEmpty()) return mesh;
        const auto bounds=boundingPoints();
        mesh.bbMin=mesh.bbMax=QVector3D(bounds.first().x(),bounds.first().y(),planeZ);
        for (const auto &point : bounds) {
            mesh.bbMin.setX(std::min(mesh.bbMin.x(),float(point.x())));mesh.bbMax.setX(std::max(mesh.bbMax.x(),float(point.x())));
            mesh.bbMin.setY(std::min(mesh.bbMin.y(),float(point.y())));mesh.bbMax.setY(std::max(mesh.bbMax.y(),float(point.y())));
        }
        for (int contourIndex=0;contourIndex<contourCount();++contourIndex) {
        const auto value=contour(contourIndex);
        const auto &points=value.points;
        const QString prefix="sketch:"+id+":"+value.id;
        MeshBoundaryLoop loop;loop.faceId=prefix+":loop";loop.outer=true;
        if (value.kind=="line" || value.isCurve()) {
            const auto samples=value.displayPoints();
            MeshPickEdge edge;edge.id=prefix+":edge:0";edge.closed=value.kind=="circle";
            edge.firstVertexId=prefix+":vertex:0";edge.lastVertexId=prefix+(edge.closed?":vertex:0":":vertex:1");
            for (int i=0;i<samples.size();++i) {
                edge.points.append(QVector3D(samples[i].x(),samples[i].y(),planeZ));
                edge.parameters.append(value.isCurve()?(value.normalizedStart()+value.sweepAngle*i/64)*std::acos(-1.0)/180:i);
            }
            mesh.pickEdges.append(edge);
            MeshPickVertex first;first.id=edge.firstVertexId;first.point=edge.points.first();first.incidentEdgeIds=QStringList{edge.id};
            mesh.pickVertices.append(first);
            if (!edge.closed) {MeshPickVertex last=first;last.id=edge.lastVertexId;last.point=edge.points.last();mesh.pickVertices.append(last);}
            else {loop.orderedEdgeIds.append(edge.id);loop.orderedForward.append(true);mesh.boundaryLoops.append(loop);}
            continue;
        }
        for (int i=0;i<points.size();++i) {
            const QVector3D point(points[i].x(),points[i].y(),planeZ);
            const int next=(i+1)%points.size();
            MeshPickEdge edge;edge.id=prefix+":edge:"+QString::number(i);
            edge.firstVertexId=prefix+":vertex:"+QString::number(i);
            edge.lastVertexId=prefix+":vertex:"+QString::number(next);
            edge.points={point,QVector3D(points[next].x(),points[next].y(),planeZ)};edge.parameters={0,1};
            mesh.pickEdges.append(edge);loop.orderedEdgeIds.append(edge.id);loop.orderedForward.append(true);
            MeshPickVertex vertex;vertex.id=edge.firstVertexId;vertex.point=point;
            vertex.incidentEdgeIds=QStringList{prefix+":edge:"+QString::number((i+points.size()-1)%points.size()),edge.id};
            mesh.pickVertices.append(vertex);
        }
        mesh.boundaryLoops.append(loop);
        }
        return mesh;
    }
};
