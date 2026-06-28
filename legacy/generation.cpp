#include "generation.h"

#include <QJsonObject>
#include <QSet>

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

QStringList summarizeTopLevelConfigDiff(const QJsonObject &left, const QJsonObject &right)
{
    QStringList lines;

    // Collect union of top-level keys.
    QSet<QString> keys;
    for (auto it = left.begin(); it != left.end(); ++it) {
        keys.insert(it.key());
    }
    for (auto it = right.begin(); it != right.end(); ++it) {
        keys.insert(it.key());
    }

    QStringList sortedKeys = keys.values();
    sortedKeys.sort();

    for (const QString &key : sortedKeys) {
        const bool inLeft = left.contains(key);
        const bool inRight = right.contains(key);

        if (inLeft && !inRight) {
            lines.append(QStringLiteral("Key '%1' only present on left").arg(key));
            continue;
        }
        if (!inLeft && inRight) {
            lines.append(QStringLiteral("Key '%1' only present on right").arg(key));
            continue;
        }

        const QJsonValue vLeft = left.value(key);
        const QJsonValue vRight = right.value(key);
        if (vLeft == vRight) {
            // Optionally include identical keys if we ever want a full picture.
            continue;
        }

        // For complex structures we do not attempt deep diffing; just mark
        // that the value differs.
        if (vLeft.isObject() || vLeft.isArray() || vRight.isObject() || vRight.isArray()) {
            lines.append(QStringLiteral("Key '%1' differs (complex value)").arg(key));
        } else {
            const QString leftStr = vLeft.toVariant().toString();
            const QString rightStr = vRight.toVariant().toString();
            lines.append(QStringLiteral("Key '%1' differs: left=%2, right=%3")
                             .arg(key, leftStr, rightStr));
        }
    }

    if (lines.isEmpty()) {
        lines.append(QStringLiteral("No top-level differences."));
    }

    return lines;
}
