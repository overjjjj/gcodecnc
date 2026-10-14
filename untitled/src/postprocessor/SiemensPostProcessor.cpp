#include "SiemensPostProcessor.h"
#include <QDateTime>
#include <QMap>
#include "../core/GCodeCoordinate.h"

namespace {

struct HoleCycleCall {
    QString code;
    double rtp = 0.0;
    double rfp = 0.0;
    double sdis = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double q = 0.0;
    double p = 0.0;
    double f = 0.0;
    double pitch = 0.0;
    double rpm = 0.0;
    int vari = 0;
    bool valid = false;
};

// 函数说明：formatDouble负责当前模块的核心处理。
static QString formatDouble(double value)
{
    return QString::number(value, 'f', 3);
}

// 函数说明：parseHoleCycle负责当前模块的核心处理。
static HoleCycleCall parseHoleCycle(const QString &line)
{
    HoleCycleCall call;
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith(QStringLiteral(";CNEXT_HOLE_CYCLE"))) {
        return call;
    }

    QMap<QString, QString> values;
    const QStringList tokens = trimmed.split(' ', Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        const int eq = token.indexOf('=');
        if (eq <= 0) {
            continue;
        }
        values.insert(token.left(eq).toLower(), token.mid(eq + 1));
    }

    call.code = values.value(QStringLiteral("code"));
    call.rtp = values.value(QStringLiteral("rtp")).toDouble();
    call.rfp = values.value(QStringLiteral("rfp")).toDouble();
    call.sdis = values.value(QStringLiteral("sdis")).toDouble();
    call.x = values.value(QStringLiteral("x")).toDouble();
    call.y = values.value(QStringLiteral("y")).toDouble();
    call.z = values.value(QStringLiteral("z")).toDouble();
    call.q = values.value(QStringLiteral("q")).toDouble();
    call.p = values.value(QStringLiteral("p")).toDouble();
    call.f = values.value(QStringLiteral("f")).toDouble();
    call.pitch = values.value(QStringLiteral("pitch")).toDouble();
    call.rpm = values.value(QStringLiteral("rpm")).toDouble();
    call.vari = values.value(QStringLiteral("vari"), QStringLiteral("0")).toInt();
    call.valid = !call.code.isEmpty() && values.contains(QStringLiteral("x")) &&
                 values.contains(QStringLiteral("y")) && values.contains(QStringLiteral("z"));
    return call;
}

// 函数说明：siemensCycleCommand负责当前模块的核心处理。
static QString siemensCycleCommand(const HoleCycleCall &call)
{
    if (call.code == QStringLiteral("G81")) {
        return QStringLiteral("MCALL CYCLE81(%1,%2,%3,%4,)")
            .arg(formatGCodeCoordinate(call.rtp),
                 formatGCodeCoordinate(call.rfp),
                 formatGCodeCoordinate(call.sdis),
                 formatGCodeCoordinate(call.z));
    }

    if (call.code == QStringLiteral("G82")) {
        const double dwellSeconds = call.p > 0.0 ? call.p / 1000.0 : 0.0;
        return QStringLiteral("MCALL CYCLE82(%1,%2,%3,%4,,%5)")
            .arg(formatGCodeCoordinate(call.rtp),
                 formatGCodeCoordinate(call.rfp),
                 formatGCodeCoordinate(call.sdis),
                 formatGCodeCoordinate(call.z),
                 formatDouble(dwellSeconds));
    }

    if (call.code == QStringLiteral("G73")) {
        // SINUMERIK CYCLE83 (PGZ 11/2002, 2-69):
        // RTP,RFP,SDIS,DP,DPR,FDEP,FDPR,DAM,DTB,DTS,FRF,VARI,_AXN,_MDEP,_VRT,_DTD,_DIS1.
        // G73 uses a fixed positive first depth (FDPR), chip breaking (VARI=0),
        // and the documented default short retract (_VRT=0 => 1 mm).
        if (call.q <= 0.0) return QString();
        const QStringList args = {
            formatGCodeCoordinate(call.rtp), formatGCodeCoordinate(call.rfp), formatGCodeCoordinate(call.sdis),
            formatGCodeCoordinate(call.z), QString(), QString(), formatGCodeCoordinate(call.q),
            formatDouble(0.0), formatDouble(call.p), QString(), formatDouble(1.0),
            QStringLiteral("0"), QString(), QString(), formatDouble(0.0),
            QString(), QString()
        };
        return QStringLiteral("MCALL CYCLE83(%1)").arg(args.join(QLatin1Char(',')));
    }

    if (call.code == QStringLiteral("G83")) {
        // Use the same documented argument positions for conventional peck
        // drilling, but select chip evacuation (VARI=1) explicitly.
        if (call.q <= 0.0) return QString();
        const QStringList args = {
            formatGCodeCoordinate(call.rtp), formatGCodeCoordinate(call.rfp), formatGCodeCoordinate(call.sdis),
            formatGCodeCoordinate(call.z), QString(), QString(), formatGCodeCoordinate(call.q),
            formatDouble(0.0), formatDouble(call.p), QString(), formatDouble(1.0),
            QStringLiteral("1"), QString(), QString(), QString(), QString(), QString()
        };
        return QStringLiteral("MCALL CYCLE83(%1)").arg(args.join(QLatin1Char(',')));
    }

    if (call.code == QStringLiteral("G84")) {
        const double dwellSeconds = call.p > 0.0 ? call.p / 1000.0 : 0.0;
        return QStringLiteral("MCALL CYCLE84(%1,%2,%3,%4,,%5,3,,%6,0,%7,%7)")
            .arg(formatGCodeCoordinate(call.rtp),
                 formatGCodeCoordinate(call.rfp),
                 formatGCodeCoordinate(call.sdis),
                 formatGCodeCoordinate(call.z),
                 formatDouble(dwellSeconds),
                 formatDouble(call.pitch),
                 formatDouble(call.rpm));
    }

    if (call.code == QStringLiteral("G85")) {
        const double dwellSeconds = call.p > 0.0 ? call.p / 1000.0 : 0.0;
        return QStringLiteral("MCALL CYCLE85(%1,%2,%3,%4,,%5,%6,%6)")
            .arg(formatGCodeCoordinate(call.rtp),
                 formatGCodeCoordinate(call.rfp),
                 formatGCodeCoordinate(call.sdis),
                 formatGCodeCoordinate(call.z),
                 formatDouble(dwellSeconds),
                 formatDouble(call.f));
    }

    // Siemens G76/G86 boring cycles are machine-builder specific. Do not
    // silently substitute Fanuc syntax or rename G85.
    if (call.code == QStringLiteral("G76") || call.code == QStringLiteral("G86")) {
        return QString();
    }

    return QString();
}

// 函数说明：cycleSignature负责当前模块的核心处理。
static QString cycleSignature(const HoleCycleCall &call)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11")
        .arg(call.code,
             formatGCodeCoordinate(call.rtp),
             formatGCodeCoordinate(call.rfp),
             formatGCodeCoordinate(call.sdis),
             formatGCodeCoordinate(call.z),
             formatGCodeCoordinate(call.q),
             formatDouble(call.p),
             formatDouble(call.f),
             QString::number(call.vari),
             formatDouble(call.pitch),
             formatDouble(call.rpm));
}

} // namespace

// 函数说明：wrapGCode负责当前模块的核心处理。
QString SiemensPostProcessor::wrapGCode(const QStringList &gcodeBlocks,
                                         const PostProcessorOptions &opts) const
{
    QString out;

    if (opts.addComments) {
        out += QString("; Generated by CNEXT-CAM  %1\n")
                   .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
        out += QString("; Post-processor: Siemens 840D\n");
        out += ";\n";
    }

    // Program header
    out += QString("%1\n").arg(opts.programNumber);
    for (const QString &block : resolvedSafeStartBlocks(opts)) {
        if (!block.trimmed().isEmpty()) {
            out += block.trimmed() + QStringLiteral("\n");
        }
    }

    out += "\n";

    bool activeCycle = false;
    QString activeSignature;
    bool seenToolChange = false;

    auto closeCycle = [&]() {
        if (activeCycle) {
            out += QStringLiteral("MCALL\n");
            activeCycle = false;
            activeSignature.clear();
        }
    };

    for (const QString &block : gcodeBlocks) {
        QString trimmed = block.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        const HoleCycleCall cycle = parseHoleCycle(trimmed);
        if (cycle.valid) {
            const QString signature = cycleSignature(cycle);
            if (!activeCycle || activeSignature != signature) {
                closeCycle();
                if (cycle.f > 0.0) {
                    out += QStringLiteral("F%1\n").arg(formatDouble(cycle.f));
                }
                const QString command = siemensCycleCommand(cycle);
                if (!command.isEmpty()) {
                    out += command + QStringLiteral("\n");
                }
                activeCycle = !command.isEmpty();
                activeSignature = signature;
            }
            out += QStringLiteral("X%1 Y%2\n")
                .arg(formatGCodeCoordinate(cycle.x), formatGCodeCoordinate(cycle.y));
            continue;
        }

        closeCycle();
        if (trimmed.startsWith(QStringLiteral("T")) && trimmed.contains(QStringLiteral("M6"))) {
            if (seenToolChange) {
                out += QStringLiteral("M5\n");
                out += QStringLiteral("M9\n");
            }
            seenToolChange = true;
        }
        out += trimmed + "\n";
    }
    closeCycle();

    out += "\n";
    out += "M5\n";
    out += "M9\n";
    out += "M30\n";

    return out;
}
