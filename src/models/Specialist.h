// Specialist data model
// Mirrors the Specialist entity defined in docs/PARADIGM.md (v0.1).

#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonValue>

class Specialist
{
public:
    QString id;        // Stable identifier for this specialist
    QString roleId;    // Reference to Role.id
    QString modelId;   // Provider/model id (e.g. "anthropic/claude-3-5-sonnet-latest")
    QString name;      // Optional human name/handle

    // Optional prompt override: string or {"file": "..."}, applied on top of Role.system_prompt
    QJsonValue promptOverride;

    // Arbitrary metadata for experimentation context
    QJsonObject metadata;

    static Specialist fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};
