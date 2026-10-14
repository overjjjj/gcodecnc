#pragma once
#include <BRepAdaptor_Curve.hxx>
#include <QVector3D>
#include <QVector>
#include <QString>
#include <GeomConvert.hxx>
#include <GeomConvert_BSplineCurveToBezierCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <Standard_Failure.hxx>
#include <Precision.hxx>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

struct BoundedCurveResult {
    QVector<QVector3D> points;
    QString error;
    double computedBound=0;
    double floatDisplacement=0;
    double conversionEndpointDisplacement=0;
    int continuityIntervals=0;
};

// Positive-weight Bezier control hulls bound the whole subarc's
// distance to its finite endpoint chord. Endpoint quantization is charged too.
// OCC conversion/subdivision and these distances use doubles: this is a numeric
// geometric bound, not a directed-rounding certificate or machining permission.
// first -> last explicitly specifies traversal; edge.Orientation() is not
// applied here. The caller must resolve wire traversal once before calling.
inline BoundedCurveResult discretizeCurve(const BRepAdaptor_Curve &curve,double first,double last,double tolerance) {
    BoundedCurveResult result;
    auto fail=[](const char *error){BoundedCurveResult failed;failed.error=error;return failed;};
    if(!std::isfinite(tolerance) || tolerance<16*Precision::Confusion())return fail("CURVE_BUDGET_INVALID");
    if(!std::isfinite(first) || !std::isfinite(last) || first==last)return fail("CURVE_INTERVAL_INVALID");
    try {
        if(!curve.Is3DCurve())return fail("CURVE_3D_REQUIRED");
        const auto type=curve.GetType();
        if(type!=GeomAbs_Line && type!=GeomAbs_Circle && type!=GeomAbs_Ellipse &&
           type!=GeomAbs_BezierCurve && type!=GeomAbs_BSplineCurve)return fail("CURVE_TYPE_UNSUPPORTED");
        const double lo=std::min(first,last),hi=std::max(first,last);
        if(lo<curve.FirstParameter() || hi>curve.LastParameter())return fail("CURVE_INTERVAL_INVALID");
        if(curve.IsPeriodic() && (type==GeomAbs_BSplineCurve || hi-lo>curve.Period()))
            return fail("CURVE_PERIOD_UNSUPPORTED");
        QVector<Handle(Geom_BezierCurve)> arcs;
        if(type==GeomAbs_BSplineCurve) {
            // BSpline() applies the edge placement. Decompose before trimming:
            // BSpline::Segment's knot tolerance must not swallow a short span.
            GeomConvert_BSplineCurveToBezierCurve split(curve.BSpline());
            if(split.NbArcs()<1 || split.NbArcs()>4096)return fail("CURVE_INTERVAL_LIMIT");
            TColStd_Array1OfReal knots(1,split.NbArcs()+1);split.Knots(knots);
            double covered=lo;
            for(int i=1;i<=split.NbArcs();++i) {
                const double a=std::max(lo,knots(i)),b=std::min(hi,knots(i+1));
                if(b<=a)continue;
                if(a!=covered || knots(i+1)<=knots(i))return fail("CURVE_INTERVAL_GAP");
                auto arc=split.Arc(i);
                arc->Segment((a-knots(i))/(knots(i+1)-knots(i)),(b-knots(i))/(knots(i+1)-knots(i)));
                arcs.append(arc);covered=b;
            }
            if(covered!=hi)return fail("CURVE_INTERVAL_INCOMPLETE");
        } else if(type==GeomAbs_BezierCurve) {
            auto arc=curve.Bezier();arc->Segment(lo,hi);arcs.append(arc);
        } else {
            // Conic conversion changes parameterization. Only its complete
            // converted spans are consumed, never original angular parameters.
            Handle(Geom_TrimmedCurve) trimmed=new Geom_TrimmedCurve(curve.Curve().Curve(),lo,hi,true,false);
            GeomConvert_BSplineCurveToBezierCurve split(GeomConvert::CurveToBSplineCurve(trimmed));
            if(split.NbArcs()<1 || split.NbArcs()>4096)return fail("CURVE_INTERVAL_LIMIT");
            for(int i=1;i<=split.NbArcs();++i){auto arc=split.Arc(i);arc->Transform(curve.Trsf());arcs.append(arc);}
        }
        if(arcs.isEmpty())return fail("CURVE_INTERVAL_INCOMPLETE");
        const gp_Pnt start=curve.Value(lo),end=curve.Value(hi);
        const double endpointScale=std::max({1.,std::abs(start.X()),std::abs(start.Y()),std::abs(start.Z()),
                                            std::abs(end.X()),std::abs(end.Y()),std::abs(end.Z())});
        result.conversionEndpointDisplacement=std::max(start.Distance(arcs.first()->Pole(1)),
            end.Distance(arcs.last()->Pole(arcs.last()->NbPoles())));
        if(!std::isfinite(result.conversionEndpointDisplacement) || result.conversionEndpointDisplacement>
           std::max(Precision::Confusion(),64*std::numeric_limits<double>::epsilon()*endpointScale))
            return fail("CURVE_CONVERSION_ENDPOINT_MISMATCH");
        QString error;
        auto stored=[](const gp_Pnt &p){return QVector3D(float(p.X()),float(p.Y()),float(p.Z()));};
        auto point=[](const QVector3D &p){return gp_Pnt(p.x(),p.y(),p.z());};
        gp_Pnt previousEnd;bool hasPrevious=false;
        std::function<bool(const Handle(Geom_BezierCurve)&,int)> append;
        append=[&](const Handle(Geom_BezierCurve)& arc,int depth) {
            const gp_Pnt a=arc->Pole(1),b=arc->Pole(arc->NbPoles());
            // Reuse the previous stored junction explicitly and charge its full
            // displacement from this subarc's start to the same error budget.
            const auto fa=hasPrevious?result.points.last():stored(a),fb=stored(b);
            const double quantization=std::max(a.Distance(point(fa)),b.Distance(point(fb)));
            if(!std::isfinite(quantization) || quantization>tolerance*.25){error="CURVE_FLOAT_BUDGET_EXCEEDED";return false;}
            const gp_Vec chord(a,b);const double square=chord.SquareMagnitude();
            double hull=0,scale=1;
            for(int i=1;i<=arc->NbPoles();++i) {
                const auto p=arc->Pole(i);const double weight=arc->Weight(i);
                if(!std::isfinite(p.X()) || !std::isfinite(p.Y()) || !std::isfinite(p.Z()) ||
                   !std::isfinite(weight) || weight<=0){error="CURVE_POLE_INVALID";return false;}
                scale=std::max(scale,std::max(std::abs(p.X()),std::max(std::abs(p.Y()),std::abs(p.Z()))));
                const double t=square?std::max(0.,std::min(1.,gp_Vec(a,p).Dot(chord)/square)):0.;
                hull=std::max(hull,p.Distance(a.Translated(chord*t)));
            }
            // Reserve numeric room; this reserve is not an interval-arithmetic proof.
            const double reserve=std::max(Precision::Confusion(),64*std::numeric_limits<double>::epsilon()*scale);
            if(hull+quantization+reserve<=tolerance*.75) {
                if(hasPrevious && previousEnd.Distance(a)>reserve) {
                    error="CURVE_SPAN_DISCONTINUITY";return false;
                }
                if(result.points.size()>=65536){error="CURVE_POINT_LIMIT";return false;}
                if(!hasPrevious)result.points.append(fa);
                result.points.append(fb);previousEnd=b;hasPrevious=true;
                result.floatDisplacement=std::max(result.floatDisplacement,quantization);
                result.computedBound=std::max(result.computedBound,hull+quantization+reserve);
                return true;
            }
            if(depth>=32){error="CURVE_SUBDIVISION_LIMIT";return false;}
            auto left=Handle(Geom_BezierCurve)::DownCast(arc->Copy());
            auto right=Handle(Geom_BezierCurve)::DownCast(arc->Copy());
            left->Segment(0,.5);right->Segment(.5,1);
            return append(left,depth+1) && append(right,depth+1);
        };
        for(const auto &arc:arcs) {
            if(!append(arc,0))return fail(error.toLatin1().constData());
            ++result.continuityIntervals;
        }
        if(result.points.size()<2 || curve.Value(lo).Distance(point(result.points.first()))>tolerance*.25 ||
           curve.Value(hi).Distance(point(result.points.last()))>tolerance*.25)return fail("CURVE_ENDPOINT_MISSING");
        if(first>last)std::reverse(result.points.begin(),result.points.end());
        result.computedBound+=result.conversionEndpointDisplacement;
        if(result.computedBound>tolerance)return fail("CURVE_TOTAL_BUDGET_EXCEEDED");
        return result;
    } catch(const Standard_Failure &) {return fail("CURVE_KERNEL_FAILURE");}
}
