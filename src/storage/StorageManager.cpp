#include "storage/StorageManager.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QDateTime>

#include "apply_helpers.h"
#include "generation/TeamRenderer.h"

StorageManager::StorageManager(const QString &rootOverride)
    : m_rootOverride(rootOverride)
{
}

QString StorageManager::rootPath() const
{
    // All data is stored under ~/.opencode-meta/ by default. Tests may inject
    // a different root via m_rootOverride to avoid touching real user data.
    if (!m_rootOverride.isEmpty()) {
        return m_rootOverride;
    }

    return QDir::homePath() + QStringLiteral("/.opencode-meta");
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
    const ApplyResult applyResult = applyConfigWithBackup(configPath, config);
    if (!applyResult.success) {
        qDebug() << "StorageManager::applyTeamToProject: applyConfigWithBackup failed" << applyResult.errorString;
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
