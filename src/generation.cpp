#include "generation.h"

#include <QJsonObject>

#include "models/Profile.h"
#include "models/Template.h"

QJsonObject renderProfileToConfig(const Template &t, const Profile &p)
{
    QJsonObject root;

    root.insert(QStringLiteral("$schema"), QStringLiteral("https://opencode.ai/config.json"));

    if (!t.defaultAgent.isEmpty()) {
        root.insert(QStringLiteral("default_agent"), t.defaultAgent);
    }

    QJsonObject agentsObj;
    for (auto it = t.agents.constBegin(); it != t.agents.constEnd(); ++it) {
        const QString agentName = it.key();
        const AgentDef &def = it.value();

        QJsonObject agentObj = def.toJson();

        const QString modelId = p.modelAssignments.value(agentName);
        if (!modelId.isEmpty()) {
            agentObj.insert(QStringLiteral("model"), modelId);
        }

        agentsObj.insert(agentName, agentObj);
    }

    root.insert(QStringLiteral("agents"), agentsObj);

    for (auto it = p.globalOverrides.constBegin(); it != p.globalOverrides.constEnd(); ++it) {
        root.insert(it.key(), it.value());
    }

    return root;
}
