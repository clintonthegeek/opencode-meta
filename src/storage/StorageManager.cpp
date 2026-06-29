#include "storage/StorageManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>

#include "apply_helpers.h"
#include "generation/AgentMarkdown.h"
#include "generation/ProviderCatalog.h"
#include "generation/TeamRenderer.h"

StorageManager::StorageManager(const QString &rootOverride)
    : m_rootOverride(rootOverride)
{
}

QString StorageManager::rootPath() const
{
    // 1. Tests / explicit callers: constructor-injected override always wins
    //    so unit tests stay isolated from any user-level preference.
    if (!m_rootOverride.isEmpty()) {
        return m_rootOverride;
    }

    // 2. Optional override from Preferences (QSettings key
    //    "settings/storage_root_path"). Falls back silently when missing,
    //    empty, malformed, or pointing at a non-existent directory so a
    //    bad value never breaks seeding or first-run behavior.
    const QString prefRoot = readPreferencesOverride();
    if (!prefRoot.isEmpty()) {
        return prefRoot;
    }

    // 3. Default location under the user's HOME directory.
    return QDir::homePath() + QStringLiteral("/.opencode-meta");
}

QString StorageManager::readPreferencesOverride()
{
    // QSettings() with no args requires a QCoreApplication (e.g. tests that
    // never construct one). Without this guard we would crash instead of
    // gracefully falling back to the default root.
    if (QCoreApplication::instance() == nullptr) {
        return QString();
    }

    const QSettings settings;
    const QString raw = settings
                            .value(QStringLiteral("settings/storage_root_path"))
                            .toString()
                            .trimmed();
    if (raw.isEmpty()) {
        return QString();
    }

    const QString cleaned = QDir::cleanPath(raw);
    if (!QDir(cleaned).exists()) {
        // Silent fallback: the override is not a usable directory.
        return QString();
    }

    return cleaned;
}

void StorageManager::ensureRoot() const
{
    QDir rootDir(rootPath());
    if (rootDir.exists()) {
        return;
    }

    if (!rootDir.mkpath(QStringLiteral("."))) {
        qDebug() << "StorageManager: failed to create root directory" << rootDir.path();
    }
}







bool StorageManager::saveModelsCache(const ModelsCache &cache) const
{
    ensureRoot();

    const QString cacheFilePath = rootPath() + QStringLiteral("/models-cache.json");
    QFile file(cacheFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open models cache file for writing" << cacheFilePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(cache.toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write complete models cache file" << cacheFilePath;
        return false;
    }

    return true;
}

ModelsCache StorageManager::loadModelsCache() const
{
    ModelsCache cache;

    const QString cacheFilePath = rootPath() + QStringLiteral("/models-cache.json");
    QFile file(cacheFilePath);
    if (!file.exists()) {
        // Not an error: cache just has not been created yet.
        return cache;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open models cache file for reading" << cacheFilePath << file.errorString();
        return cache;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "StorageManager: failed to parse models cache JSON" << cacheFilePath << parseError.errorString();
        return cache;
    }

    cache = ModelsCache::fromJson(doc.object());
    return cache;
}

bool StorageManager::saveProjects(const QList<ProjectRecord> &projects) const
{
    ensureRoot();

    QJsonArray array;
    for (const ProjectRecord &record : projects) {
        array.append(record.toJson());
    }

    const QString projectsFilePath = rootPath() + QStringLiteral("/projects.json");
    QFile file(projectsFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open projects file for writing" << projectsFilePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(array);
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write complete projects file" << projectsFilePath;
        return false;
    }

    return true;
}

QList<ProjectRecord> StorageManager::loadProjects() const
{
    QList<ProjectRecord> projects;

    const QString projectsFilePath = rootPath() + QStringLiteral("/projects.json");
    QFile file(projectsFilePath);
    if (!file.exists()) {
        // Not an error: projects may not have been saved yet.
        return projects;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open projects file for reading" << projectsFilePath << file.errorString();
        return projects;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qDebug() << "StorageManager: failed to parse projects JSON" << projectsFilePath << parseError.errorString();
        return projects;
    }

    if (!doc.isArray()) {
        qDebug() << "StorageManager: projects JSON is not an array" << projectsFilePath;
        return projects;
    }

    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        projects.append(ProjectRecord::fromJson(value.toObject()));
    }

    return projects;
}



// Preferred providers for Models Browser (new)
bool StorageManager::savePreferredProviders(const QSet<QString> &providers) const
{
    ensureRoot();

    QJsonArray array;
    for (const QString &provider : providers) {
        array.append(provider);
    }

    const QString filePath = rootPath() + QStringLiteral("/preferred-providers.json");
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open preferred-providers.json for writing" << filePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(array);
    const QByteArray data = doc.toJson(QJsonDocument::Compact);  // Compact for small set
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write preferred-providers.json" << filePath;
        return false;
    }

    return true;
}

QSet<QString> StorageManager::loadPreferredProviders() const
{
    QSet<QString> providers;

    const QString filePath = rootPath() + QStringLiteral("/preferred-providers.json");
    QFile file(filePath);
    if (!file.exists()) {
        return providers;  // Empty set is default
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open preferred-providers.json for reading" << filePath << file.errorString();
        return providers;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qDebug() << "StorageManager: failed to parse preferred-providers.json" << filePath << parseError.errorString();
        return providers;
    }

    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (value.isString()) {
            providers.insert(value.toString());
        }
    }

    return providers;
}

// Roles (PARADIGM entities)

bool StorageManager::saveRole(const Role &role) const
{
    if (role.id.isEmpty()) {
        qDebug() << "StorageManager: cannot save role without id";
        return false;
    }

    ensureRoot();

    QDir rootDir(rootPath());
    const QString rolesSubdir = QStringLiteral("roles");
    if (!rootDir.mkpath(rolesSubdir)) {
        qDebug() << "StorageManager: failed to create roles directory" << rolesSubdir;
        return false;
    }

    const QString filePath = rootPath() + QStringLiteral("/roles/%1.json").arg(role.id);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open role file for writing" << filePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(role.toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write complete role file" << filePath;
        return false;
    }

    return true;
}

Role StorageManager::loadRole(const QString &id) const
{
    Role result;

    if (id.isEmpty()) {
        qDebug() << "StorageManager: loadRole called with empty id";
        return result;
    }

    const QString filePath = rootPath() + QStringLiteral("/roles/%1.json").arg(id);
    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "StorageManager: role file does not exist" << filePath;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open role file for reading" << filePath << file.errorString();
        return result;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "StorageManager: failed to parse role JSON" << filePath << parseError.errorString();
        return result;
    }

    result = Role::fromJson(doc.object());
    if (result.id.isEmpty()) {
        result.id = id;
    }

    return result;
}

QList<Role> StorageManager::listRoles() const
{
    QList<Role> roles;

    const QString rolesPath = rootPath() + QStringLiteral("/roles");
    QDir dir(rolesPath);
    if (!dir.exists()) {
        return roles;
    }

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (const QString &fileName : files) {
        const QFileInfo info(dir.filePath(fileName));
        const QString id = info.completeBaseName();
        Role role = loadRole(id);
        if (!role.id.isEmpty()) {
            roles.append(role);
        }
    }

    return roles;
}

bool StorageManager::deleteRole(const QString &id) const
{
    if (id.isEmpty()) {
        qDebug() << "StorageManager::deleteRole: empty id";
        return false;
    }

    const QString filePath = rootPath() + QStringLiteral("/roles/%1.json").arg(id);
    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "StorageManager::deleteRole: file does not exist" << filePath;
        return false;
    }
    if (!file.remove()) {
        qDebug() << "StorageManager::deleteRole: failed to remove" << filePath << file.errorString();
        return false;
    }

    return true;
}

// Specialists (PARADIGM entities)

bool StorageManager::saveSpecialist(const Specialist &s) const
{
    if (s.id.isEmpty()) {
        qDebug() << "StorageManager: cannot save specialist without id";
        return false;
    }

    ensureRoot();

    QDir rootDir(rootPath());
    const QString subdir = QStringLiteral("specialists");
    if (!rootDir.mkpath(subdir)) {
        qDebug() << "StorageManager: failed to create specialists directory" << subdir;
        return false;
    }

    const QString filePath = rootPath() + QStringLiteral("/specialists/%1.json").arg(s.id);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open specialist file for writing" << filePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(s.toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write complete specialist file" << filePath;
        return false;
    }

    return true;
}

Specialist StorageManager::loadSpecialist(const QString &id) const
{
    Specialist result;

    if (id.isEmpty()) {
        qDebug() << "StorageManager: loadSpecialist called with empty id";
        return result;
    }

    const QString filePath = rootPath() + QStringLiteral("/specialists/%1.json").arg(id);
    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "StorageManager: specialist file does not exist" << filePath;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open specialist file for reading" << filePath << file.errorString();
        return result;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "StorageManager: failed to parse specialist JSON" << filePath << parseError.errorString();
        return result;
    }

    result = Specialist::fromJson(doc.object());
    if (result.id.isEmpty()) {
        result.id = id;
    }

    return result;
}

QList<Specialist> StorageManager::listSpecialists() const
{
    QList<Specialist> specialists;

    const QString path = rootPath() + QStringLiteral("/specialists");
    QDir dir(path);
    if (!dir.exists()) {
        return specialists;
    }

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (const QString &fileName : files) {
        const QFileInfo info(dir.filePath(fileName));
        const QString id = info.completeBaseName();
        Specialist s = loadSpecialist(id);
        if (!s.id.isEmpty()) {
            specialists.append(s);
        }
    }

    return specialists;
}

// Teams (PARADIGM entities)

bool StorageManager::saveTeam(const Team &team) const
{
    if (team.id.isEmpty()) {
        qDebug() << "StorageManager: cannot save team without id";
        return false;
    }

    ensureRoot();

    QDir rootDir(rootPath());
    const QString subdir = QStringLiteral("teams");
    if (!rootDir.mkpath(subdir)) {
        qDebug() << "StorageManager: failed to create teams directory" << subdir;
        return false;
    }

    const QString filePath = rootPath() + QStringLiteral("/teams/%1.json").arg(team.id);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open team file for writing" << filePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(team.toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write complete team file" << filePath;
        return false;
    }

    return true;
}

Team StorageManager::loadTeam(const QString &id) const
{
    Team result;

    if (id.isEmpty()) {
        qDebug() << "StorageManager: loadTeam called with empty id";
        return result;
    }

    const QString filePath = rootPath() + QStringLiteral("/teams/%1.json").arg(id);
    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "StorageManager: team file does not exist" << filePath;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open team file for reading" << filePath << file.errorString();
        return result;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "StorageManager: failed to parse team JSON" << filePath << parseError.errorString();
        return result;
    }

    result = Team::fromJson(doc.object());
    if (result.id.isEmpty()) {
        result.id = id;
    }

    return result;
}

QList<Team> StorageManager::listTeams() const
{
    QList<Team> teams;

    const QString path = rootPath() + QStringLiteral("/teams");
    QDir dir(path);
    if (!dir.exists()) {
        return teams;
    }

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (const QString &fileName : files) {
        const QFileInfo info(dir.filePath(fileName));
        const QString id = info.completeBaseName();
        Team t = loadTeam(id);
        if (!t.id.isEmpty()) {
            teams.append(t);
        }
    }

    return teams;
}

bool StorageManager::deleteTeam(const QString &id) const
{
    if (id.isEmpty()) {
        qDebug() << "StorageManager::deleteTeam: empty id";
        return false;
    }

    const QString filePath = rootPath() + QStringLiteral("/teams/%1.json").arg(id);
    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "StorageManager::deleteTeam: file does not exist" << filePath;
        return false;
    }
    if (!file.remove()) {
        qDebug() << "StorageManager::deleteTeam: failed to remove" << filePath << file.errorString();
        return false;
    }

    return true;
}

// Trials (PARADIGM entities)

bool StorageManager::saveTrial(const Trial &trial) const
{
    if (trial.id.isEmpty()) {
        qDebug() << "StorageManager: cannot save trial without id";
        return false;
    }

    ensureRoot();

    QDir rootDir(rootPath());
    const QString subdir = QStringLiteral("trials");
    if (!rootDir.mkpath(subdir)) {
        qDebug() << "StorageManager: failed to create trials directory" << subdir;
        return false;
    }

    const QString filePath = rootPath() + QStringLiteral("/trials/%1.json").arg(trial.id);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qDebug() << "StorageManager: failed to open trial file for writing" << filePath << file.errorString();
        return false;
    }

    const QJsonDocument doc(trial.toJson());
    const QByteArray data = doc.toJson(QJsonDocument::Indented);
    const qint64 written = file.write(data);
    if (written != data.size()) {
        qDebug() << "StorageManager: failed to write complete trial file" << filePath;
        return false;
    }

    return true;
}

Trial StorageManager::loadTrial(const QString &id) const
{
    Trial result;

    if (id.isEmpty()) {
        qDebug() << "StorageManager: loadTrial called with empty id";
        return result;
    }

    const QString filePath = rootPath() + QStringLiteral("/trials/%1.json").arg(id);
    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "StorageManager: trial file does not exist" << filePath;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "StorageManager: failed to open trial file for reading" << filePath << file.errorString();
        return result;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qDebug() << "StorageManager: failed to parse trial JSON" << filePath << parseError.errorString();
        return result;
    }

    result = Trial::fromJson(doc.object());
    if (result.id.isEmpty()) {
        result.id = id;
    }

    return result;
}

QList<Trial> StorageManager::listTrials() const
{
    QList<Trial> trials;

    const QString path = rootPath() + QStringLiteral("/trials");
    QDir dir(path);
    if (!dir.exists()) {
        return trials;
    }

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (const QString &fileName : files) {
        const QFileInfo info(dir.filePath(fileName));
        const QString id = info.completeBaseName();
        Trial t = loadTrial(id);
        if (!t.id.isEmpty()) {
            trials.append(t);
        }
    }

    return trials;
}

bool StorageManager::deleteTrial(const QString &id) const
{
    if (id.isEmpty()) {
        qDebug() << "StorageManager::deleteTrial: empty id";
        return false;
    }

    const QString filePath = rootPath() + QStringLiteral("/trials/%1.json").arg(id);
    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "StorageManager::deleteTrial: file does not exist" << filePath;
        return false;
    }
    if (!file.remove()) {
        qDebug() << "StorageManager::deleteTrial: failed to remove" << filePath << file.errorString();
        return false;
    }

    return true;
}

bool StorageManager::applyTeamToProject(const QString &projectPath,
                                        const QString &teamId) const
{
    if (projectPath.isEmpty() || teamId.isEmpty()) {
        qDebug() << "StorageManager::applyTeamToProject: empty projectPath or teamId";
        return false;
    }

    const Team team = loadTeam(teamId);
    if (team.id.isEmpty()) {
        qDebug() << "StorageManager::applyTeamToProject: failed to load team" << teamId;
        return false;
    }

    // Collect the Specialist and Role ids referenced by this Team.
    QSet<QString> specialistIds;
    QSet<QString> roleIds;
    for (const auto &binding : team.specialists) {
        const QString roleId = binding.roleId;
        const QString specId = binding.specialistId;
        if (!roleId.isEmpty()) {
            roleIds.insert(roleId);
        }
        if (!specId.isEmpty()) {
            specialistIds.insert(specId);
        }
    }

    QMap<QString, Specialist> specialists;
    for (const QString &specId : specialistIds) {
        const Specialist s = loadSpecialist(specId);
        if (s.id.isEmpty()) {
            qDebug() << "StorageManager::applyTeamToProject: missing specialist" << specId;
            continue;
        }
        specialists.insert(s.id, s);
    }

    QMap<QString, Role> roles;
    for (const QString &roleId : roleIds) {
        const Role r = loadRole(roleId);
        if (r.id.isEmpty()) {
            qDebug() << "StorageManager::applyTeamToProject: missing role" << roleId;
            continue;
        }
        roles.insert(r.id, r);
    }

    const QJsonObject config = TeamRenderer::render(team, specialists, roles);

    // Write to <projectRoot>/opencode.json, matching the existing
    // per-project Template/Profile apply behavior.
    QDir projectDir(projectPath);
    if (!projectDir.exists()) {
        qDebug() << "StorageManager::applyTeamToProject: project directory does not exist" << projectPath;
        return false;
    }

    const QString configPath = projectDir.filePath(QStringLiteral("opencode.json"));
    // C0-1: validate-then-write via apply_helpers::commit (the
    // pre-write contract gate per report §12.3).
    // C0-2 / D-2: also thread the process-wide ProviderCatalog through
    // production apply so every emitted model string is checked against
    // the live catalog. When the singleton fails to load (no
    // `<Global.Path.cache>/models.json` on disk, unparseable cache,
    // etc.), apply_helpers::commit REFUSES to write the file and
    // surfaces a clear "provider catalog not loaded" error in
    // `applyResult.errorString`. There is no silent fallback to the
    // structural §8.3 check on the production path per D-2 — "files
    // that nobody can run are useless to users".
    //
    // C2-2: refresh-on-stale gate. We try `ensureReadyForApply(60)` once
    // before handing the catalog to `commit()`. If the cache is
    // already fresh the helper is a no-op (Phase C2-1). If it is stale
    // we shell out to `opencode models --refresh` and reload. A failed
    // refresh degrades to whatever is already on disk; the helper
    // surfaces the truth via `applyResult` so the user sees the real
    // diagnostic ("provider catalog not loaded" / "model not in live
    // catalog") rather than a phantom structural-pass.
    ProviderCatalog &catalog = ProviderCatalog::instance();
    if (!catalog.ensureReadyForApply(/*maxAgeMinutes=*/60)) {
        // Even if stale-cache was the cause, the surface error has to
        // come from `commit()` so the user always sees the same
        // "provider catalog not loaded" wording (D-2 + apply_helpers).
        // The helper log above already noted the refresh path.
        qDebug() << "StorageManager::applyTeamToProject: ensureReadyForApply "
                    "returned false; deferring error to commit() for unified "
                    "user-facing wording";
    }
    const ApplyResult applyResult = commit(configPath,
                                           config,
                                           &catalog);
    if (!applyResult.success) {
        qDebug() << "StorageManager::applyTeamToProject: commit failed" << applyResult.errorString;
        return false;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();

    // Record a minimal Trial for this application.
    Trial trial;
    trial.id = QStringLiteral("trial-%1").arg(now.toString(QStringLiteral("yyyyMMddHHmmsszzz")));
    trial.teamId = team.id;
    trial.projectPath = QDir::cleanPath(projectPath);
    trial.timestamp = now;
    trial.renderedConfigSnapshot = config;

    if (!saveTrial(trial)) {
        qDebug() << "StorageManager::applyTeamToProject: failed to save Trial" << trial.id;
    }

    // Update projects.json association (path -> active Team id, last Trial).
    QList<ProjectRecord> projects = loadProjects();
    const QString cleanPath = QDir::cleanPath(projectPath);

    int index = -1;
    for (int i = 0; i < projects.size(); ++i) {
        if (QDir::cleanPath(projects.at(i).path) == cleanPath) {
            index = i;
            break;
        }
    }

    if (index < 0) {
        ProjectRecord record;
        record.path = cleanPath;
        record.profileId.clear();
        record.teamId = team.id;
        record.watchEnabled = false;
        record.lastSync = now;
        record.lastTrialId = trial.id;
        projects.append(record);
    } else {
        ProjectRecord &record = projects[index];
        record.teamId = team.id;
        record.lastSync = now;
        record.lastTrialId = trial.id;
    }

    if (!saveProjects(projects)) {
        qDebug() << "StorageManager::applyTeamToProject: failed to save projects.json";
    }

    // Phase C5-2 / D-8: optional agent-side `.md` emission. Default
    // OFF (the user has not toggled "Also write agent `.md` files" in
    // the Settings dialog). When the toggle is on we drop a
    // `<specialistId>.md` next to `opencode.json` so a future flag
    // flip can pick up the agent bodies without revisiting the apply
    // path. The `.md` is a sidecar — it is NOT loaded by the opencode
    // runtime and never affects `opencode debug config`. Failures are
    // logged but do NOT invalidate the JSON apply (the `.md` emission
    // is best-effort).
    if (QCoreApplication::instance() != nullptr
        && QSettings().value(
               QStringLiteral("settings/write_agent_markdown"),
               QVariant(false)).toBool()) {
        QDir specDir(projectPath);
        const QString mdDirName = QStringLiteral(".opencode/agent");
        if (!specDir.exists(mdDirName)) {
            specDir.mkpath(mdDirName);
        }
        for (auto it = specialists.constBegin(); it != specialists.constEnd(); ++it) {
            const Specialist &s = it.value();
            const QMap<QString, Role>::const_iterator roleIt =
                roles.constFind(s.roleId);
            if (roleIt == roles.constEnd()) {
                continue;
            }
            const QString mdPath =
                QDir(projectPath)
                    .filePath(mdDirName
                              + QStringLiteral("/")
                              + (s.id.isEmpty() ? QStringLiteral("agent")
                                                 : s.id)
                              + QStringLiteral(".md"));
            QFile mdFile(mdPath);
            if (!mdFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                qDebug() << "StorageManager::applyTeamToProject: "
                            "could not open" << mdPath << "for agent .md write";
                continue;
            }
            const QString body =
                AgentMarkdown::render(s, roleIt.value());
            mdFile.write(body.toUtf8());
            mdFile.close();
        }
    }

    return true;
}

void StorageManager::seedDefaultsIfNeeded() const
{
    // Default seeding for PARADIGM entities (see docs/PARADIGM.md):
    // - Roles: build, plan, general
    // - One "Starter Team" with build + plan Specialists
    // The goal is to make the new Role/Specialist/Team model usable on
    // first run without overwriting any existing user data.

    ensureRoot();

    const QString rolesPath = rootPath() + QStringLiteral("/roles");
    const QString teamsPath = rootPath() + QStringLiteral("/teams");

    const QDir rolesDir(rolesPath);
    const QDir teamsDir(teamsPath);

    const bool rolesEmpty = !rolesDir.exists() ||
                            rolesDir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files).isEmpty();
    const bool teamsEmpty = !teamsDir.exists() ||
                            teamsDir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files).isEmpty();

    // Only seed on first-run style scenarios (no Roles or Teams yet).
    if (!rolesEmpty && !teamsEmpty) {
        return;
    }

    qDebug() << "StorageManager:" << "seeding default PARADIGM entities under" << rootPath();

    // --- Built-in Roles (PARADIGM v0.1 §2.1, §3.3) ---
    if (rolesEmpty) {
        // build — primary, full access development agent (OpenCode default)
        Role buildRole;
        buildRole.id = QStringLiteral("build");
        buildRole.name = QStringLiteral("Build");
        buildRole.description = QStringLiteral(
            "Primary development role. Writes and edits code, runs tools, and applies "
            "changes within the OpenCode workspace.");
        buildRole.systemPrompt = QJsonValue(QStringLiteral(
            "You are the primary Build agent in an OpenCode workspace. Your job is to write and "
            "edit code, run tools when helpful, and keep the project in a buildable, tested state. "
            "Respect existing project style and conventions, explain non-obvious changes briefly, "
            "and prefer minimal, correct edits over large rewrites."));
        buildRole.mode = Role::Mode::Primary;

        if (!saveRole(buildRole)) {
            qDebug() << "StorageManager: failed to seed built-in role 'build'";
        }

        // plan — read-only analysis / planning agent (OpenCode Tab-switch sibling)
        Role planRole;
        planRole.id = QStringLiteral("plan");
        planRole.name = QStringLiteral("Plan");
        planRole.description = QStringLiteral(
            "Planning and analysis role. Reads code, explains behavior, and proposes plans "
            "without directly editing files.");
        planRole.systemPrompt = QJsonValue(QStringLiteral(
            "You are the Plan agent in an OpenCode workspace. Focus on reading and analyzing code, "
            "explaining behavior, and proposing clear step-by-step plans. Do not edit files yourself; "
            "defer actual implementation to the Build role or other execution agents."));
        planRole.mode = Role::Mode::Subagent;

        if (!saveRole(planRole)) {
            qDebug() << "StorageManager: failed to seed built-in role 'plan'";
        }

        // general — internal utility subagent for multi-step flows
        Role generalRole;
        generalRole.id = QStringLiteral("general");
        generalRole.name = QStringLiteral("General");
        generalRole.description = QStringLiteral(
            "General-purpose subagent used internally for decomposing work, background "
            "reasoning, or scratch-pad exploration.");
        generalRole.systemPrompt = QJsonValue(QStringLiteral(
            "You are a general-purpose subagent supporting the primary Build agent. Help with "
            "research, outlining, and intermediate reasoning, but leave final code edits and "
            "user-facing decisions to the primary agent unless explicitly instructed."));
        generalRole.mode = Role::Mode::Subagent;

        if (!saveRole(generalRole)) {
            qDebug() << "StorageManager: failed to seed built-in role 'general'";
        }
    }

    // --- Starter Team (PARADIGM v0.1 §2.3, §3.3) ---
    if (teamsEmpty) {
        // Specialists: one for build, one for plan, both bound to a recommended model.
        const QString modelId = QStringLiteral("anthropic/claude-sonnet-4-6");

        Specialist buildSpec;
        buildSpec.id = QStringLiteral("starter-build");
        buildSpec.roleId = QStringLiteral("build");
        buildSpec.modelId = modelId;
        buildSpec.name = QStringLiteral("Starter Build");

        if (!saveSpecialist(buildSpec)) {
            qDebug() << "StorageManager: failed to seed starter Specialist 'starter-build'";
        }

        Specialist planSpec;
        planSpec.id = QStringLiteral("starter-plan");
        planSpec.roleId = QStringLiteral("plan");
        planSpec.modelId = modelId;
        planSpec.name = QStringLiteral("Starter Plan");

        if (!saveSpecialist(planSpec)) {
            qDebug() << "StorageManager: failed to seed starter Specialist 'starter-plan'";
        }

        Team starterTeam;
        starterTeam.id = QStringLiteral("starter-team");
        starterTeam.name = QStringLiteral("Starter Team");
        starterTeam.description = QStringLiteral(
            "Default two-agent team: Build (primary) + Plan (subagent), both available for "
            "tab-style switching in OpenCode.");
        starterTeam.version = QStringLiteral("1.0.0");

        // Allow tab-switching between both core specialists; see Team.h commentary.
        starterTeam.primarySpecialistIds.append(QStringLiteral("starter-build"));
        starterTeam.primarySpecialistIds.append(QStringLiteral("starter-plan"));

        {
            Team::SpecialistBinding binding;
            binding.roleId = QStringLiteral("build");
            binding.specialistId = QStringLiteral("starter-build");
            starterTeam.specialists.append(binding);
        }
        {
            Team::SpecialistBinding binding;
            binding.roleId = QStringLiteral("plan");
            binding.specialistId = QStringLiteral("starter-plan");
            starterTeam.specialists.append(binding);
        }

        if (!saveTeam(starterTeam)) {
            qDebug() << "StorageManager: failed to seed default 'Starter Team'";
        }
    }
}
