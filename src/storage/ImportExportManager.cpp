#include "storage/ImportExportManager.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>

// Qt-private ZIP APIs. The "private" prefix is intentional; see the
// header warning block for the rationale.
#include <private/qzipreader_p.h>
#include <private/qzipwriter_p.h>

#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"
#include "storage/StorageManager.h"

namespace {

constexpr const char *kManifestFieldFormat          = "format";
constexpr const char *kManifestFieldFormatVersion   = "formatVersion";
constexpr const char *kManifestFieldCreatedUtc      = "createdUtc";
constexpr const char *kManifestFieldSource          = "source";
constexpr const char *kManifestFieldSourceVersion   = "sourceVersion";
constexpr const char *kManifestFieldNotes           = "notes";
constexpr const char *kManifestFieldCounts          = "counts";
constexpr const char *kManifestFieldIncluded        = "included";
constexpr const char *kManifestCountRoles           = "roles";
constexpr const char *kManifestCountTeams           = "teams";
constexpr const char *kManifestCountSpecialists     = "specialists";
constexpr const char *kManifestIncludedRoles        = "roles";
constexpr const char *kManifestIncludedTeams        = "teams";
constexpr const char *kManifestIncludedSpecialists  = "specialists";

QStringList readStringList(const QJsonObject &parent, const QString &key)
{
    QStringList out;
    const QJsonValue value = parent.value(key);
    if (!value.isArray()) {
        return out;
    }
    const QJsonArray array = value.toArray();
    out.reserve(array.size());
    for (const QJsonValue &entry : array) {
        if (entry.isString()) {
            out.append(entry.toString());
        }
    }
    return out;
}

QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QString normalizedZipPath(const QString &raw)
{
    // Accept file:// URLs from QFileDialog::getOpenFileName as well as
    // plain paths -- the upper layers sometimes give us either.
    if (raw.startsWith(QStringLiteral("file://"))) {
        return QUrl(raw).toLocalFile();
    }
    return raw;
}

} // namespace

ImportExportManager::ImportExportManager(StorageManager &storageManager)
    : m_storageManager(storageManager)
{
}

QJsonObject ImportExportManager::Manifest::toJson() const
{
    QJsonObject obj;
    obj.insert(QString::fromLatin1(kManifestFieldFormat),
               QString::fromLatin1(kFormatTag));
    obj.insert(QString::fromLatin1(kManifestFieldFormatVersion),
               formatVersion);
    obj.insert(QString::fromLatin1(kManifestFieldCreatedUtc),
               createdUtc.toUTC().toString(Qt::ISODateWithMs));
    obj.insert(QString::fromLatin1(kManifestFieldSource),
               source);
    obj.insert(QString::fromLatin1(kManifestFieldSourceVersion),
               sourceVersion);
    obj.insert(QString::fromLatin1(kManifestFieldNotes),
               notes);

    QJsonObject counts;
    counts.insert(QString::fromLatin1(kManifestCountRoles),
                  includedRoleIds.size());
    counts.insert(QString::fromLatin1(kManifestCountTeams),
                  includedTeamIds.size());
    counts.insert(QString::fromLatin1(kManifestCountSpecialists),
                  includedSpecialistIds.size());
    obj.insert(QString::fromLatin1(kManifestFieldCounts), counts);

    QJsonObject included;
    included.insert(QString::fromLatin1(kManifestIncludedRoles),
                    toJsonArray(includedRoleIds));
    included.insert(QString::fromLatin1(kManifestIncludedTeams),
                    toJsonArray(includedTeamIds));
    included.insert(QString::fromLatin1(kManifestIncludedSpecialists),
                    toJsonArray(includedSpecialistIds));
    obj.insert(QString::fromLatin1(kManifestFieldIncluded), included);

    return obj;
}

ImportExportManager::Manifest ImportExportManager::Manifest::fromJson(
    const QJsonObject &obj)
{
    Manifest m;

    const QString formatTag = obj.value(QString::fromLatin1(kManifestFieldFormat))
                                  .toString();
    if (formatTag != QString::fromLatin1(kFormatTag)) {
        m.errorString = QStringLiteral(
            "Manifest 'format' tag is not '%1' (got '%2').")
            .arg(QString::fromLatin1(kFormatTag), formatTag);
        return m;
    }

    const int version = obj.value(QString::fromLatin1(kManifestFieldFormatVersion))
                            .toInt(-1);
    if (version <= 0) {
        m.errorString = QStringLiteral(
            "Manifest is missing a positive 'formatVersion'.");
        return m;
    }
    if (version > kSupportedFormatVersion) {
        m.errorString = QStringLiteral(
            "Bundle formatVersion=%1 is newer than supported (%2). "
            "Upgrade opencode-meta-qt before importing.")
            .arg(version)
            .arg(kSupportedFormatVersion);
        return m;
    }

    m.formatVersion    = version;
    m.createdUtc       = QDateTime::fromString(
        obj.value(QString::fromLatin1(kManifestFieldCreatedUtc)).toString(),
        Qt::ISODateWithMs);
    m.source           = obj.value(QString::fromLatin1(kManifestFieldSource))
                              .toString();
    m.sourceVersion    = obj.value(QString::fromLatin1(kManifestFieldSourceVersion))
                              .toString();
    m.notes            = obj.value(QString::fromLatin1(kManifestFieldNotes))
                              .toString();

    const QJsonObject included = obj.value(QString::fromLatin1(kManifestFieldIncluded))
                                       .toObject();
    m.includedRoleIds        = readStringList(included, QString::fromLatin1(kManifestIncludedRoles));
    m.includedTeamIds        = readStringList(included, QString::fromLatin1(kManifestIncludedTeams));
    m.includedSpecialistIds  = readStringList(included, QString::fromLatin1(kManifestIncludedSpecialists));

    m.valid = true;
    return m;
}

QList<QString> ImportExportManager::transitiveSpecialistIds(
    const QList<QString> &teamIds) const
{
    QSet<QString> seen;
    for (const QString &teamId : teamIds) {
        const Team team = m_storageManager.loadTeam(teamId);
        if (team.id.isEmpty()) {
            // Silently skip missing teams. Export validates names
            // separately; this helper is intentionally best-effort so
            // it can be called from MainWindow preview paths without
            // having to surface error dialogs from inside a getter.
            continue;
        }
        for (const auto &binding : team.specialists) {
            if (!binding.specialistId.isEmpty()) {
                seen.insert(binding.specialistId);
            }
        }
    }

    QStringList sorted = seen.values();
    sorted.sort();
    return sorted;
}

ImportExportManager::ExportResult ImportExportManager::exportBundle(
    const QString &zipPath,
    const ExportRequest &request)
{
    ExportResult result;
    result.outputPath = normalizedZipPath(zipPath);

    if (result.outputPath.isEmpty()) {
        result.errorString = QStringLiteral("Export destination path is empty.");
        return result;
    }

    if (request.roleIds.isEmpty() && request.teamIds.isEmpty()) {
        result.errorString = QStringLiteral(
            "Select at least one Role or Team before exporting a bundle.");
        return result;
    }

    // Pre-flight: confirm all requested roles + teams exist; fail
    // early so the caller can show the user *which* id was missing
    // rather than silently producing a partial bundle. Specialists are
    // discovered below from the teams; explicit specialist ids in the
    // request are not part of v1 (the format documents this).
    for (const QString &id : request.roleIds) {
        if (id.isEmpty()) {
            continue;
        }
        if (m_storageManager.loadRole(id).id.isEmpty()) {
            result.errorString = QStringLiteral(
                "Requested Role '%1' does not exist in storage.").arg(id);
            return result;
        }
    }
    for (const QString &id : request.teamIds) {
        if (id.isEmpty()) {
            continue;
        }
        if (m_storageManager.loadTeam(id).id.isEmpty()) {
            result.errorString = QStringLiteral(
                "Requested Team '%1' does not exist in storage.").arg(id);
            return result;
        }
    }

    // Build the lists of Specialist ids actually referenced.
    QStringList specialistIds = transitiveSpecialistIds(request.teamIds);

    // Confirm those Specialists exist too; a Team referencing a
    // dangling specialist id would still render but we'd rather not
    // ship a bundle with broken pointers.
    for (const QString &id : specialistIds) {
        if (m_storageManager.loadSpecialist(id).id.isEmpty()) {
            result.errorString = QStringLiteral(
                "Team references Specialist '%1' which is missing from storage.")
                .arg(id);
            return result;
        }
    }

    // De-dup + sort worksite ids so the manifest is stable across
    // runs. The same QSet would normally be used for the inclusion
    // lists; we use a set here so we don't write the same JSON twice.
    QSet<QString> roleSet(request.roleIds.begin(), request.roleIds.end());
    roleSet.remove(QString());
    QStringList roleIdsSorted = roleSet.values();
    roleIdsSorted.sort();

    QSet<QString> teamSet(request.teamIds.begin(), request.teamIds.end());
    teamSet.remove(QString());
    QStringList teamIdsSorted = teamSet.values();
    teamIdsSorted.sort();

    Manifest manifest;
    manifest.formatVersion           = kSupportedFormatVersion;
    manifest.createdUtc              = QDateTime::currentDateTimeUtc();
    manifest.source                  = QString::fromLatin1(kSourceLabel);
    manifest.sourceVersion           = QString::fromLatin1(kSourceVersion);
    manifest.notes                   = request.notes;
    manifest.includedRoleIds         = roleIdsSorted;
    manifest.includedTeamIds         = teamIdsSorted;
    manifest.includedSpecialistIds   = specialistIds;

    // QZipWriter's constructor takes the destination path; if the
    // file already exists it is overwritten. The destructor flushes
    // the central directory, so we must let it run before reporting
    // success.
    QZipWriter writer(result.outputPath);
    if (!writer.isWritable()) {
        result.errorString = QStringLiteral(
            "Could not open '%1' for writing as a zip bundle.")
            .arg(result.outputPath);
        return result;
    }

    // Write manifest first, then entity folders. The ordering does
    // not affect the bundle's semantics but makes the file slightly
    // easier to skim with `unzip -l` (manifest jumps out at the top).
    {
        const QJsonDocument doc(manifest.toJson());
        writer.addFile(QString::fromLatin1(kManifestFilename),
                       doc.toJson(QJsonDocument::Indented));
    }

    auto writeOneJson = [&writer](const QString &dir,
                                  const QString &id,
                                  const QJsonObject &obj) {
        const QString path = dir + id + QStringLiteral(".json");
        const QJsonDocument doc(obj);
        writer.addFile(path, doc.toJson(QJsonDocument::Indented));
    };

    for (const QString &roleId : std::as_const(roleIdsSorted)) {
        const Role role = m_storageManager.loadRole(roleId);
        writeOneJson(QString::fromLatin1(kRolesDir), role.id, role.toJson());
        ++result.rolesWritten;
    }

    for (const QString &teamId : std::as_const(teamIdsSorted)) {
        const Team team = m_storageManager.loadTeam(teamId);
        writeOneJson(QString::fromLatin1(kTeamsDir), team.id, team.toJson());
        ++result.teamsWritten;
    }

    for (const QString &specialistId : std::as_const(specialistIds)) {
        const Specialist specialist = m_storageManager.loadSpecialist(specialistId);
        writeOneJson(QString::fromLatin1(kSpecialistsDir),
                     specialist.id,
                     specialist.toJson());
        ++result.specialistsWritten;
    }

    // Closing happens in QZipWriter's destructor (scoped above).
    // Once we leave the brace the central directory is written.
    writer.close();

    result.success = (QFileInfo(result.outputPath).size() > 0);
    if (!result.success) {
        result.errorString = QStringLiteral(
            "Bundle writer reached end-of-input but '%1' is empty.")
            .arg(result.outputPath);
    }
    return result;
}

ImportExportManager::Manifest ImportExportManager::readManifest(
    const QString &zipPath)
{
    Manifest bad;
    const QString normalized = normalizedZipPath(zipPath);

    if (normalized.isEmpty() || !QFileInfo(normalized).isFile()) {
        bad.errorString = QStringLiteral("Bundle file is not readable: %1")
                              .arg(normalized);
        return bad;
    }

    QZipReader reader(normalized);
    if (!reader.isReadable()) {
        bad.errorString = QStringLiteral(
            "Could not open '%1' as a zip bundle.").arg(normalized);
        return bad;
    }

    const QByteArray bytes = reader.fileData(QString::fromLatin1(kManifestFilename));
    reader.close();

    if (bytes.isEmpty()) {
        bad.errorString = QStringLiteral(
            "Bundle '%1' is missing a top-level manifest.json.").arg(normalized);
        return bad;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        bad.errorString = QStringLiteral(
            "Bundle manifest is not valid JSON: %1").arg(parseError.errorString());
        return bad;
    }

    return Manifest::fromJson(doc.object());
}

ImportExportManager::ImportResult ImportExportManager::importBundle(
    const QString &zipPath)
{
    ImportResult result;
    result.inputPath = normalizedZipPath(zipPath);

    if (result.inputPath.isEmpty() || !QFileInfo(result.inputPath).isFile()) {
        result.errorString = QStringLiteral("Bundle file is not readable: %1")
                                  .arg(result.inputPath);
        return result;
    }

    QZipReader reader(result.inputPath);
    if (!reader.isReadable()) {
        result.errorString = QStringLiteral(
            "Could not open '%1' as a zip bundle.").arg(result.inputPath);
        return result;
    }

    // First pass: validate the manifest so we never half-import.
    const QByteArray manifestBytes =
        reader.fileData(QString::fromLatin1(kManifestFilename));
    if (manifestBytes.isEmpty()) {
        result.errorString = QStringLiteral(
            "Bundle '%1' is missing a top-level manifest.json.")
            .arg(result.inputPath);
        reader.close();
        return result;
    }

    QJsonParseError parseError{};
    const QJsonDocument manifestDoc =
        QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifestDoc.isObject()) {
        result.errorString = QStringLiteral(
            "Bundle manifest is not valid JSON: %1").arg(parseError.errorString());
        reader.close();
        return result;
    }

    const Manifest manifest = Manifest::fromJson(manifestDoc.object());
    if (!manifest.valid) {
        result.errorString = QStringLiteral(
            "Bundle '%1' has an invalid manifest: %2")
            .arg(result.inputPath, manifest.errorString);
        reader.close();
        return result;
    }

    // Build a quick lookup for the zip's filename -> bytes. Doing this
    // once is cheaper than letting QZipReader do file lookup for every
    // entity on the slow path.
    const QList<QZipReader::FileInfo> entries = reader.fileInfoList();
    QHash<QString, QByteArray> files;
    files.reserve(entries.size());
    for (const QZipReader::FileInfo &info : entries) {
        if (info.isDir || info.isSymLink) {
            continue;
        }
        files.insert(info.filePath, reader.fileData(info.filePath));
    }
    reader.close();

    auto importJsonFile = [&](const QString &dir,
                              const QString &id,
                              const QString &kindForError) -> bool {
        const QString path = dir + id + QStringLiteral(".json");
        const QByteArray bytes = files.value(path);
        if (bytes.isEmpty()) {
            result.errorString = QStringLiteral(
                "Manifest lists %1 '%2' but the bundle contains no '%3'.")
                .arg(kindForError, id, path);
            return false;
        }
        QJsonParseError localError{};
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &localError);
        if (localError.error != QJsonParseError::NoError || !doc.isObject()) {
            result.errorString = QStringLiteral(
                "Bundle entry '%1' is not valid JSON: %2")
                .arg(path, localError.errorString());
            return false;
        }
        return true;
    };

    QStringList newRoleIds;
    QStringList overwritingRoleIds;
    for (const QString &id : manifest.includedRoleIds) {
        if (id.isEmpty()) {
            continue;
        }
        if (!importJsonFile(QString::fromLatin1(kRolesDir), id, QStringLiteral("Role"))) {
            return result;
        }
        const bool existedAlready =
            !m_storageManager.loadRole(id).id.isEmpty();
        if (existedAlready) {
            overwritingRoleIds.append(id);
        } else {
            newRoleIds.append(id);
        }
    }

    QStringList newTeamIds;
    QStringList overwritingTeamIds;
    for (const QString &id : manifest.includedTeamIds) {
        if (id.isEmpty()) {
            continue;
        }
        if (!importJsonFile(QString::fromLatin1(kTeamsDir), id, QStringLiteral("Team"))) {
            return result;
        }
        const bool existedAlready =
            !m_storageManager.loadTeam(id).id.isEmpty();
        if (existedAlready) {
            overwritingTeamIds.append(id);
        } else {
            newTeamIds.append(id);
        }
    }

    QStringList newSpecialistIds;
    QStringList overwritingSpecialistIds;
    for (const QString &id : manifest.includedSpecialistIds) {
        if (id.isEmpty()) {
            continue;
        }
        if (!importJsonFile(QString::fromLatin1(kSpecialistsDir),
                            id,
                            QStringLiteral("Specialist"))) {
            return result;
        }
        const bool existedAlready =
            !m_storageManager.loadSpecialist(id).id.isEmpty();
        if (existedAlready) {
            overwritingSpecialistIds.append(id);
        } else {
            newSpecialistIds.append(id);
        }
    }

    // Second pass: actually write to storage now that we have a
    // complete picture. Order matters -- Specialists before Teams,
    // Roles before Teams, because TeamRenderer / apply path resolves
    // those ids at render time. We do that here manually so a single
    // import round-trip cannot leave Teams pointing at IDs that have
    // been patchily imported.
    auto writeRoleFromBundle = [&](const QString &id) -> bool {
        const QString path = QString::fromLatin1(kRolesDir) + id + QStringLiteral(".json");
        const QByteArray bytes = files.value(path);
        QJsonParseError localError{};
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &localError);
        const Role role = Role::fromJson(doc.object());
        if (role.id.isEmpty() || role.id != id) {
            // Defense-in-depth: the manifest said id 'X' but the JSON
            // inside claims 'Y'. Refuse rather than silently rename.
            result.errorString = QStringLiteral(
                "Bundle entry '%1' has id '%2' but manifest claimed '%3'.")
                .arg(path, role.id, id);
            return false;
        }
        if (!m_storageManager.saveRole(role)) {
            result.errorString = QStringLiteral(
                "StorageManager failed to save bundle Role '%1'.").arg(id);
            return false;
        }
        return true;
    };

    auto writeSpecialistFromBundle = [&](const QString &id) -> bool {
        const QString path = QString::fromLatin1(kSpecialistsDir)
                             + id + QStringLiteral(".json");
        const QByteArray bytes = files.value(path);
        QJsonParseError localError{};
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &localError);
        const Specialist specialist = Specialist::fromJson(doc.object());
        if (specialist.id.isEmpty() || specialist.id != id) {
            result.errorString = QStringLiteral(
                "Bundle entry '%1' has id '%2' but manifest claimed '%3'.")
                .arg(path, specialist.id, id);
            return false;
        }
        if (!m_storageManager.saveSpecialist(specialist)) {
            result.errorString = QStringLiteral(
                "StorageManager failed to save bundle Specialist '%1'.").arg(id);
            return false;
        }
        return true;
    };

    auto writeTeamFromBundle = [&](const QString &id) -> bool {
        const QString path = QString::fromLatin1(kTeamsDir)
                             + id + QStringLiteral(".json");
        const QByteArray bytes = files.value(path);
        QJsonParseError localError{};
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &localError);
        const Team team = Team::fromJson(doc.object());
        if (team.id.isEmpty() || team.id != id) {
            result.errorString = QStringLiteral(
                "Bundle entry '%1' has id '%2' but manifest claimed '%3'.")
                .arg(path, team.id, id);
            return false;
        }
        if (!m_storageManager.saveTeam(team)) {
            result.errorString = QStringLiteral(
                "StorageManager failed to save bundle Team '%1'.").arg(id);
            return false;
        }
        return true;
    };

    // Specialists first -- referenced by Teams.
    for (const QString &id : std::as_const(newSpecialistIds)) {
        if (!writeSpecialistFromBundle(id)) {
            return result;
        }
    }
    for (const QString &id : std::as_const(overwritingSpecialistIds)) {
        if (!writeSpecialistFromBundle(id)) {
            return result;
        }
    }

    // Roles next -- referenced by Teams indirectly (Specialists carry
    // roleId) but it is safer to import them before teams in case a
    // consumer ever wires TeamRenderer here.
    for (const QString &id : std::as_const(newRoleIds)) {
        if (!writeRoleFromBundle(id)) {
            return result;
        }
    }
    for (const QString &id : std::as_const(overwritingRoleIds)) {
        if (!writeRoleFromBundle(id)) {
            return result;
        }
    }

    // Teams last.
    for (const QString &id : std::as_const(newTeamIds)) {
        if (!writeTeamFromBundle(id)) {
            return result;
        }
    }
    for (const QString &id : std::as_const(overwritingTeamIds)) {
        if (!writeTeamFromBundle(id)) {
            return result;
        }
    }

    result.success = true;
    result.importedRoleIds          = std::move(newRoleIds);
    result.importedTeamIds          = std::move(newTeamIds);
    result.importedSpecialistIds    = std::move(newSpecialistIds);
    result.overwrittenRoleIds       = std::move(overwritingRoleIds);
    result.overwrittenTeamIds       = std::move(overwritingTeamIds);
    result.overwrittenSpecialistIds = std::move(overwritingSpecialistIds);
    return result;
}
