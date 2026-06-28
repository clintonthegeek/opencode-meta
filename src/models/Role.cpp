#include "Role.h"

Role Role::fromJson(const QJsonObject &obj)
{
    Role role;

    role.id = obj.value(QStringLiteral("id")).toString();
    role.name = obj.value(QStringLiteral("name")).toString();
    role.description = obj.value(QStringLiteral("description")).toString();

    // system_prompt can be a string or an object {"file": ...}
    role.systemPrompt = obj.value(QStringLiteral("system_prompt"));

    const QString modeStr = obj.value(QStringLiteral("mode")).toString();
    role.mode = modeFromString(modeStr);

    if (obj.contains(QStringLiteral("permissions")) && obj.value(QStringLiteral("permissions")).isObject()) {
        role.permissions = obj.value(QStringLiteral("permissions")).toObject();
    }

    if (obj.contains(QStringLiteral("tools")) && obj.value(QStringLiteral("tools")).isObject()) {
        role.tools = obj.value(QStringLiteral("tools")).toObject();
    }

    if (obj.contains(QStringLiteral("metadata")) && obj.value(QStringLiteral("metadata")).isObject()) {
        role.metadata = obj.value(QStringLiteral("metadata")).toObject();
    }

    return role;
}

QJsonObject Role::toJson() const
{
    QJsonObject obj;

    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("description"), description);

    if (!systemPrompt.isUndefined() && !systemPrompt.isNull()) {
        obj.insert(QStringLiteral("system_prompt"), systemPrompt);
    }

    obj.insert(QStringLiteral("mode"), modeToString(mode));

    if (!permissions.isEmpty()) {
        obj.insert(QStringLiteral("permissions"), permissions);
    }
    if (!tools.isEmpty()) {
        obj.insert(QStringLiteral("tools"), tools);
    }
    if (!metadata.isEmpty()) {
        obj.insert(QStringLiteral("metadata"), metadata);
    }

    return obj;
}

QString Role::modeToString(Role::Mode mode)
{
    switch (mode) {
    case Mode::Primary:
        return QStringLiteral("primary");
    case Mode::Subagent:
        return QStringLiteral("subagent");
    case Mode::All:
        return QStringLiteral("all");
    }

    return QStringLiteral("primary");
}

Role::Mode Role::modeFromString(const QString &value)
{
    const QString v = value.toLower();
    if (v == QLatin1String("primary")) {
        return Mode::Primary;
    }
    if (v == QLatin1String("subagent")) {
        return Mode::Subagent;
    }
    if (v == QLatin1String("all")) {
        return Mode::All;
    }

    // Default to primary if unknown
    return Mode::Primary;
}
