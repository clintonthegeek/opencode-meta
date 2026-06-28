#include "Specialist.h"

Specialist Specialist::fromJson(const QJsonObject &obj)
{
    Specialist s;

    s.id = obj.value(QStringLiteral("id")).toString();
    s.roleId = obj.value(QStringLiteral("role_id")).toString();
    s.modelId = obj.value(QStringLiteral("model_id")).toString();
    s.name = obj.value(QStringLiteral("name")).toString();

    if (obj.contains(QStringLiteral("prompt_override"))) {
        s.promptOverride = obj.value(QStringLiteral("prompt_override"));
    }

    if (obj.contains(QStringLiteral("metadata")) && obj.value(QStringLiteral("metadata")).isObject()) {
        s.metadata = obj.value(QStringLiteral("metadata")).toObject();
    }

    return s;
}

QJsonObject Specialist::toJson() const
{
    QJsonObject obj;

    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("role_id"), roleId);
    obj.insert(QStringLiteral("model_id"), modelId);
    obj.insert(QStringLiteral("name"), name);

    if (!promptOverride.isUndefined() && !promptOverride.isNull()) {
        obj.insert(QStringLiteral("prompt_override"), promptOverride);
    }

    if (!metadata.isEmpty()) {
        obj.insert(QStringLiteral("metadata"), metadata);
    }

    return obj;
}
