#pragma once
#include "../../../strategies/StrategyFactory.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>
#include <cmath>

class MaterialRecommendationStore {
public:
    explicit MaterialRecommendationStore(const QString &path = {})
        : m_path(path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                                 + QStringLiteral("/material-recommendations.json") : path) {}

    QJsonArray entries(QString *error) const {
        error->clear();
        QFile file(m_path);
        if (!file.exists()) return {};
        if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return {}; }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        const auto root = document.object();
        if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
            root.value("version").toDouble() != 1 || !root.value("materials").isArray()) {
            *error = QStringLiteral("材料建议文件格式或版本不兼容。");
            return {};
        }
        QSet<QString> ids, identities;
        const auto rows = root.value("materials").toArray();
        for (const auto &value : rows) {
            const auto row = value.toObject();
            const QString id = row.value("id").toString();
            if (!valid(row) || ids.contains(id) || identities.contains(identity(row))) {
                *error = QStringLiteral("材料建议文件内容无效，请检查原文件。");
                return {};
            }
            ids.insert(id); identities.insert(identity(row));
        }
        return rows;
    }

    QString save(QJsonObject row, QString *error) const {
        auto rows = entries(error);
        if (!error->isEmpty()) return {};
        const bool creating = row.value("id").toString().isEmpty();
        if (creating) row["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        row["name"] = row.value("name").toString().trimmed();
        if (!valid(row)) { *error = QStringLiteral("请输入材料名称、策略、刀具及有效正数建议；转速须为整数。"); return {}; }
        int replace = -1;
        for (int i = 0; i < rows.size(); ++i) {
            const auto existing = rows.at(i).toObject();
            if (existing.value("id") == row.value("id")) replace = i;
            else if (identity(existing) == identity(row)) {
                *error = QStringLiteral("此材料、策略和刀具已有建议，请编辑已有条目。"); return {};
            }
        }
        if (!creating && replace < 0) { *error = QStringLiteral("材料建议已不存在，请重新选择。"); return {}; }
        if (replace >= 0) rows.replace(replace, row); else rows.append(row);
        return write(rows, error) ? row.value("id").toString() : QString();
    }

    bool remove(const QString &id, QString *error) const {
        auto rows = entries(error);
        if (!error->isEmpty()) return false;
        for (int i = 0; i < rows.size(); ++i) {
            if (rows.at(i).toObject().value("id").toString() != id) continue;
            rows.removeAt(i); return write(rows, error);
        }
        *error = QStringLiteral("材料建议已不存在。"); return false;
    }

    bool apply(const QString &id, const QString &strategyId, const QString &toolFingerprint,
               QVariantMap *params, QString *error, const QJsonObject &reviewed = {}) const {
        const auto rows = entries(error);
        if (!error->isEmpty()) return false;
        for (const auto &value : rows) {
            const auto row = value.toObject();
            if (row.value("id").toString() != id) continue;
            if (!reviewed.isEmpty() && row != reviewed) {
                *error = QStringLiteral("材料建议已改变，请重新预览。"); return false;
            }
            if (row.value("strategyId").toString() != strategyId ||
                row.value("toolFingerprint").toString() != toolFingerprint) {
                *error = QStringLiteral("建议的策略或刀具规格与当前工序不一致。"); return false;
            }
            const auto suggestion = row.value("parameters").toObject().toVariantMap();
            const auto strategy = StrategyFactory::instance().strategy(strategyId);
            const QVariantMap supported = strategy ? strategy->defaultParams().values : QVariantMap();
            for (auto it = suggestion.cbegin(); it != suggestion.cend(); ++it)
                if (!params->contains(it.key()) || !supported.contains(it.key())) {
                    *error = QStringLiteral("当前策略不支持建议参数：%1").arg(it.key()); return false;
                }
            for (auto it = suggestion.cbegin(); it != suggestion.cend(); ++it) params->insert(it.key(), it.value());
            return true;
        }
        *error = QStringLiteral("材料建议已不存在。"); return false;
    }

private:
    static QString identity(const QJsonObject &row) {
        return row.value("name").toString() + QChar(0) + row.value("strategyId").toString()
            + QChar(0) + row.value("toolFingerprint").toString();
    }
    static bool valid(const QJsonObject &row) {
        static const QRegularExpression fingerprint(QStringLiteral("^[0-9a-fA-F]{64}$"));
        if (QUuid(row.value("id").toString()).isNull() || row.value("name").toString().trimmed().isEmpty() ||
            row.value("strategyId").toString().isEmpty() ||
            !fingerprint.match(row.value("toolFingerprint").toString()).hasMatch() ||
            !row.value("parameters").isObject()) return false;
        const auto params = row.value("parameters").toObject();
        if (params.isEmpty()) return false;
        for (auto it = params.begin(); it != params.end(); ++it) {
            if (it.key() != "spindleSpeed" && it.key() != "feedRate" && it.key() != "plungeRate") return false;
            const double value = it.value().toDouble();
            if (!it.value().isDouble() || !std::isfinite(value) || value <= 0 || value > 1000000 ||
                (it.key() == "spindleSpeed" && value != std::floor(value))) return false;
        }
        return true;
    }
    bool write(const QJsonArray &rows, QString *error) const {
        if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
            *error = QStringLiteral("无法创建材料建议目录。"); return false;
        }
        QSaveFile file(m_path);
        const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"materials", rows}}).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            *error = file.errorString(); return false;
        }
        return true;
    }
    QString m_path;
};
