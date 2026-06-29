// ImportExportManager.h
//
// ROADMAP P3-2: zip-based import/export bundles for selected Roles +
// Teams + their transitive Specialists.
//
// The format is intentionally a *single zip file* with a top-level
// `manifest.json` plus one folder per entity type:
//
//   my-bundle.zip
//     manifest.json                     -- format, version, timestamp, counts
//     roles/<id>.json                   -- Role::toJson()
//     teams/<id>.json                   -- Team::toJson()
//     specialists/<id>.json             -- Specialist::toJson()
//
// This shape means a user can `unzip -l my-bundle.zip` and immediately
// see what is inside without re-installing opencode-meta-qt, while the
// manifest gives us a stable machine-readable summary that importers
// can validate against a format version before touching anything.
//
// WARNING: QZipReader / QZipWriter are Qt-private APIs -- they live in
// <private/qzip{reader,writer}_p.h> under QtCore. We deliberately use
// them rather than adding a QuaZip dependency because the alternative
// pulls in a non-KDE kit under permissive licensing that we cannot
// guarantee is available. The QtCore-private API has been stable
// across Qt 5.15 through Qt 6.x for years; if Qt ever breaks it the
// fallback is to swap our BundleWriter / BundleReader shims behind a
// stable interface.

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

class QJsonObject;
class StorageManager;

class ImportExportManager
{
public:
    // Bundle writer request: caller picks the ids of Roles and Teams
    // they want to include. Specialists are auto-discovered by walking
    // each Team's SpecialistBinding list, so callers do not need to
    // think about which Specialists their Teams reference. Optional
    // notes string lets users tag bundles ("Q3 demo set", etc.); empty
    // is fine.
    struct ExportRequest {
        QList<QString> roleIds;
        QList<QString> teamIds;
        QString notes;
    };

    // Result of a bundle export. Roles/Teams/Specialists counts are
    // the *actual* number of entities the manager wrote to the zip
    // (post de-dup + transitive expansion), not the size of the
    // request, so callers can present a precise "we saved N things"
    // status without having to re-read the bundle themselves.
    struct ExportResult {
        bool success = false;
        QString errorString;
        QString outputPath;
        int rolesWritten = 0;
        int teamsWritten = 0;
        int specialistsWritten = 0;
    };

    // Result of a bundle import. imported* lists contain entities
    // that were new to storage; overwritten* lists contain entities
    // whose existing copy was replaced. Both are sorted ascending by
    // id (stable ordering) so the status bar message can be diffed
    // across runs.
    struct ImportResult {
        bool success = false;
        QString errorString;
        QString inputPath;
        QList<QString> importedRoleIds;
        QList<QString> importedTeamIds;
        QList<QString> importedSpecialistIds;
        QList<QString> overwrittenRoleIds;
        QList<QString> overwrittenTeamIds;
        QList<QString> overwrittenSpecialistIds;
    };

    // Decoded manifest. `valid` is true iff the bundle carried a
    // manifest we successfully parsed; on parse errors `errorString`
    // explains why and the included* lists are empty.
    struct Manifest {
        int formatVersion = 0;
        QDateTime createdUtc;
        QString source;
        QString sourceVersion;
        QString notes;
        QList<QString> includedRoleIds;
        QList<QString> includedTeamIds;
        QList<QString> includedSpecialistIds;

        bool valid = false;
        QString errorString;

        QJsonObject toJson() const;
        static Manifest fromJson(const QJsonObject &obj);
    };

    // Format-level constants. Exposed so tests can assert against
    // them without re-declaring string literals.
    constexpr static int         kSupportedFormatVersion = 1;
    constexpr static const char *kFormatTag              = "opencode-meta-bundle";
    constexpr static const char *kRolesDir              = "roles/";
    constexpr static const char *kTeamsDir              = "teams/";
    constexpr static const char *kSpecialistsDir        = "specialists/";
    constexpr static const char *kManifestFilename      = "manifest.json";
    constexpr static const char *kSourceLabel           = "opencode-meta-qt";
    constexpr static const char *kSourceVersion         = "0.1.0";

    explicit ImportExportManager(StorageManager &storageManager);
    ~ImportExportManager() = default;

    // Write a bundle to zipPath. Returns success/failure with details.
    // The zip is closed before this function returns; partial writes
    // are removed on failure so we never leave a corrupt file behind.
    ExportResult exportBundle(const QString &zipPath,
                              const ExportRequest &request);

    // Read everything from zipPath into the bound StorageManager.
    // Existing entities with the same id are *overwritten* -- this is
    // the documented behavior; the caller is expected to have warned
    // the user via the dialog before invoking the import.
    ImportResult importBundle(const QString &zipPath);

    // Read the manifest out of a bundle without touching storage.
    // Used by the dialog preview pane so the user gets a confirmation
    // surface before they hit Accept.
    Manifest readManifest(const QString &zipPath);

    // Helper: ids of the Specialists that any Team in `teamIds`
    // references. Returns a sorted, de-duplicated list (QSet-backed,
    // then re-laid in alphabetical order so the manifest is stable
    // across runs).
    QList<QString> transitiveSpecialistIds(const QList<QString> &teamIds) const;

private:
    StorageManager &m_storageManager;
};
