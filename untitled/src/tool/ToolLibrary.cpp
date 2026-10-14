#include "ToolLibrary.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <climits>

static QString toolError(const ToolEntry &tool)
{
    if (tool.id <= 0) return QStringLiteral("Tool number must be a positive integer.");
    for (double value : {tool.diameter, tool.fluteLen, tool.totalLen, tool.shankDia,
                         tool.pointAngle, tool.pitch})
        if (!std::isfinite(value) || value < 0)
            return QStringLiteral("Tool dimensions must be finite and nonnegative.");
    if (tool.flutes < 0) return QStringLiteral("Flute count must be nonnegative.");
    return {};
}

// 创建刀具库并追加内置默认刀具。
ToolLibrary::ToolLibrary(QObject *parent)
    // 函数说明：QObject负责当前模块的核心处理。
    : QObject(parent)
{
    loadDefaults();
}

// 返回进程内共享的刀具库，首次访问时构造并加载默认刀具。
ToolLibrary &ToolLibrary::instance()
{
    static ToolLibrary s;
    return s;
}

// 新增唯一刀号；零表示自动分配，失败不改变刀库。
bool ToolLibrary::addTool(const ToolEntry &t)
{
    ToolEntry e = t;
    QSet<int> ids;
    for (const auto &tool : m_tools) ids.insert(tool.id);
    if (e.id == 0) {
        int candidate = m_nextId;
        while (ids.contains(candidate)) candidate = candidate == INT_MAX ? 1 : candidate + 1;
        e.id = candidate;
    }
    m_lastError = toolError(e);
    if (!m_lastError.isEmpty()) return false;
    if (ids.contains(e.id)) {
        m_lastError = tr("Tool number %1 already exists.").arg(e.id);
        return false;
    }
    if (e.id >= m_nextId) m_nextId = e.id == INT_MAX ? 1 : e.id + 1;
    m_tools.append(e);
    emit toolLibraryChanged();
    return true;
}

// 按刀号替换首个匹配项并通知界面；未找到时不修改刀具库。
bool ToolLibrary::updateTool(const ToolEntry &t)
{
    m_lastError = toolError(t);
    if (!m_lastError.isEmpty()) return false;
    for (ToolEntry &e : m_tools) {
        if (e.id == t.id) {
            e = t;
            emit toolLibraryChanged();
            return true;
        }
    }
    m_lastError = tr("Tool number %1 does not exist.").arg(t.id);
    return false;
}

// 移除所有匹配刀号的条目，并发出刀具库变更信号。
void ToolLibrary::removeTool(int id)
{
    // 函数说明：erase负责当前模块的核心处理。
    m_tools.erase(std::remove_if(m_tools.begin(), m_tools.end(),
        [id](const ToolEntry &e) { return e.id == id; }), m_tools.end());
    emit toolLibraryChanged();
}

// 按刀号返回首个匹配刀具的副本；未找到时返回默认构造的刀具。
ToolEntry ToolLibrary::tool(int id) const
{
    for (const auto &t : m_tools) {
        if (t.id == id) {
            return t;
        }
    }
    return {};
}

// 按类型标识精确筛选刀具，保持原有顺序返回副本列表。
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

// 完整保存刀具证据，原子替换文件；任何写入或提交失败均保留原文件。
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
        o["shankDia"] = t.shankDia;
        o["flutes"] = t.flutes;
        o["pointAngle"] = t.pointAngle;
        o["pitch"] = t.pitch;
        o["material"] = t.material;
        o["coating"] = t.coating;
        o["modelPath"] = t.modelPath;
        o["extra"] = QJsonObject::fromVariantMap(t.extra);
        arr.append(o);
    }
    QJsonObject root;
    root["tools"] = arr;
    m_lastError.clear();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson();
    if (file.write(bytes) != bytes.size()) {
        m_lastError = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        m_lastError = file.errorString();
        return false;
    }
    return true;
}

// 从 JSON 重建刀具列表、推进可分配编号并通知界面；打开或解析失败时保留原列表。
bool ToolLibrary::loadFromFile(const QString &path)
{
    m_lastError.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject() || !doc.object()["tools"].isArray()) {
        m_lastError = parseError.error != QJsonParseError::NoError ? parseError.errorString()
            : tr("Expected a JSON object containing a tools array.");
        return false;
    }
    QVector<ToolEntry> loaded;
    QSet<int> ids;
    int maximumId = 0;
    for (const auto &v : doc.object()["tools"].toArray()) {
        const QJsonObject o = v.toObject();
        const auto invalid = [this]() {
            m_lastError = tr("Invalid tool entry or duplicate tool number; the current library was kept.");
            return false;
        };
        if (!v.isObject() || !o["id"].isDouble() || o["id"].toDouble() <= 0 ||
            o["id"].toDouble() > INT_MAX || std::floor(o["id"].toDouble()) != o["id"].toDouble())
            return invalid();
        for (const char *field : {"diameter", "fluteLen", "totalLen", "shankDia", "pointAngle", "pitch", "flutes"}) {
            if (!o.contains(field)) continue;
            const auto value = o[field];
            if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0)
                return invalid();
        }
        if (o.contains("flutes") && (o["flutes"].toDouble() > INT_MAX ||
            std::floor(o["flutes"].toDouble()) != o["flutes"].toDouble())) return invalid();
        for (const char *field : {"name", "type", "material", "coating", "modelPath"})
            if (o.contains(field) && !o[field].isString()) return invalid();
        if (o.contains("extra") && !o["extra"].isObject()) return invalid();
        ToolEntry t;
        t.id = o["id"].toInt();
        t.name = o["name"].toString();
        t.type = o["type"].toString();
        t.diameter = o["diameter"].toDouble();
        t.fluteLen = o["fluteLen"].toDouble();
        t.totalLen = o["totalLen"].toDouble();
        t.shankDia = o["shankDia"].toDouble();
        t.flutes = o["flutes"].toInt();
        t.pointAngle = o["pointAngle"].toDouble();
        t.pitch = o["pitch"].toDouble();
        t.material = o["material"].toString();
        t.coating = o["coating"].toString();
        t.modelPath = o["modelPath"].toString();
        t.extra = o["extra"].toObject().toVariantMap();
        if (ids.contains(t.id)) return invalid();
        ids.insert(t.id);
        maximumId = std::max(maximumId, t.id);
        loaded.append(t);
    }
    m_tools = loaded;
    m_nextId = maximumId == INT_MAX ? 1 : maximumId + 1;
    emit toolLibraryChanged(false);
    return true;
}

// 追加内置钻头、丝锥、铰刀和铣刀，给匹配规格绑定默认模型路径。
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
        t.pointAngle = d.angle;
        t.material = QStringLiteral("硬质合金");
        t.extra.insert(QStringLiteral("angle"), d.angle);
        assignDefaultToolModel(t);
        addTool(t);
    }
}
