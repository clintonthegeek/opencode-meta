#include "Team.h"

#include <QJsonArray>

Team Team::fromJson(const QJsonObject &obj)
{
    Team team;

    team.id = obj.value(QStringLiteral("id")).toString();
    team.name = obj.value(QStringLiteral("name")).toString();
    team.description = obj.value(QStringLiteral("description")).toString();
    team.version = obj.value(QStringLiteral("version")).toString();
    team.parentTeamId = obj.value(QStringLiteral("parent_team_id")).toString();

    // Primary specialists: prefer array field, fall back to singular.
    if (obj.contains(QStringLiteral("primary_specialist_ids")) && obj.value(QStringLiteral("primary_specialist_ids")).isArray()) {
        const QJsonArray arr = obj.value(QStringLiteral("primary_specialist_ids")).toArray();
        for (const QJsonValue &v : arr) {
            if (v.isString()) {
                const QString id = v.toString();
                if (!id.isEmpty()) {
                    team.primarySpecialistIds.append(id);
                }
            }
        }
    }

    if (team.primarySpecialistIds.isEmpty()) {
        const QString primary = obj.value(QStringLiteral("primary_specialist_id")).toString();
        if (!primary.isEmpty()) {
            team.primarySpecialistIds.append(primary);
        }
    }

    const QJsonObject specsObj = obj.value(QStringLiteral("specialists")).toObject();
    for (auto it = specsObj.begin(); it != specsObj.end(); ++it) {
        if (!it.value().isString()) {
            continue;
        }
        Team::SpecialistBinding binding;
        binding.roleId = it.key();
        binding.specialistId = it.value().toString();
        team.specialists.append(binding);
    }

    if (obj.contains(QStringLiteral("metadata")) && obj.value(QStringLiteral("metadata")).isObject()) {
        team.metadata = obj.value(QStringLiteral("metadata")).toObject();
    }

    return team;
}

QJsonObject Team::toJson() const
{
    QJsonObject obj;

    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("description"), description);
    obj.insert(QStringLiteral("version"), version);

    if (!parentTeamId.isEmpty()) {
        obj.insert(QStringLiteral("parent_team_id"), parentTeamId);
    }

    // Emit both the legacy singular field (when appropriate) and the array
    // to keep the on-disk format friendly to future readers.
    if (!primarySpecialistIds.isEmpty()) {
        if (primarySpecialistIds.size() == 1) {
            obj.insert(QStringLiteral("primary_specialist_id"), primarySpecialistIds.first());
        }

        QJsonArray arr;
        for (const QString &id : primarySpecialistIds) {
            arr.append(id);
        }
        obj.insert(QStringLiteral("primary_specialist_ids"), arr);
    }

    QJsonObject specsObj;
    for (const auto &binding : specialists) {
        specsObj.insert(binding.roleId, binding.specialistId);
    }
    obj.insert(QStringLiteral("specialists"), specsObj);

    if (!metadata.isEmpty()) {
        obj.insert(QStringLiteral("metadata"), metadata);
    }

    return obj;
}
