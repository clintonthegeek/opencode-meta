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
      bool deleteRole(const QString &id) const;     // removes roles/<id>.json; false if missing
      bool isStockRole(const Role &role) const;

     // Specialists
     bool saveSpecialist(const Specialist &s) const;   // specialists/<id>.json
     Specialist loadSpecialist(const QString &id) const; // returns empty Specialist on failure
     QList<Specialist> listSpecialists() const;         // scans specialists/ directory

       // Teams
       bool saveTeam(const Team &team) const;        // teams/<id>.json
       Team loadTeam(const QString &id) const;       // returns empty Team on failure
       QList<Team> listTeams() const;                // scans teams/ directory
       bool deleteTeam(const QString &id) const;     // removes teams/<id>.json; false if missing
       bool isStockTeam(const Team &team) const;

  // Trials
  bool saveTrial(const Trial &trial) const;     // trials/<id>.json
  Trial loadTrial(const QString &id) const;     // returns empty Trial on failure
  QList<Trial> listTrials() const;              // scans trials/ directory
  bool deleteTrial(const QString &id) const;    // removes trials/<id>.json; false if missing

  // Seed default PARADIGM entities on first run.
  // See docs/PARADIGM.md (built-in Roles build/plan/general and a starter Team).
  //
  // Phase D1 / D-9: the seed mirrors stock opencode's seven native
  // agents (build/plan/general/explore/compaction/title/summary) instead
  // of the v0 fiction approximation. Per-agent permission rulesets live
  // in `kStockDefaults` (StorageManager.cpp anon-ns) and are sourced
  // verbatim from `agent.ts:119-264`. Each seeded Role carries
  // `metadata.native = true` so the editor can badge it (D-10); the three
  // hidden Roles carry `metadata.hidden = true` in addition (D3-3
  // filter chip). Existing user data is sacred — see seedDefaultsIfNeeded
  // early-out (StorageManager.cpp:924) and D4-2.
  void seedDefaultsIfNeeded() const;

  // Phase D1-2 / §3: track which seed generation is in place so future
  // migrations can run without clobbering user data. The QSettings key
  // lives under "storage/seed_version" so an empty / pre-D1 storage
  // root seeded by the v0 fiction path reads as v0_legacyFiction and
  // a freshly-seeded root reads as v1_stockFidelity.
  enum class SeedVersion {
    v0_legacyFiction = 0,
    v1_stockFidelity = 1,
  };
  static SeedVersion readSeedVersion();
  static void writeSeedVersion(SeedVersion v);

      // Apply a Team to a concrete project path by rendering it to a
      // project-local opencode.json and recording a minimal Trial +
      // project association.
     bool applyTeamToProject(const QString &projectPath,
                             const QString &teamId) const;

private:
     QString m_rootOverride; // empty = use default under home directory
     QString rootPath() const;

     // Read the optional storage root override from QSettings
     // (key "settings/storage_root_path"). Returns a usable directory path
     // if the value is non-empty and points to an existing directory;
     // otherwise returns an empty string so callers can fall back silently
     // to the default (~/.opencode-meta).
     static QString readPreferencesOverride();

     // Phase D1-5: helper that writes the v1 stock-aligned Starter
     // Team + four Specialists when teams/ is empty. Called from
     // seedDefaultsIfNeeded when the
     // `settings/seed_stock_defaults` checkbox is on (default true).
     void seedStarterTeamStock(bool teamsEmpty) const;
};
