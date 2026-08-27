#pragma once
#include <QString>
#include <QVariantMap>

struct ToolEntry {
    int     id       = 0;
    QString name;
    QString type;    // drill, tap, reamer, boring_bar, thread_mill, end_mill, ...
    double  diameter = 0.0;
    double  fluteLen = 0.0;
    double  totalLen = 0.0;
    double  shankDia = 0.0;
    int     flutes   = 2;
    double  pointAngle = 118.0; // for drills
    double  pitch    = 0.0;     // for taps (mm/rev)
    QString material = "HSS";
    QString coating  = "none";
    QString modelPath;
    QVariantMap extra;

    QString toDisplayString() const;
};
