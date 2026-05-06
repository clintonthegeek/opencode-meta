// ModelInfo and ModelsCache data models
// Intended for working with the models.dev API snapshot

#pragma once

#include <QString>
#include <QSet>
#include <QMap>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

struct ModelInfo
{
    QString id;
    QString displayName;
    double inputCost = 0.0;
    double outputCost = 0.0;
    QSet<QString> capabilities;

    // Raw JSON payload for this model entry (kept flexible for API changes)
    QJsonObject data;

    static ModelInfo fromJson(const QJsonObject &obj)
    {
        ModelInfo info;
        info.data = obj;

        info.id = obj.value("id").toString();
        info.displayName = obj.value("display_name").toString();

        if (obj.contains("input_cost")) {
            info.inputCost = obj.value("input_cost").toDouble();
        }
        if (obj.contains("output_cost")) {
            info.outputCost = obj.value("output_cost").toDouble();
        }

        const QJsonArray caps = obj.value("capabilities").toArray();
        for (const QJsonValue &v : caps) {
            if (v.isString()) {
                info.capabilities.insert(v.toString());
            }
        }

        return info;
    }

    QJsonObject toJson() const
    {
        // Start from the raw data so that we do not lose information
        QJsonObject obj = data;

        // Ensure core fields are up to date with the struct
        if (!id.isEmpty()) {
            obj.insert("id", id);
        }
        if (!displayName.isEmpty()) {
            obj.insert("display_name", displayName);
        }

        obj.insert("input_cost", inputCost);
        obj.insert("output_cost", outputCost);

        if (!capabilities.isEmpty()) {
            QJsonArray capsArray;
            for (const QString &cap : capabilities) {
                capsArray.append(cap);
            }
            obj.insert("capabilities", capsArray);
        }

        return obj;
    }
};

class ModelsCache
{
public:
    // Map of model id -> ModelInfo
    QMap<QString, ModelInfo> models;

    // Timestamp of when the cache was last refreshed
    QDateTime timestamp;

    static ModelsCache fromJson(const QJsonObject &obj)
    {
        ModelsCache cache;

        const QString ts = obj.value("timestamp").toString();
        if (!ts.isEmpty()) {
            cache.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        }

        const QJsonObject modelsObj = obj.value("models").toObject();
        for (auto it = modelsObj.begin(); it != modelsObj.end(); ++it) {
            if (!it.value().isObject()) {
                continue;
            }
            const QString modelId = it.key();
            ModelInfo info = ModelInfo::fromJson(it.value().toObject());
            if (info.id.isEmpty()) {
                info.id = modelId;
            }
            cache.models.insert(modelId, info);
        }

        return cache;
    }

    QJsonObject toJson() const
    {
        QJsonObject obj;

        if (timestamp.isValid()) {
            obj.insert("timestamp", timestamp.toString(Qt::ISODate));
        }

        QJsonObject modelsObj;
        for (auto it = models.constBegin(); it != models.constEnd(); ++it) {
            modelsObj.insert(it.key(), it.value().toJson());
        }
        obj.insert("models", modelsObj);

        return obj;
    }
};
