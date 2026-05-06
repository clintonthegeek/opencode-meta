#include "Template.h"

#include <QJsonArray>

AgentDef AgentDef::fromJson(const QJsonObject &obj)
{
    AgentDef def;

    const QString modeStr = obj.value("mode").toString();
    def.mode = modeFromString(modeStr);

    def.description = obj.value("description").toString();
    def.prompt = obj.value("prompt");

    // Permissions default to Ask if not present
    def.edit = permissionFromString(obj.value("edit").toString("ask"));
    def.bash = permissionFromString(obj.value("bash").toString("ask"));
    def.task = permissionFromString(obj.value("task").toString("ask"));
    def.read = permissionFromString(obj.value("read").toString("ask"));
    def.grep = permissionFromString(obj.value("grep").toString("ask"));
    def.glob = permissionFromString(obj.value("glob").toString("ask"));

    if (obj.contains("tools") && obj.value("tools").isObject()) {
        def.tools = obj.value("tools").toObject();
    }

    return def;
}

QJsonObject AgentDef::toJson() const
{
    QJsonObject obj;

    obj.insert("mode", modeToString(mode));
    obj.insert("description", description);

    if (!prompt.isUndefined() && !prompt.isNull()) {
        obj.insert("prompt", prompt);
    }

    obj.insert("edit", permissionToString(edit));
    obj.insert("bash", permissionToString(bash));
    obj.insert("task", permissionToString(task));
    obj.insert("read", permissionToString(read));
    obj.insert("grep", permissionToString(grep));
    obj.insert("glob", permissionToString(glob));

    if (!tools.isEmpty()) {
        obj.insert("tools", tools);
    }

    return obj;
}

QString AgentDef::modeToString(Mode mode)
{
    switch (mode) {
    case Mode::Primary:
        return QStringLiteral("primary");
    case Mode::Subagent:
        return QStringLiteral("subagent");
    }

    return QStringLiteral("primary");
}

AgentDef::Mode AgentDef::modeFromString(const QString &value)
{
    const QString v = value.toLower();
    if (v == QLatin1String("primary")) {
        return Mode::Primary;
    }
    if (v == QLatin1String("subagent")) {
        return Mode::Subagent;
    }

    // Default to primary if unknown
    return Mode::Primary;
}

QString AgentDef::permissionToString(Permission permission)
{
    switch (permission) {
    case Permission::Allow:
        return QStringLiteral("allow");
    case Permission::Deny:
        return QStringLiteral("deny");
    case Permission::Ask:
        return QStringLiteral("ask");
    }

    return QStringLiteral("ask");
}

AgentDef::Permission AgentDef::permissionFromString(const QString &value)
{
    const QString v = value.toLower();
    if (v == QLatin1String("allow")) {
        return Permission::Allow;
    }
    if (v == QLatin1String("deny")) {
        return Permission::Deny;
    }

    // Default to Ask for unknown or missing values
    return Permission::Ask;
}

Template Template::fromJson(const QJsonObject &obj)
{
    Template t;

    t.id = obj.value("id").toString();
    t.name = obj.value("name").toString();
    t.version = obj.value("version").toString();
    t.defaultAgent = obj.value("default_agent").toString();

    const QJsonObject agentsObj = obj.value("agents").toObject();
    for (auto it = agentsObj.begin(); it != agentsObj.end(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QString agentName = it.key();
        const QJsonObject agentObj = it.value().toObject();
        t.agents.insert(agentName, AgentDef::fromJson(agentObj));
    }

    const QJsonObject metaObj = obj.value("metadata").toObject();
    for (auto it = metaObj.begin(); it != metaObj.end(); ++it) {
        t.metadata.insert(it.key(), it.value().toString());
    }

    return t;
}

QJsonObject Template::toJson() const
{
    QJsonObject obj;

    obj.insert("id", id);
    obj.insert("name", name);
    obj.insert("version", version);
    obj.insert("default_agent", defaultAgent);

    QJsonObject agentsObj;
    for (auto it = agents.constBegin(); it != agents.constEnd(); ++it) {
        agentsObj.insert(it.key(), it.value().toJson());
    }
    obj.insert("agents", agentsObj);

    QJsonObject metaObj;
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        metaObj.insert(it.key(), it.value());
    }
    if (!metaObj.isEmpty()) {
        obj.insert("metadata", metaObj);
    }

    return obj;
}
