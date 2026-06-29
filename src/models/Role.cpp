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

    // Phase C3-1 / D-5: readOnly round-trips from JSON as a bare bool.
    // Absent or non-bool values fall back to the default (false), so
    // legacy Role files written before this field existed still load
    // cleanly.
    role.readOnly = obj.value(QStringLiteral("readOnly")).toBool(false);

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

    // Phase C3-1 / D-5: emit the flag as a bare bool so a downstream
    // tool can read it without ceremony. Always emitted (true OR
    // false); absence-vs-explicit-false isn't useful here because we
    // have no legacy schema to coexist with.
    obj.insert(QStringLiteral("readOnly"), readOnly);

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
