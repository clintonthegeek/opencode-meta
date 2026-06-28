// Team data model
// Mirrors the Team entity defined in docs/PARADIGM.md (v0.1),
// with support for multiple primary specialists per user decision.

#pragma once

#include <QString>
#include <QMap>
#include <QList>
#include <QJsonObject>

class Team
{
public:
    QString id;          // Stable identifier for this team
    QString name;        // Human-readable name
    QString description; // Long-form description

    // Primary/orchestrator specialists. PARADIGM.md 2.3 defines a single
    // primarySpecialistId; here we allow multiple primaries while still
    // accepting the legacy singular field when loading.
    QList<QString> primarySpecialistIds;

    // Map of role id -> specialist id (no duplicate roles in one Team).
    // See PARADIGM.md 2.3 Team.specialists.
    struct SpecialistBinding {
        QString roleId;
        QString specialistId;
    };
    QList<SpecialistBinding> specialists;

    QString version;        // Semver or opaque version string
    QString parentTeamId;   // Optional parent id for future composition
    QJsonObject metadata;   // Generic metadata (created, notes, tags, ...)

    static Team fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;
};
