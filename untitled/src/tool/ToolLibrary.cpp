#include "ToolLibrary.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

ToolLibrary::ToolLibrary(QObject *parent)
    : QObject(parent)
{
    loadDefaults();
}

ToolLibrary &ToolLibrary::instance()
{
    static ToolLibrary s;
    return s;
}

void ToolLibrary::addTool(const ToolEntry &t)
{
    ToolEntry e = t;
    if (e.id == 0) {
        e.id = m_nextId++;
    }
    m_tools.append(e);
    emit toolLibraryChanged();
}

void ToolLibrary::updateTool(const ToolEntry &t)
{
    for (ToolEntry &e : m_tools) {
        if (e.id == t.id) {
            e = t;
            emit toolLibraryChanged();
            return;
        }
    }
}

void ToolLibrary::removeTool(int id)
{
    m_tools.erase(std::remove_if(m_tools.begin(), m_tools.end(),
        [id](const ToolEntry &e) { return e.id == id; }), m_tools.end());
    emit toolLibraryChanged();
}

ToolEntry ToolLibrary::tool(int id) const
{
    for (const auto &t : m_tools) {
        if (t.id == id) {
            return t;
        }
    }
    return {};
}

QVector<ToolEntry> ToolLibrary::toolsByType(const QString &type) const
{
    QVector<ToolEntry> res;
    for (const auto &t : m_tools) {
        if (t.type == type) {
            res.append(t);
        }
    }
    return res;
}

bool ToolLibrary::saveToFile(const QString &path)
{
    QJsonArray arr;
    for (const auto &t : m_tools) {
        QJsonObject o;
        o["id"] = t.id;
        o["name"] = t.name;
        o["type"] = t.type;
        o["diameter"] = t.diameter;
        o["fluteLen"] = t.fluteLen;
        o["totalLen"] = t.totalLen;
        o["flutes"] = t.flutes;
        o["pointAngle"] = t.pointAngle;
        o["pitch"] = t.pitch;
        o["material"] = t.material;
        o["modelPath"] = t.modelPath;
        arr.append(o);
    }
    QJsonObject root;
    root["tools"] = arr;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    return true;
}

bool ToolLibrary::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) {
        return false;
    }
    m_tools.clear();
    for (const auto &v : doc.object()["tools"].toArray()) {
        QJsonObject o = v.toObject();
        ToolEntry t;
        t.id = o["id"].toInt();
        t.name = o["name"].toString();
        t.type = o["type"].toString();
        t.diameter = o["diameter"].toDouble();
        t.fluteLen = o["fluteLen"].toDouble();
        t.totalLen = o["totalLen"].toDouble();
        t.flutes = o["flutes"].toInt();
        t.pointAngle = o["pointAngle"].toDouble();
        t.pitch = o["pitch"].toDouble();
        t.material = o["material"].toString();
        t.modelPath = o["modelPath"].toString();
        if (t.id >= m_nextId) {
            m_nextId = t.id + 1;
        }
        m_tools.append(t);
    }
    emit toolLibraryChanged();
    return true;
}

void ToolLibrary::loadDefaults()
{
    auto modelPath = [](const QString &file) {
        return QStringLiteral("tool_models/") + file;
    };
    auto assignDefaultToolModel = [&](ToolEntry &tool) {
        if (tool.type == QStringLiteral("drill")) {
            if (std::abs(tool.diameter - 3.0) < 0.01) tool.modelPath = modelPath(QStringLiteral("drill_D3.stl"));
            else if (std::abs(tool.diameter - 6.0) < 0.01) tool.modelPath = modelPath(QStringLiteral("drill_D6.stl"));
        } else if (tool.type == QStringLiteral("spot_drill")) {
            if (std::abs(tool.diameter - 4.0) < 0.01) tool.modelPath = modelPath(QStringLiteral("spot_drill_D4_90deg.stl"));
        } else if (tool.type == QStringLiteral("tap")) {
            if (std::abs(tool.diameter - 6.0) < 0.01) tool.modelPath = modelPath(QStringLiteral("tap_M6.stl"));
        } else if (tool.type == QStringLiteral("end_mill")) {
            if (std::abs(tool.diameter - 3.0) < 0.01) tool.modelPath = modelPath(QStringLiteral("end_mill_D3.stl"));
            else if (std::abs(tool.diameter - 6.0) < 0.01) tool.modelPath = modelPath(QStringLiteral("end_mill_D6.stl"));
            else if (std::abs(tool.diameter - 10.0) < 0.01) tool.modelPath = modelPath(QStringLiteral("end_mill_D10.stl"));
            else if (std::abs(tool.diameter - 50.0) < 0.01) tool.modelPath = modelPath(QStringLiteral("face_mill_D50.stl"));
        } else if (tool.type == QStringLiteral("chamfer_mill")) {
            if (std::abs(tool.diameter - 10.0) < 0.01) tool.modelPath = modelPath(QStringLiteral("chamfer_mill_D10_90deg.stl"));
        }
    };

    struct DefaultDrill { const char *name; double dia; double flute; double total; double angle; };
    static const DefaultDrill drills[] = {
        {"麻花钻 D3", 3.0, 30, 60, 118},
        {"麻花钻 D4", 4.0, 40, 75, 118},
        {"麻花钻 D5", 5.0, 52, 86, 118},
        {"麻花钻 D6", 6.0, 57, 93, 118},
        {"麻花钻 D8", 8.0, 75, 117, 118},
        {"麻花钻 D10", 10.0, 87, 133, 118},
        {"麻花钻 D12", 12.0, 101, 151, 118},
        {"中心钻 A2", 2.0, 5, 35, 60},
        {"中心钻 A4", 4.0, 10, 50, 60},
    };
    for (const auto &d : drills) {
        ToolEntry t;
        t.name = QString::fromUtf8(d.name);
        t.type = t.name.startsWith(QStringLiteral("中心钻"))
            ? QStringLiteral("spot_drill")
            : QStringLiteral("drill");
        t.diameter = d.dia;
        t.fluteLen = d.flute;
        t.totalLen = d.total;
        t.pointAngle = d.angle;
        t.flutes = 2;
        t.material = QStringLiteral("高速钢");
        assignDefaultToolModel(t);
        addTool(t);
    }

    struct DefaultTap { const char *name; double dia; double pitch; };
    static const DefaultTap taps[] = {
        {"丝锥 M3", 3.0, 0.5},
        {"丝锥 M4", 4.0, 0.7},
        {"丝锥 M5", 5.0, 0.8},
        {"丝锥 M6", 6.0, 1.0},
        {"丝锥 M8", 8.0, 1.25},
        {"丝锥 M10", 10.0, 1.5},
        {"丝锥 M12", 12.0, 1.75},
    };
    for (const auto &d : taps) {
        ToolEntry t;
        t.name = QString::fromUtf8(d.name);
        t.type = QStringLiteral("tap");
        t.diameter = d.dia;
        t.pitch = d.pitch;
        t.fluteLen = d.dia * 10;
        t.totalLen = d.dia * 18;
        t.material = QStringLiteral("高速钢");
        assignDefaultToolModel(t);
        addTool(t);
    }

    struct DefaultReamer { const char *name; double dia; };
    static const DefaultReamer reamers[] = {
        {"铰刀 D6", 6.0},
        {"铰刀 D8", 8.0},
        {"铰刀 D10", 10.0},
        {"铰刀 D12", 12.0},
    };
    for (const auto &d : reamers) {
        ToolEntry t;
        t.name = QString::fromUtf8(d.name);
        t.type = QStringLiteral("reamer");
        t.diameter = d.dia;
        t.fluteLen = d.dia * 8;
        t.totalLen = d.dia * 15;
        t.flutes = 6;
        t.material = QStringLiteral("高速钢");
        assignDefaultToolModel(t);
        addTool(t);
    }

    struct DefaultEndMill { const char *name; double dia; int flutes; };
    static const DefaultEndMill endMills[] = {
        {"立铣刀 D3", 3.0, 2},
        {"立铣刀 D6", 6.0, 2},
        {"立铣刀 D8", 8.0, 4},
        {"立铣刀 D10", 10.0, 4},
        {"立铣刀 D12", 12.0, 4},
        {"面铣刀 D50", 50.0, 4},
    };
    for (const auto &d : endMills) {
        ToolEntry t;
        t.name = QString::fromUtf8(d.name);
        t.type = QStringLiteral("end_mill");
        t.diameter = d.dia;
        t.fluteLen = d.dia * 3.0;
        t.totalLen = d.dia * 8.0;
        t.flutes = d.flutes;
        t.material = QStringLiteral("硬质合金");
        assignDefaultToolModel(t);
        addTool(t);
    }

    struct DefaultChamferMill { const char *name; double dia; double angle; };
    static const DefaultChamferMill chamferMills[] = {
        {"倒角刀 D6 90°", 6.0, 90.0},
        {"倒角刀 D10 90°", 10.0, 90.0},
        {"倒角刀 D12 90°", 12.0, 90.0},
    };
    for (const auto &d : chamferMills) {
        ToolEntry t;
        t.name = QString::fromUtf8(d.name);
        t.type = QStringLiteral("chamfer_mill");
        t.diameter = d.dia;
        t.fluteLen = d.dia * 2.0;
        t.totalLen = d.dia * 7.0;
        t.flutes = 2;
        t.material = QStringLiteral("硬质合金");
        t.extra.insert(QStringLiteral("angle"), d.angle);
        assignDefaultToolModel(t);
        addTool(t);
    }
}
