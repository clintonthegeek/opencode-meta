#include "TeamVersion.h"

#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>

TeamVersion TeamVersion::fromJson(const QJsonObject &obj)
{
    TeamVersion tv;

    tv.id              = obj.value(QStringLiteral("id")).toString();
    tv.teamId          = obj.value(QStringLiteral("team_id")).toString();
    tv.parentVersionId = obj.value(QStringLiteral("parent_version_id")).toString();
    tv.reason          = obj.value(QStringLiteral("reason")).toString();
    tv.note            = obj.value(QStringLiteral("note")).toString();

    const QString tsText = obj.value(QStringLiteral("timestamp_utc")).toString();
    if (!tsText.isEmpty()) {
        tv.timestampUtc = QDateTime::fromString(tsText, Qt::ISODateWithMs);
        if (!tv.timestampUtc.isValid()) {
            tv.timestampUtc = QDateTime::fromString(tsText, Qt::ISODate);
        }
    }

    if (obj.contains(QStringLiteral("team")) && obj.value(QStringLiteral("team")).isObject()) {
        tv.team = Team::fromJson(obj.value(QStringLiteral("team")).toObject());
    }

    return tv;
}

QJsonObject TeamVersion::toJson() const
{
    QJsonObject obj;

    obj.insert(QStringLiteral("schema"), QStringLiteral("opencode-meta-team-version"));
    obj.insert(QStringLiteral("schema_version"), 1);

    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("team_id"), teamId);

    if (!parentVersionId.isEmpty()) {
        obj.insert(QStringLiteral("parent_version_id"), parentVersionId);
    }

    if (timestampUtc.isValid()) {
        obj.insert(QStringLiteral("timestamp_utc"),
                   timestampUtc.toUTC().toString(Qt::ISODateWithMs));
    }

    obj.insert(QStringLiteral("reason"), reason);
    if (!note.isEmpty()) {
        obj.insert(QStringLiteral("note"), note);
    }

    obj.insert(QStringLiteral("team"), team.toJson());

    return obj;
}
