/**
 * \file StorageManager.h
 * \brief Centralized storage manager for reading/writing JSON data models under ~/.opencode-meta/ as described in v1spec.md.
 *
 * This class handles persistence for templates, profiles, models cache, projects, and now preferred providers.
 * All operations use Qt's JSON facilities and file I/O, with error logging via qDebug().
 */
#pragma once

#include <QDir>
#include <QList>
#include <QString>
#include <QSet>

#include "models/Template.h"
#include "models/Profile.h"
#include "models/ModelInfo.h"
#include "models/ProjectRecord.h"

// Centralized storage manager for reading/writing JSON data models
// under ~/.opencode-meta/ as described in v1spec.md.
class StorageManager
{
public:
    // Optional rootOverride is primarily for tests; in the application we
    // rely on the default ~/.opencode-meta location documented in v1spec.md.
    explicit StorageManager(const QString &rootOverride = QString());

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

    // Preferred providers for Models Browser (new)
    bool savePreferredProviders(const QSet<QString> &providers) const; // preferred-providers.json
    QSet<QString> loadPreferredProviders() const;                      // empty set on failure

private:
    QString m_rootOverride; // empty = use default under home directory
    QString rootPath() const;
};
