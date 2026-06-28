#include "Trial.h"

Trial Trial::fromJson(const QJsonObject &obj)
{
    Trial trial;

    trial.id = obj.value(QStringLiteral("id")).toString();
    trial.teamId = obj.value(QStringLiteral("team_id")).toString();
    trial.projectPath = obj.value(QStringLiteral("project_path")).toString();

    const QString ts = obj.value(QStringLiteral("timestamp")).toString();
    if (!ts.isEmpty()) {
        trial.timestamp = QDateTime::fromString(ts, Qt::ISODate);
    }

    trial.notes = obj.value(QStringLiteral("notes")).toString();

    if (obj.contains(QStringLiteral("ratings")) && obj.value(QStringLiteral("ratings")).isObject()) {
        trial.ratings = obj.value(QStringLiteral("ratings")).toObject();
    }

    if (obj.contains(QStringLiteral("rendered_config_snapshot")) && obj.value(QStringLiteral("rendered_config_snapshot")).isObject()) {
        trial.renderedConfigSnapshot = obj.value(QStringLiteral("rendered_config_snapshot")).toObject();
    }

    if (obj.contains(QStringLiteral("duration_minutes"))) {
        trial.durationMinutes = obj.value(QStringLiteral("duration_minutes")).toInt(-1);
    }

    if (obj.contains(QStringLiteral("metadata")) && obj.value(QStringLiteral("metadata")).isObject()) {
        trial.metadata = obj.value(QStringLiteral("metadata")).toObject();
    }

    return trial;
}

QJsonObject Trial::toJson() const
{
    QJsonObject obj;

    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("team_id"), teamId);
    obj.insert(QStringLiteral("project_path"), projectPath);

    if (timestamp.isValid()) {
        obj.insert(QStringLiteral("timestamp"), timestamp.toString(Qt::ISODate));
    }

    obj.insert(QStringLiteral("notes"), notes);

    if (!ratings.isEmpty()) {
        obj.insert(QStringLiteral("ratings"), ratings);
    }

    if (!renderedConfigSnapshot.isEmpty()) {
        obj.insert(QStringLiteral("rendered_config_snapshot"), renderedConfigSnapshot);
    }

    if (durationMinutes >= 0) {
        obj.insert(QStringLiteral("duration_minutes"), durationMinutes);
    }

    if (!metadata.isEmpty()) {
        obj.insert(QStringLiteral("metadata"), metadata);
    }

    return obj;
}
