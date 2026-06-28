// Profile data model
// Mirrors the Profile JSON structure defined in v1spec.md

#pragma once

#include <QString>
#include <QMap>
#include <QJsonObject>

class Profile
{
public:
    QString id;
    QString name;
    QString templateId;

    // agent-name -> provider/model-id
    QMap<QString, QString> modelAssignments;

    // Arbitrary overrides, e.g. small_model, instructions, etc.
    QJsonObject globalOverrides;

    // Generic metadata map, typically contains "created" and "last_applied" ISO timestamps
    QMap<QString, QString> metadata;

    static Profile fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};
