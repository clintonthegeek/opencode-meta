// ProjectRecord data model
// Mirrors the Project Record JSON structure defined in v1spec.md

#pragma once

#include <QString>
#include <QDateTime>
#include <QJsonObject>

class ProjectRecord
{
public:
    QString path;
    QString profileId;   // empty string if none
    QString teamId;      // empty string if none (active Team id)
    bool watchEnabled = false;
    QDateTime lastSync;
    QString lastTrialId; // id of the most recent Trial for this project (optional)

    static ProjectRecord fromJson(const QJsonObject &obj)
    {
        ProjectRecord record;

        record.path = obj.value("path").toString();

        const QJsonValue profileValue = obj.value("profile_id");
        if (profileValue.isString()) {
            record.profileId = profileValue.toString();
        } else {
            record.profileId.clear();
        }

        const QJsonValue teamValue = obj.value("team_id");
        if (teamValue.isString()) {
            record.teamId = teamValue.toString();
        } else {
            record.teamId.clear();
        }

        record.watchEnabled = obj.value("watch_enabled").toBool(false);

        const QString lastSyncStr = obj.value("last_sync").toString();
        if (!lastSyncStr.isEmpty()) {
            record.lastSync = QDateTime::fromString(lastSyncStr, Qt::ISODate);
        }

        const QJsonValue lastTrialValue = obj.value("last_trial_id");
        if (lastTrialValue.isString()) {
            record.lastTrialId = lastTrialValue.toString();
        } else {
            record.lastTrialId.clear();
        }

        return record;
    }

    QJsonObject toJson() const
    {
        QJsonObject obj;

        obj.insert("path", path);

        if (!profileId.isEmpty()) {
            obj.insert("profile_id", profileId);
        } else {
            obj.insert("profile_id", QJsonValue());
        }

        if (!teamId.isEmpty()) {
            obj.insert("team_id", teamId);
        } else {
            obj.insert("team_id", QJsonValue());
        }

        obj.insert("watch_enabled", watchEnabled);

        if (lastSync.isValid()) {
            obj.insert("last_sync", lastSync.toString(Qt::ISODate));
        } else {
            obj.insert("last_sync", QJsonValue());
        }

        if (!lastTrialId.isEmpty()) {
            obj.insert("last_trial_id", lastTrialId);
        } else {
            obj.insert("last_trial_id", QJsonValue());
        }

        return obj;
    }
};
