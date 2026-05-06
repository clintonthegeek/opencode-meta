#pragma once

#include <QDir>
#include <QList>
#include <QString>

#include "models/Template.h"
#include "models/Profile.h"
#include "models/ModelInfo.h"
#include "models/ProjectRecord.h"

// Centralized storage manager for reading/writing JSON data models
// under ~/.opencode-meta/ as described in v1spec.md.
class StorageManager
{
public:
    StorageManager() = default;

    // Ensure that the root directory (~/.opencode-meta/) exists.
    void ensureRoot() const;

    // Template I/O
    bool saveTemplate(const Template &t) const;        // templates/<id>/template.json
    Template loadTemplate(const QString &id) const;    // returns empty Template on failure
    QList<Template> listTemplates() const;             // scans templates/ directory

    // Profile I/O
    bool saveProfile(const Profile &p) const;          // profiles/<id>.json
    Profile loadProfile(const QString &id) const;      // returns empty Profile on failure
    QList<Profile> listProfiles() const;               // scans profiles/ directory

    // Models cache I/O
    bool saveModelsCache(const ModelsCache &cache) const; // models-cache.json
    ModelsCache loadModelsCache() const;                   // empty cache on failure

    // Projects I/O
    bool saveProjects(const QList<ProjectRecord> &projects) const; // projects.json array
    QList<ProjectRecord> loadProjects() const;                       // empty list on failure

    // Default profile handling
    bool setDefaultProfile(const QString &profileId) const; // default-profile.json copy
    QString getDefaultProfile() const;                      // returns profile id or empty

private:
    QString rootPath() const;
};
