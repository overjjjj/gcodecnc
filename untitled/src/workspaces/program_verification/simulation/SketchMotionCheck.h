#pragma once

#include "SimulationController.h"
#include "SketchVerificationContext.h"
#include "SketchClearance.h"
#include <QRegularExpression>
#include <QSet>
#include <cfenv>

inline SimulationCheckResult checkSketchMotion(const SketchVerificationContext &context,
                                               const QVector<ToolMove> &moves,
                                               const QString &nc, const Coordinate3D &origin)
{
    using namespace SketchClearance;
    auto reject = [](const QString &reason) {
        return SimulationCheckResult{SimulationCheckState::Incomplete,
            QStringLiteral("草图运动检查未通过：") + reason,{}};
    };
    if (std::fegetround() != FE_TONEAREST)
        return reject(QStringLiteral("当前浮点舍入模式不受支持。"));
    volatile double smallest = std::numeric_limits<double>::denorm_min();
    volatile double doubled = smallest + smallest;
    if (doubled == 0) return reject(QStringLiteral("当前浮点环境丢失次正规数。"));
    if (!context.error.isEmpty()) return reject(context.error);
    if (!QRegularExpression("^G5[4-9]$").match(context.workOffset).hasMatch())
        return reject(QStringLiteral("工件坐标系不受支持。"));
    if (context.document.contourKind!="rectangle" && context.document.contourKind!="polyline")
        return reject(QStringLiteral("直线、圆和圆弧尚无草图加工运动证明。"));
    if (!context.document.additionalContours.isEmpty())
        return reject(QStringLiteral("多轮廓草图尚不支持加工运动检查。"));
    if (!context.document.validationError().isEmpty() || context.document.isEmpty() ||
        context.sourceFingerprint != context.document.fingerprint() || context.setupFingerprint.isEmpty() ||
        origin.x()!=context.modelOrigin.x() || origin.y()!=context.modelOrigin.y() || origin.z()!=context.modelOrigin.z() ||
        !std::isfinite(origin.x()) || !std::isfinite(origin.y()) || !std::isfinite(origin.z()) ||
        !convexOrientation(context.document.points))
        return reject(QStringLiteral("来源、设置或凸轮廓证据无效。"));
    const auto &stock = context.stock;
    const double allowances[]{stock.minusX,stock.plusX,stock.minusY,stock.plusY,stock.minusZ,stock.plusZ};
    if (!stock.confirmed) return reject(QStringLiteral("请确认毛坯。"));
    for (double value : allowances)
        if (!std::isfinite(value) || value < 0) return reject(QStringLiteral("毛坯余量无效。"));
    if (stock.minusZ <= 0) return reject(QStringLiteral("毛坯需要明确厚度。"));
    double minX=context.document.points.first().x(), maxX=minX;
    double minY=context.document.points.first().y(), maxY=minY;
    for (const auto &point : context.document.points) {
        minX=std::min(minX,point.x()); maxX=std::max(maxX,point.x());
        minY=std::min(minY,point.y()); maxY=std::max(maxY,point.y());
    }
    const auto left=subtract(Bound::exact(minX),Bound::exact(stock.minusX));
    const auto right=add(Bound::exact(maxX),Bound::exact(stock.plusX));
    const auto front=subtract(Bound::exact(minY),Bound::exact(stock.minusY));
    const auto back=add(Bound::exact(maxY),Bound::exact(stock.plusY));
    const auto bottom=subtract(Bound::exact(context.document.planeZ),Bound::exact(stock.minusZ));
    const auto top=add(Bound::exact(context.document.planeZ),Bound::exact(stock.plusZ));
    for (auto value : {left,right,front,back,bottom,top})
        if (!value.valid()) return reject(QStringLiteral("毛坯数值范围无法证明。"));
    QHash<QString,const SketchOperationCheck *> owners;
    for (const auto &operation : context.operations) {
        const auto &tool=operation.tool;
        if (operation.id.isEmpty() || owners.contains(operation.id) ||
            operation.selectionFingerprint.isEmpty() || operation.parameterFingerprint.isEmpty() ||
            !std::isfinite(operation.depth) || operation.depth<=0 ||
            !std::isfinite(operation.stockToLeave) || operation.stockToLeave<0 ||
            tool.id<=0 || tool.type!="end_mill" || !tool.modelPath.isEmpty() ||
            !std::isfinite(tool.diameter) || tool.diameter<=0 ||
            !std::isfinite(tool.fluteLen) || tool.fluteLen<=0 ||
            !std::isfinite(tool.totalLen) || tool.totalLen<tool.fluteLen ||
            !std::isfinite(tool.shankDia) || tool.shankDia<=0)
            return reject(QStringLiteral("当前工序或名义刀具尺寸无效。"));
        owners.insert(operation.id,&operation);
    }
    if (owners.isEmpty()) return reject(QStringLiteral("缺少当前工序。"));
    const auto lines=nc.split('\n');
    QVector<QString> lineOwners;
    QVector<bool> lineModesReady;
    QSet<int> establishedModes;
    QString currentOwner;
    const QRegularExpression marker("\\[op:([^\\]]+)\\]");
    const QRegularExpression word("([A-Za-z])([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+))");
    const QSet<int> allowedG{0,1,17,21,40,49,80,90,94};
    const QSet<int> allowedM{2,3,5,6,8,9,30};
    bool ended=false;
    for (QString line : lines) {
        auto markers=marker.globalMatch(line);
        int markerCount=0;
        while (markers.hasNext()) {
            currentOwner=markers.next().captured(1);
            if (++markerCount>1 || !owners.contains(currentOwner))
                return reject(QStringLiteral("程序工序归属无效。"));
        }
        lineOwners.append(currentOwner);
        QString code;
        bool comment=false;
        for (int i=0;i<line.size();++i) {
            const QChar c=line[i];
            if (!comment && (c==';' || (c=='/' && i+1<line.size() && line[i+1]=='/'))) break;
            if (c=='(') { if(comment) return reject(QStringLiteral("嵌套注释不受支持。")); comment=true; continue; }
            if (c==')') { if(!comment) return reject(QStringLiteral("注释不完整。")); comment=false; continue; }
            if (!comment) code+=c;
        }
        if (comment) return reject(QStringLiteral("注释不完整。"));
        if (code.trimmed()=="%") {
            lineModesReady.append(false);
            continue;
        }
        if (ended && !code.trimmed().isEmpty())
            return reject(QStringLiteral("程序结束之后不能包含可执行内容。"));
        int cursor=0; bool hasT=false, hasM6=false, programEnd=false, motionWord=false;
        auto words=word.globalMatch(code);
        while (words.hasNext()) {
            const auto match=words.next();
            if (!code.mid(cursor,match.capturedStart()-cursor).trimmed().isEmpty())
                return reject(QStringLiteral("程序包含不支持的表达式。"));
            cursor=match.capturedEnd();
            const QString letter=match.captured(1).toUpper(), token=match.captured(2);
            bool valid=false; const double value=token.toDouble(&valid);
            if (!valid || !std::isfinite(value)) return reject(QStringLiteral("程序数值无效。"));
            if (letter=="G") {
                if (value!=std::floor(value) || value<0 || value>99 ||
                    (!allowedG.contains(int(value)) && QString("G%1").arg(int(value))!=context.workOffset))
                    return reject(QStringLiteral("草图仅支持固定坐标系下的 G0/G1 与 G40。"));
                establishedModes.insert(int(value));
                motionWord |= value==0 || value==1;
            } else if (letter=="M") {
                if (value!=std::floor(value) || value<0 || value>99 || !allowedM.contains(int(value)))
                    return reject(QStringLiteral("程序辅助指令不受支持。"));
                hasM6 |= value==6;
                programEnd |= value==2 || value==30;
            } else if (letter=="T") {
                if(value!=std::floor(value) || value<=0 || value>2147483647.0 || hasT)
                    return reject(QStringLiteral("换刀指令无效。"));
                hasT=true;
            } else if (letter=="X" || letter=="Y" || letter=="Z") {
                motionWord=true;
                // Three-decimal output must round-trip without silently collapsing a large coordinate.
                QString normalized=token;
                bool negative=normalized.startsWith('-');
                if(normalized.startsWith('-') || normalized.startsWith('+')) normalized.remove(0,1);
                QStringList parts=normalized.split('.');
                QString integer=parts[0], fraction=parts.size()>1?parts[1]:QString();
                while(integer.size()>1 && integer.startsWith('0')) integer.remove(0,1);
                if(integer.isEmpty()) integer="0";
                if(fraction.size()>3) return reject(QStringLiteral("坐标输出精度超出三位范围。"));
                fraction=fraction.leftJustified(3,'0');
                normalized=(negative && value!=0?"-":"")+integer+"."+fraction;
                if(QString::number(value,'f',3)!=normalized)
                    return reject(QStringLiteral("坐标十进制解析丢失精度。"));
            } else if (letter!="F" && letter!="S" && letter!="N" && letter!="O")
                return reject(QStringLiteral("程序字不受支持。"));
        }
        if (programEnd && motionWord)
            return reject(QStringLiteral("程序结束指令不能与运动或坐标写在同一行。"));
        ended |= programEnd;
        if (!code.mid(cursor).trimmed().isEmpty() || hasT!=hasM6)
            return reject(QStringLiteral("程序文本或换刀不完整。"));
        bool ready=true;
        for(int mode : {17,21,40,49,80,90,94,context.workOffset.mid(1).toInt()})
            ready &= establishedModes.contains(mode);
        lineModesReady.append(ready);
    }
    bool initialLimited=false, knownMotion=false, established=false;
    for (const auto &move : moves) {
        if (move.sourceLine<0 || move.sourceLine>=lineOwners.size())
            return reject(QStringLiteral("运动来源行无效。"));
        if (!lineModesReady[move.sourceLine])
            return reject(QStringLiteral("首次运动前必须显式建立固定单位、平面、补偿、绝对坐标及进给模式。"));
        auto coordinate=[](double work,double offset){return add(Bound::parsed(work),Bound::exact(offset));};
        const auto az=coordinate(move.from.z(),origin.z()), bz=coordinate(move.to.z(),origin.z());
        if (!az.valid() || !bz.valid()) return reject(QStringLiteral("运动坐标无效。"));
        if (move.fromKnownAxes!=7 || move.toKnownAxes!=7) {
            if (established || !move.isRapid || !(move.toKnownAxes&4) || bz.lo<=top.hi ||
                ((move.fromKnownAxes&4) && az.lo<=top.hi) ||
                (move.fromKnownAxes!=0 && move.fromKnownAxes!=4) ||
                (move.fromKnownAxes==0 && move.toKnownAxes!=4) ||
                (move.fromKnownAxes==0 && move.commandedAxes!=4) ||
                (move.toKnownAxes!=4 && move.toKnownAxes!=7))
                return reject(QStringLiteral("未知初始位置只能先建立安全 Z，再建立 XYZ。"));
            initialLimited=true;
            established=move.toKnownAxes==7;
            continue;
        }
        established=true; knownMotion=true;
        const auto owner=owners.value(lineOwners[move.sourceLine],nullptr);
        if (!owner || move.toolId!=owner->tool.id)
            return reject(QStringLiteral("运动归属或实际刀号与当前工序不符。"));
        const Bound lowest{std::min(az.lo,bz.lo),std::min(az.hi,bz.hi)};
        const auto depthBottom=subtract(Bound::exact(context.document.planeZ),Bound::exact(owner->depth));
        if (!depthBottom.valid() || lowest.lo<bottom.hi || lowest.lo<depthBottom.hi)
            return reject(QStringLiteral("运动超出毛坯或工序深度范围。"));
        if (lowest.lo>top.hi) continue;
        const bool xyChanged=move.from.x()!=move.to.x() || move.from.y()!=move.to.y();
        const bool zChanged=move.from.z()!=move.to.z();
        if ((move.isRapid || zChanged) && xyChanged)
            return reject(QStringLiteral("低位快速或升降运动必须严格固定 XY。"));
        const auto shankBottom=add(lowest,Bound::exact(owner->tool.fluteLen));
        if (!shankBottom.valid() || shankBottom.lo<=top.hi)
            return reject(QStringLiteral("刀柄未被证明完全高于声明毛坯。"));
        auto xyError=[&](double x,double y) {
            const auto bx=coordinate(x,origin.x()), by=coordinate(y,origin.y());
            const double nx=x+origin.x(), ny=y+origin.y();
            if(!bx.valid() || !by.valid()) return Bound{};
            const auto ex=outward(0,std::max(std::abs(nx-bx.lo),std::abs(bx.hi-nx)));
            const auto ey=outward(0,std::max(std::abs(ny-by.lo),std::abs(by.hi-ny)));
            return add(ex,ey);
        };
        const auto ea=xyError(move.from.x(),move.from.y()), eb=xyError(move.to.x(),move.to.y());
        if(!ea.valid() || !eb.valid()) return reject(QStringLiteral("坐标转换误差无法界定。"));
        const auto radius=divide(Bound::exact(owner->tool.diameter),Bound::exact(2));
        const auto inflated=add(radius,Bound::exact(std::max(ea.hi,eb.hi)));
        const QPointF a(move.from.x()+origin.x(),move.from.y()+origin.y());
        const QPointF b(move.to.x()+origin.x(),move.to.y()+origin.y());
        if (!inflated.valid()) return reject(QStringLiteral("刀具包络无法界定。"));
        if (move.isRapid || zChanged) {
            if(pointClearsRectangle(a,left.lo,front.lo,right.hi,back.hi,inflated.hi)!=Verdict::Clear)
                return reject(QStringLiteral("整刀盘未离开声明毛坯，请调整引入长度、起点或毛坯。"));
        } else {
            const auto clearance=add(inflated,Bound::exact(owner->stockToLeave));
            if(!clearance.valid() || segmentClearsPolygon(a,b,context.document.points,clearance.hi)!=Verdict::Clear)
                return reject(QStringLiteral("整段刀盘与保留轮廓的正间隙无法证明。"));
        }
    }
    if (!knownMotion) return reject(QStringLiteral("没有已知运动可供检查。"));
    SimulationCheckResult result{initialLimited?SimulationCheckState::Limited:SimulationCheckState::Complete,
        QStringLiteral("二维轮廓与声明毛坯检查通过。"),{}};
    if(initialLimited) result.limitations.append(QStringLiteral("初始机床位置未知，仅核验建立安全位置之后的运动。"));
    return result;
}
