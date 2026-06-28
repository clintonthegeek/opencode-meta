/**
 * \file StorageManager.h
 * \brief Centralized storage manager for reading/writing JSON data models under ~/.opencode-meta/.
 *
 * This class handles persistence for templates, profiles, models cache, projects, and now preferred providers.
 * All operations use Qt's JSON facilities and file I/O, with error logging via qDebug(). For the current
 * on-disk layout, see README.md; older descriptions in historical docs (such as the original v1 spec) are
 * kept for context only.
 */
#pragma once

#include <QDir>
#include <QList>
#include <QString>
#include <QSet>

#include "models/ModelInfo.h"
#include "models/ProjectRecord.h"
#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "models/Trial.h"

// Centralized storage manager for reading/writing JSON data models
// under ~/.opencode-meta/ (see README.md for the current layout).
class StorageManager
{
public:
    // Optional rootOverride is primarily for tests; in the application we
    // rely on the default ~/.opencode-meta location described in README.md.
    explicit StorageManager(const QString &rootOverride = QString());

    // Ensure that the root directory (~/.opencode-meta/) exists.
    void ensureRoot() const;

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

     // PARADIGM entities (see docs/PARADIGM.md)

     // Roles
     bool saveRole(const Role &role) const;        // roles/<id>.json
     Role loadRole(const QString &id) const;       // returns empty Role on failure
     QList<Role> listRoles() const;                // scans roles/ directory

     // Specialists
     bool saveSpecialist(const Specialist &s) const;   // specialists/<id>.json
     Specialist loadSpecialist(const QString &id) const; // returns empty Specialist on failure
     QList<Specialist> listSpecialists() const;         // scans specialists/ directory

      // Teams
      bool saveTeam(const Team &team) const;        // teams/<id>.json
      Team loadTeam(const QString &id) const;       // returns empty Team on failure
      QList<Team> listTeams() const;                // scans teams/ directory
      bool deleteTeam(const QString &id) const;     // removes teams/<id>.json; false if missing

  // Trials
  bool saveTrial(const Trial &trial) const;     // trials/<id>.json
  Trial loadTrial(const QString &id) const;     // returns empty Trial on failure
  QList<Trial> listTrials() const;              // scans trials/ directory

  // Seed default PARADIGM entities on first run.
  // See docs/PARADIGM.md (built-in Roles build/plan/general and a starter Team).
  void seedDefaultsIfNeeded() const;

      // Apply a Team to a concrete project path by rendering it to a
      // project-local opencode.json and recording a minimal Trial +
      // project association.
     bool applyTeamToProject(const QString &projectPath,
                             const QString &teamId) const;

private:
     QString m_rootOverride; // empty = use default under home directory
     QString rootPath() const;
};
