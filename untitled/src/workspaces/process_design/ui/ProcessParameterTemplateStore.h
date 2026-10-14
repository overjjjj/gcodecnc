#pragma once
#include <QFile>
#include <QJsonArray>
#include <QVariantMap>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <cmath>

class ProcessParameterTemplateStore {
public:
    explicit ProcessParameterTemplateStore(const QString &path = {})
        : m_path(path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                                 + QStringLiteral("/process-parameter-templates.json") : path) {}

    static QVariantMap cuttingParameters(const QVariantMap &source) {
        // Explicit allowlist: geometry, absolute heights, WCS and tool identity never travel.
        static const QSet<QString> keys = {
            "feedRate", "spindleSpeed", "plungeRate", "stepDown", "stepover", "peckDepth",
            "dwellTime", "coolantMode",
            "sideStockToLeave", "floorStockToLeave", "stockToLeave", "finishStock"
        };
        QVariantMap result;
        for (auto it = source.cbegin(); it != source.cend(); ++it) {
            bool ok = false;
            const double value = it.value().toDouble(&ok);
            if (keys.contains(it.key()) && ok && std::isfinite(value)) result[it.key()] = value;
        }
        return result;
    }

    QJsonArray entries(QString *error) const {
        error->clear();
        QFile file(m_path);
        if (!file.exists()) return {};
        if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return {}; }
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        const auto root = doc.object();
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()
            || root.value("version").toDouble() != 1.0 || !root.value("templates").isArray()) {
            *error = QStringLiteral("模板文件格式或版本不兼容 / Unsupported template format or version");
            return {};
        }
        const auto rows = root.value("templates").toArray();
        QSet<QString> identities;
        for (const auto &value : rows) {
            const auto row = value.toObject();
            const auto params = row.value("parameters").toObject().toVariantMap();
            const QString identity = row.value("strategyId").toString() + QChar(0)
                + row.value("name").toString();
            if (row.value("name").toString().trimmed().isEmpty()
                || row.value("strategyId").toString().isEmpty() || params.isEmpty()
                || cuttingParameters(params) != params || identities.contains(identity)) {
                *error = QStringLiteral("模板内容无效，请检查文件 / Invalid template contents");
                return {};
            }
            identities.insert(identity);
        }
        return rows;
    }

    bool save(const QString &name, const QString &strategyId, const QVariantMap &params, QString *error) {
        auto rows = entries(error);
        if (!error->isEmpty()) return false;
        const auto cutting = cuttingParameters(params);
        if (name.trimmed().isEmpty() || strategyId.isEmpty() || cutting.isEmpty()) {
            *error = QStringLiteral("请输入名称并提供切削参数 / Name and cutting parameters required");
            return false;
        }
        for (const auto &value : rows) {
            const auto row = value.toObject();
            if (row.value("name").toString() == name.trimmed()
                && row.value("strategyId").toString() == strategyId) {
                *error = QStringLiteral("同策略已有此名称，请使用新名称 / Name already exists for this strategy");
                return false;
            }
        }
        rows.append(QJsonObject{{"name", name.trimmed()}, {"strategyId", strategyId},
                                {"parameters", QJsonObject::fromVariantMap(cutting)}});
        return write(rows, error);
    }

    bool apply(const QString &name, const QString &strategyId, QVariantMap *params, QString *error) const {
        const auto rows = entries(error);
        if (!error->isEmpty()) return false;
        for (const auto &value : rows) {
            const auto row = value.toObject();
            if (row.value("name").toString() != name || row.value("strategyId").toString() != strategyId) continue;
            const auto cutting = row.value("parameters").toObject().toVariantMap();
            for (auto it = cutting.cbegin(); it != cutting.cend(); ++it) {
                if (params->contains(it.key())) params->insert(it.key(), it.value());
            }
            return true;
        }
        *error = QStringLiteral("模板不存在或策略不匹配 / Missing template or strategy mismatch");
        return false;
    }

    bool remove(const QString &name, const QString &strategyId, QString *error) {
        auto rows = entries(error);
        if (!error->isEmpty()) return false;
        for (int i = 0; i < rows.size(); ++i) {
            const auto row = rows.at(i).toObject();
            if (row.value("name").toString() == name && row.value("strategyId").toString() == strategyId) {
                rows.removeAt(i);
                return write(rows, error);
            }
        }
        *error = QStringLiteral("模板不存在 / Template not found");
        return false;
    }

private:
    bool write(const QJsonArray &rows, QString *error) const {
        if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
            *error = QStringLiteral("无法创建模板目录 / Cannot create template directory");
            return false;
        }
        QSaveFile file(m_path);
        const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"templates", rows}}).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            *error = file.errorString();
            return false;
        }
        return true;
    }
    QString m_path;
};
