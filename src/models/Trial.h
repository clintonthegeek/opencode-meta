// Trial data model
// Mirrors the Trial entity defined in docs/PARADIGM.md (v0.1).

#pragma once

#include <QString>
#include <QDateTime>
#include <QJsonObject>

class Trial
{
public:
    QString id;           // Unique id for this trial
    QString teamId;       // Reference to Team.id
    QString projectPath;  // Filesystem path for the project

    QDateTime timestamp;  // When the trial started (ISO on disk)

    QString notes;        // Free-form observations
    QJsonObject ratings;  // e.g. { "promptAdherence": 4, "codeQuality": 5, ... }

    // Optional snapshot of the exact opencode.json applied for this trial.
    QJsonObject renderedConfigSnapshot;

    // Optional duration in minutes (negative value means "unknown").
    int durationMinutes = -1;

    QJsonObject metadata; // Additional structured data

    static Trial fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};
