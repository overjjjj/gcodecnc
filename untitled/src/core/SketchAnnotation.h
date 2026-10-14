#pragma once
#include <QJsonObject>
#include <QUuid>
#include <cmath>

struct SketchGeometryReference
{
    QString contourId;
    QString kind;
    int index = 0;
    bool valid() const
    {
        return !QUuid(contourId).isNull() && index>=0 && index<4096 &&
            (kind=="vertex" || kind=="edge" || ((kind=="center" || kind=="start" || kind=="end" || kind=="curve") && index==0));
    }
    QJsonObject toJson() const { return {{"contourId",contourId},{"kind",kind},{"index",index}}; }
    static bool read(const QJsonValue &value,SketchGeometryReference *out)
    {
        if(!value.isObject())return false;
        const auto o=value.toObject();
        if(!o["contourId"].isString() || !o["kind"].isString() || !o["index"].isDouble() ||
           o["index"].toDouble()<0 || o["index"].toDouble()>=4096 || o["index"].toDouble()!=std::floor(o["index"].toDouble()))return false;
        SketchGeometryReference candidate{o["contourId"].toString(),o["kind"].toString(),o["index"].toInt()};
        if(!candidate.valid())return false;
        *out=candidate;return true;
    }
};

struct SketchAnnotation
{
    QString id;
    QString kind;
    SketchGeometryReference first,second;
    bool needsSecond() const { return kind=="distance" || kind=="angle"; }
    bool valid() const
    {
        const auto point=[](const SketchGeometryReference &r){return r.kind=="vertex" || r.kind=="center" || r.kind=="start" || r.kind=="end";};
        if(QUuid(id).isNull() || !first.valid() || (needsSecond() && !second.valid()))return false;
        return kind=="distance" ? point(first)&&point(second) : kind=="coordinate" ? point(first) :
            kind=="angle" ? first.kind=="edge"&&second.kind=="edge" : kind=="radius"&&first.kind=="curve";
    }
    QJsonObject toJson() const
    {
        QJsonObject value{{"id",id},{"kind",kind},{"first",first.toJson()}};
        if(needsSecond())value["second"]=second.toJson();
        return value;
    }
    static bool read(const QJsonValue &value,SketchAnnotation *out)
    {
        if(!value.isObject())return false;
        const auto o=value.toObject();SketchAnnotation candidate;
        if(!o["id"].isString() || !o["kind"].isString())return false;
        candidate.id=o["id"].toString();candidate.kind=o["kind"].toString();
        if(!SketchGeometryReference::read(o["first"],&candidate.first))return false;
        if(candidate.needsSecond()) {if(!SketchGeometryReference::read(o["second"],&candidate.second))return false;}
        else if(o.contains("second"))return false;
        if(!candidate.valid())return false;
        *out=candidate;return true;
    }
};
