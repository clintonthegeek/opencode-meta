#include "storage/StorageManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>

#include "apply_helpers.h"
#include "generation/AgentMarkdown.h"
#include "generation/ProviderCatalog.h"
#include "generation/TeamRenderer.h"

namespace {

// ---------------------------------------------------------------------------
// Phase D1-1 / D-9: stock opencode permission defaults
// ---------------------------------------------------------------------------
//
// kStockDefaults mirrors the seven native PermissionInfo objects built by
// stock opencode at `packages/opencode/src/agent/agent.ts:140-264`. The
// keys are the agent names; the values are the per-agent `permission`
// fragment that stock merges on top of `defaults` (agent.ts:119) and
// `user` (agent.ts:138).
//
// Why embed here instead of pulling at runtime: stock's permission shapes
// are keyed on absolute paths (`path.join(Global.Path.data, "plans",
// "*")` etc.) which depend on the host's XDG data root. For seeding we
// use stable relative patterns so the rendered file round-trips through
// `opencode debug config` without platform-dependent path literals —
// the stock runtime still matches them as glob patterns at apply time.
//
// Versioned against stock opencode commit more or less contemporaneous
// with this seed (2026-06-29). When stock changes the defaults, bump
// the SHAPES_VERSION comment below and re-run test_seed_stock_fidelity.

QJsonObject buildPlanExternalDir()
{
    // Stock plan permission broadens `<Global.Path.data>/plans/*` plus
    // ".opencode/plans/*.md". The seed uses stable relative patterns so
    // the rule applies on every host regardless of XDG data dir.
    QJsonObject obj;
    obj.insert(QStringLiteral(".opencode/plans/*"),
               QStringLiteral("allow"));
    obj.insert(QStringLiteral("<plans>/*"),
               QStringLiteral("allow"));
    return obj;
}

QJsonObject buildPlanEditRules()
{
    // Stock plan has `*: deny` plus an allowlist for the plans
    // directory. Mirrors `agent.ts:171` (the relative `path.relative(
    // ctx.worktree, path.join(Global.Path.data, "plans", "*.md"))` arm
    // is collapsed to a single `<plans>/*.md` pattern here because the
    // seed does not know the user's actual worktree).
    QJsonObject obj;
    obj.insert(QStringLiteral("*"), QStringLiteral("deny"));
    obj.insert(QStringLiteral(".opencode/plans/*.md"),
               QStringLiteral("allow"));
    obj.insert(QStringLiteral("<plans>/*.md"),
               QStringLiteral("allow"));
    return obj;
}

QJsonObject buildDefaults()
{
    // Mirror of `agent.ts:119-136` (the stock `defaults` Permission
    // applied to every native agent). Action string for the
    // action-only keys; per-pattern objects only for the rule-shaped
    // keys (`read` + `external_directory`). `doom_loop`, `question`,
    // `plan_enter`, `plan_exit`, `todowrite`, `webfetch`, `websearch`
    // are action-only and the opencode schema accepts only a bare
    // Action string for them — shipping an object would trip the v1
    // schema validator with the "doom_loop only accepts an Action"
    // error we saw before this fix.
    QJsonObject defaults;
    defaults.insert(QStringLiteral("*"), QStringLiteral("allow"));

    QJsonObject external;
    external.insert(QStringLiteral("*"), QStringLiteral("ask"));
    external.insert(QStringLiteral("<tmp>/*"), QStringLiteral("allow"));
    external.insert(QStringLiteral("<skill>/*"), QStringLiteral("allow"));
    external.insert(QStringLiteral("<reference>/*"), QStringLiteral("allow"));

    QJsonObject read;
    read.insert(QStringLiteral("*"), QStringLiteral("allow"));
    read.insert(QStringLiteral("*.env"), QStringLiteral("ask"));
    read.insert(QStringLiteral("*.env.*"), QStringLiteral("ask"));
    read.insert(QStringLiteral("*.env.example"), QStringLiteral("allow"));

    defaults.insert(QStringLiteral("question"), QStringLiteral("deny"));
    defaults.insert(QStringLiteral("plan_enter"), QStringLiteral("deny"));
    defaults.insert(QStringLiteral("plan_exit"), QStringLiteral("deny"));
    defaults.insert(QStringLiteral("read"), read);
    defaults.insert(QStringLiteral("external_directory"), external);
    defaults.insert(QStringLiteral("doom_loop"), QStringLiteral("ask"));
    return defaults;
}

QJsonObject mergeDefaultsOverrides(const QJsonObject &overrides)
{
    // Late merges win per the runtime `Permission.merge` semantics (cf.
    // report §6 / `migrate.ts:35`). The seed ships a literal snapshot
    // — at apply time the runtime merges with the host's defaults +
    // user overrides, so the seed does NOT need to embed the full
    // effective ruleset.
    QJsonObject merged = buildDefaults();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        merged.insert(it.key(), it.value());
    }
    return merged;
}

// stockDefaults() returns the seven-agent permission map. Lazily built
// on first call so static-init order does not matter.
//
// Shape contract (locked by tests/test_seed_stock_fidelity.cpp):
//   build       -> defaults + { question: "allow", plan_enter: "allow" }
//   plan        -> defaults + { question: "allow",
//                                plan_exit: "allow",
//                                task: { "general": "deny" },
//                                external_directory: { wide plans allow },
//                                edit: { { *: "deny", plans allow } } }
//   general     -> defaults + { todowrite: "deny" }
//   explore     -> defaults + { *: "deny",
//                                grep: "allow", glob: "allow",
//                                list: "allow",   bash: "allow",
//                                webfetch: "allow", websearch: "allow",
//                                read: "allow",
//                                external_directory: readonly pattern form }
//   compaction -> defaults + { *: "deny" }
//   title      -> defaults + { *: "deny" }
//   summary    -> defaults + { *: "deny" }
QJsonObject mergesReadonlyExternalDir();
QHash<QString, QJsonObject> stockDefaults()
{
    static const QHash<QString, QJsonObject> defaults = []() {
        QHash<QString, QJsonObject> map;

        // build (agent.ts:141-155)
        {
            QJsonObject overrides;
            overrides.insert(QStringLiteral("question"),
                             QStringLiteral("allow"));
            overrides.insert(QStringLiteral("plan_enter"),
                             QStringLiteral("allow"));
            map.insert(QStringLiteral("build"),
                       mergeDefaultsOverrides(overrides));
        }

        // plan (agent.ts:156-181)
        {
            QJsonObject overrides;
            overrides.insert(QStringLiteral("question"),
                             QStringLiteral("allow"));
            overrides.insert(QStringLiteral("plan_exit"),
                             QStringLiteral("allow"));

            QJsonObject task;
            task.insert(QStringLiteral("general"), QStringLiteral("deny"));
            overrides.insert(QStringLiteral("task"), task);

            overrides.insert(QStringLiteral("external_directory"),
                             buildPlanExternalDir());

            overrides.insert(QStringLiteral("edit"),
                             buildPlanEditRules());

            map.insert(QStringLiteral("plan"),
                       mergeDefaultsOverrides(overrides));
        }

        // general (agent.ts:182-195)
        {
            QJsonObject overrides;
            overrides.insert(QStringLiteral("todowrite"),
                             QStringLiteral("deny"));
            map.insert(QStringLiteral("general"),
                       mergeDefaultsOverrides(overrides));
        }

        // explore (agent.ts:196-218)
        {
            QJsonObject overrides;
            overrides.insert(QStringLiteral("*"), QStringLiteral("deny"));
            overrides.insert(QStringLiteral("grep"), QStringLiteral("allow"));
            overrides.insert(QStringLiteral("glob"), QStringLiteral("allow"));
            overrides.insert(QStringLiteral("list"), QStringLiteral("allow"));
            overrides.insert(QStringLiteral("bash"), QStringLiteral("allow"));
            overrides.insert(QStringLiteral("webfetch"),
                             QStringLiteral("allow"));
            overrides.insert(QStringLiteral("websearch"),
                             QStringLiteral("allow"));
            overrides.insert(QStringLiteral("read"), QStringLiteral("allow"));
            overrides.insert(QStringLiteral("external_directory"),
                             mergesReadonlyExternalDir());
            map.insert(QStringLiteral("explore"),
                       mergeDefaultsOverrides(overrides));
        }

        // compaction (agent.ts:219-233)
        {
            QJsonObject overrides;
            overrides.insert(QStringLiteral("*"), QStringLiteral("deny"));
            map.insert(QStringLiteral("compaction"),
                       mergeDefaultsOverrides(overrides));
        }

        // title (agent.ts:234-249)
        {
            QJsonObject overrides;
            overrides.insert(QStringLiteral("*"), QStringLiteral("deny"));
            map.insert(QStringLiteral("title"),
                       mergeDefaultsOverrides(overrides));
        }

        // summary (agent.ts:250-264)
        {
            QJsonObject overrides;
            overrides.insert(QStringLiteral("*"), QStringLiteral("deny"));
            map.insert(QStringLiteral("summary"),
                       mergeDefaultsOverrides(overrides));
        }

        return map;
    }();
    return defaults;
}

// explore's `external_directory` is the
// `readonlyExternalDirectory` value rebuilt at agent.ts:114. We use
// stable relative patterns here — see comment on buildPlanExternalDir.
QJsonObject mergesReadonlyExternalDir()
{
    QJsonObject ext;
    ext.insert(QStringLiteral("*"), QStringLiteral("ask"));
    ext.insert(QStringLiteral("<tmp>/*"), QStringLiteral("allow"));
    ext.insert(QStringLiteral("<skill>/*"), QStringLiteral("allow"));
    ext.insert(QStringLiteral("<reference>/*"), QStringLiteral("allow"));
    return ext;
}

// Prompt text for the three hidden primaries. Sourced from
// packages/opencode/src/agent/prompt/{compaction,title,summary}.txt
// (verbatim, 2026-06-29). The explore prompt is exposed in `Role.systemPrompt`
// below because it surfaces to the user — the hidden ones stay in the
// anon-ns so they remain editor-internal.
const QString kCompactionPrompt =
    QStringLiteral(
        "You are an anchored context summarization assistant for coding "
        "sessions.\n"
        "\n"
        "Summarize only the conversation history you are given. The newest "
        "turns may be kept verbatim outside your summary, so focus on the "
        "older context that still matters for continuing the work.\n"
        "\n"
        "If the prompt includes a <previous-summary> block, treat it as the "
        "current anchored summary. Update it with the new history by "
        "preserving still-true details, removing stale details, and merging "
        "in new facts.\n"
        "\n"
        "Always follow the exact output structure requested by the user "
        "prompt. Keep every section, preserve exact file paths and "
        "identifiers when known, and prefer terse bullets over paragraphs.\n"
        "\n"
        "Do not answer the conversation itself. Do not mention that you are "
        "summarizing, compacting, or merging context. Respond in the same "
        "language as the conversation.");

const QString kTitlePrompt =
    QStringLiteral(
        "You are a title generator. You output ONLY a thread title. Nothing "
        "else.\n"
        "\n"
        "<task>\n"
        "Generate a brief title that would help the user find this "
        "conversation later.\n"
        "\n"
        "Follow all rules in <rules>\n"
        "Use the <examples> so you know what a good title looks like.\n"
        "Your output must be:\n"
        "- A single line\n"
        "- <=50 characters\n"
        "- No explanations\n"
        "</task>\n"
        "\n"
        "<rules>\n"
        "- you MUST use the same language as the user message you are "
        "summarizing\n"
        "- Title must be grammatically correct and read naturally - no "
        "word salad\n"
        "- Never include tool names in the title (e.g. \"read tool\", "
        "\"bash tool\", \"edit tool\")\n"
        "- Focus on the main topic or question the user needs to retrieve\n"
        "- Vary your phrasing - avoid repetitive patterns like always "
        "starting with \"Analyzing\"\n"
        "- When a file is mentioned, focus on WHAT the user wants to do "
        "WITH the file, not just that they shared it\n"
        "- Keep exact: technical terms, numbers, filenames, HTTP codes\n"
        "- Remove: the, this, my, a, an\n"
        "- Never assume tech stack\n"
        "- Never use tools\n"
        "- NEVER respond to questions, just generate a title for the "
        "conversation\n"
        "- The title should NEVER include \"summarizing\" or "
        "\"generating\" when generating a title\n"
        "- DO NOT SAY YOU CANNOT GENERATE A TITLE OR COMPLAIN ABOUT THE "
        "INPUT\n"
        "- Always output something meaningful, even if the input is "
        "minimal.\n"
        "- If the user message is short or conversational (e.g. \"hello\", "
        "\"lol\", \"what's up\", \"hey\"):\n"
        "  -> create a title that reflects the user's tone or intent "
        "(such as Greeting, Quick check-in, Light chat, Intro message, "
        "etc.)\n"
        "</rules>\n"
        "\n"
        "<examples>\n"
        "\"debug 500 errors in production\" -> Debugging production 500 "
        "errors\n"
        "\"refactor user service\" -> Refactoring user service\n"
        "\"why is app.js failing\" -> app.js failure investigation\n"
        "\"implement rate limiting\" -> Rate limiting implementation\n"
        "\"how do I connect postgres to my API\" -> Postgres API "
        "connection\n"
        "\"best practices for React hooks\" -> React hooks best practices\n"
        "\"@src/auth.ts can you add refresh token support\" -> Auth "
        "refresh token support\n"
        "\"@utils/parser.ts this is broken\" -> Parser bug fix\n"
        "\"look at @config.json\" -> Config review\n"
        "\"@App.tsx add dark mode toggle\" -> Dark mode toggle in App\n"
        "</examples>");

const QString kSummaryPrompt =
    QStringLiteral(
        "Summarize what was done in this conversation. Write like a pull "
        "request description.\n"
        "\n"
        "Rules:\n"
        "- 2-3 sentences max\n"
        "- Describe the changes made, not the process\n"
        "- Do not mention running tests, builds, or other validation "
        "steps\n"
        "- Do not explain what the user asked for\n"
        "- Write in first person (I added..., I fixed...)\n"
        "- Never ask questions or add new questions\n"
        "- If the conversation ends with an unanswered question to the "
        "user, preserve that exact question\n"
        "- If the conversation ends with an imperative statement or "
        "request to the user (e.g. \"Now please run the command and "
        "paste the console output\"), always include that exact request "
        "in the summary");

const QString kExplorePrompt =
    QStringLiteral(
        "You are a file search specialist. You excel at thoroughly "
        "navigating and exploring codebases.\n"
        "\n"
        "Your strengths:\n"
        "- Rapidly finding files using glob patterns\n"
        "- Searching code and text with powerful regex patterns\n"
        "- Reading and analyzing file contents\n"
        "\n"
        "Guidelines:\n"
        "- Use Glob for broad file pattern matching\n"
        "- Use Grep for searching file contents with regex\n"
        "- Use Read when you know the specific file path you need to read\n"
        "- Use Bash for file operations like copying, moving, or listing "
        "directory contents\n"
        "- Adapt your search approach based on the thoroughness level "
        "specified by the caller\n"
        "- Return file paths as absolute paths in your final response\n"
        "- For clear communication, avoid using emojis\n"
        "- Do not create any files, or run bash commands that modify the "
        "user's system state in any way\n"
        "\n"
        "Complete the user's search request efficiently and report your "
        "findings clearly.");

QString staticPickModel()
{
    // D1-5 / D-9: pin the Starter Team's Specialist model. Single
    // constant because every Specialist on the team must bind to the
    // same provider/model shape so the opencode metadata gets a
    // coherent default_agent semantics. anthropic/claude-sonnet-4-6
    // is stock opencode's recommended default in 1.17.x; the live
    // catalog will reject it if not present, which is the desired
    // failure mode (D-2 / D7 enforcement).
    return QStringLiteral("anthropic/claude-sonnet-4-6");
}

} // namespace

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

StorageManager::SeedVersion StorageManager::readSeedVersion()
{
    // D1-2 / §3: lookup the persisted seed generation under the
    // "storage/seed_version" QSettings key. Without an app instance we
    // fall back to the v0 fiction so a unit test that lacks QCoreApplication
    // can still call `seedDefaultsIfNeeded`. Default = v1_stockFidelity
    // so a fresh install lands on the new seed automatically (no
    // migration warning, no user action required).
    if (QCoreApplication::instance() == nullptr) {
        return SeedVersion::v1_stockFidelity;
    }
    const QSettings settings;
    const int raw = settings
                        .value(QStringLiteral("storage/seed_version"),
                               static_cast<int>(SeedVersion::v1_stockFidelity))
                        .toInt();
    if (raw == static_cast<int>(SeedVersion::v0_legacyFiction)) {
        return SeedVersion::v0_legacyFiction;
    }
    return SeedVersion::v1_stockFidelity;
}

void StorageManager::writeSeedVersion(SeedVersion v)
{
    if (QCoreApplication::instance() == nullptr) {
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("storage/seed_version"),
                      static_cast<int>(v));
    settings.sync();
}

void StorageManager::seedDefaultsIfNeeded() const
{
    // Default seeding for PARADIGM entities (see docs/PARADIGM.md):
    // - Roles: build, plan, general, explore [+ 3 hidden: compaction,
    //                                          title, summary]
    // - One "Starter Team" with four Specialists bound to
    //   anthropic/claude-sonnet-4-6 and a metadata.default_agent
    //   override = "starter-build".
    //
    // Phase D1-3 / D-9: per-agent permission rulesets come from
    // `kStockDefaults` (anon-ns at top of file), sourcing stock
    // opencode's `agent.ts:140-264` verbatim. Each seed Role carries
    // `metadata.native = true` so the editor surfaces a Read-Only
    // badge; the three hidden primaries carry `metadata.hidden = true`
    // in addition so the D3-3 filter chip can hide them by default.
    //
    // **Existing user data is sacred.** The early-out at the line
    // marked `// EARLY-OUT` (below) bails whenever either roles/ or
    // teams/ already carries a JSON file on disk — D4-2 locks the
    // contract that we never bump seed_version in that case.

    ensureRoot();

    const QString rolesPath = rootPath() + QStringLiteral("/roles");
    const QString teamsPath = rootPath() + QStringLiteral("/teams");

    const QDir rolesDir(rolesPath);
    const QDir teamsDir(teamsPath);

    const bool rolesEmpty = !rolesDir.exists() ||
                            rolesDir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files).isEmpty();
    const bool teamsEmpty = !teamsDir.exists() ||
                            teamsDir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files).isEmpty();

    // Phase D3-7 / D-12: optional reset path (Settings checkbox
    // `settings/reset_seed_on_next_launch`). When the flag is on, we
    // wipe the existing roles/ + teams/ JSON files BEFORE the early-
    // out so the user gets a fresh stock-aligned seed on the very
    // next launch. The flag is single-shot: once consumed we clear it
    // so subsequent launches don't re-wipe the (now-stock) storage.
    if (QCoreApplication::instance() != nullptr
        && QSettings().value(QStringLiteral("settings/reset_seed_on_next_launch"),
                             QVariant(false)).toBool()) {
        qInfo() << "StorageManager:" << "settings/reset_seed_on_next_launch set;"
                   " wiping roles + teams for a fresh stock-aligned seed under"
                << rootPath();
        if (!rolesEmpty) {
            const QStringList files = rolesDir.entryList(
                QStringList() << QStringLiteral("*.json"), QDir::Files);
            for (const QString &f : files) {
                QFile::remove(rolesDir.filePath(f));
            }
        }
        if (!teamsEmpty) {
            const QStringList files = teamsDir.entryList(
                QStringList() << QStringLiteral("*.json"), QDir::Files);
            for (const QString &f : files) {
                QFile::remove(teamsDir.filePath(f));
            }
        }
        QSettings settings;
        settings.setValue(QStringLiteral("settings/reset_seed_on_next_launch"),
                          QVariant(false));
        settings.sync();
    }

    const bool resetJustRan =
        QCoreApplication::instance() != nullptr
        && false; // already cleared above; this re-read would be the cleared value

    // Recompute after the wipe — the directories may now be empty.
    const bool rolesEmptyNow = !QDir(rolesPath).exists()
        || QDir(rolesPath).entryList(QStringList() << QStringLiteral("*.json"),
                                      QDir::Files).isEmpty();
    const bool teamsEmptyNow = !QDir(teamsPath).exists()
        || QDir(teamsPath).entryList(QStringList() << QStringLiteral("*.json"),
                                      QDir::Files).isEmpty();

    // EARLY-OUT (D4-2 / §7): existing user data wins. The seed is
    // strictly an "empty storage on first run" path. We deliberately
    // do not bump seed_version here — a user who already curated
    // their own Roles / Teams keeps exactly what they had before we
    // shipped the stock seed.
    if (!rolesEmptyNow && !teamsEmptyNow && !resetJustRan) {
        return;
    }

    // D3-7 / D-7: opt-out path (Settings checkbox
    // `settings/seed_stock_defaults`). When off we run the v0 fiction
    // seed (the pre-D1 approximation) so any user who wants the old
    // behaviour can keep it. This is the only escape hatch for the
    // stock-fidelity seed.
    const bool seedStockDefaults =
        QCoreApplication::instance() == nullptr
        || QSettings().value(QStringLiteral("settings/seed_stock_defaults"),
                             QVariant(true)).toBool();

    qDebug() << "StorageManager:"
             << (seedStockDefaults
                     ? QStringLiteral("seeding stock-aligned (D1) PARADIGM entities")
                     : QStringLiteral("seeding legacy fiction PARADIGM entities"))
             << "under" << rootPath();

    if (seedStockDefaults) {
        // ---------- D1-3 / D-9: stock-seed ----------
        const QHash<QString, QJsonObject> defaults = stockDefaults();
        if (rolesEmptyNow) {
            auto writeNativeRole = [&](const QString &id,
                                       const QString &name,
                                       const QString &description,
                                       const QString &prompt,
                                       Role::Mode mode,
                                       bool hidden) {
                Role role;
                role.id = id;
                role.name = name;
                role.description = description;
                if (!prompt.isEmpty()) {
                    role.systemPrompt = QJsonValue(prompt);
                }
                role.mode = mode;
                role.permissions = defaults.value(id);
                QJsonObject meta;
                meta.insert(QStringLiteral("native"), true);
                if (hidden) {
                    meta.insert(QStringLiteral("hidden"), true);
                }
                role.metadata = meta;
                if (!saveRole(role)) {
                    qDebug() << "StorageManager: failed to seed native role" << id;
                }
            };

            // Build (agent.ts:141-155) — primary, no prompt (uses
            // stock's default empty body).
            writeNativeRole(QStringLiteral("build"),
                            QStringLiteral("Build"),
                            QStringLiteral(
                                "The default agent. Executes tools based on "
                                "configured permissions."),
                            QString(), // stock ships no body
                            Role::Mode::Primary,
                            /*hidden=*/false);

            // Plan (agent.ts:156-181) — primary, no prompt.
            // Phase D1-3 critical divergence: stock's `plan.mode =
            // primary` (NOT subagent as the v0 fiction shipped). D-9
            // explicit acceptance locks this in.
            writeNativeRole(QStringLiteral("plan"),
                            QStringLiteral("Plan"),
                            QStringLiteral(
                                "Plan mode. Disallows all edit tools."),
                            QString(),
                            Role::Mode::Primary,
                            /*hidden=*/false);

            // General (agent.ts:182-195) — subagent, no prompt.
            writeNativeRole(QStringLiteral("general"),
                            QStringLiteral("General"),
                            QStringLiteral(
                                "General-purpose agent for researching complex "
                                "questions and executing multi-step tasks. Use "
                                "this agent to execute multiple units of work "
                                "in parallel."),
                            QString(),
                            Role::Mode::Subagent,
                            /*hidden=*/false);

            // Explore (agent.ts:196-218) — subagent, kExplorePrompt.
            writeNativeRole(QStringLiteral("explore"),
                            QStringLiteral("Explore"),
                            QStringLiteral(
                                "Fast agent specialized for exploring "
                                "codebases. Use this when you need to quickly "
                                "find files by patterns (eg. \"src/components/"
                                "**/*.tsx\"), search code for keywords (eg. "
                                "\"API endpoints\"), or answer questions "
                                "about the codebase (eg. \"how do API "
                                "endpoints work?\"). When calling this agent, "
                                "specify the desired thoroughness level: "
                                "\"quick\" for basic searches, \"medium\" for "
                                "moderate exploration, or \"very thorough\" "
                                "for comprehensive analysis across multiple "
                                "locations and naming conventions."),
                            kExplorePrompt,
                            Role::Mode::Subagent,
                            /*hidden=*/false);

            // Hidden primaries (agent.ts:219-264): compaction,
            // title, summary. All `mode: primary` + `*:
            // "deny"` — chosen by stock to be unreachable from the
            // user prompt; the metadata.hidden flag tells the editor
            // (D3-3) to hide them in the Roles list by default.
            writeNativeRole(QStringLiteral("compaction"),
                            QStringLiteral("Compaction"),
                            QStringLiteral(
                                "Anchored context summarisation agent used "
                                "by opencode to compact older conversation "
                                "history."),
                            kCompactionPrompt,
                            Role::Mode::Primary,
                            /*hidden=*/true);

            writeNativeRole(QStringLiteral("title"),
                            QStringLiteral("Title"),
                            QStringLiteral(
                                "Single-line thread title generator invoked "
                                "by opencode on conversation creation."),
                            kTitlePrompt,
                            Role::Mode::Primary,
                            /*hidden=*/true);

            writeNativeRole(QStringLiteral("summary"),
                            QStringLiteral("Summary"),
                            QStringLiteral(
                                "PR-style conversation summary agent invoked "
                                "by opencode at conversation close."),
                            kSummaryPrompt,
                            Role::Mode::Primary,
                            /*hidden=*/true);
        }

        // ---------- D1-5: Starter Team v1 ----------
        seedStarterTeamStock(/*teamsEmpty=*/teamsEmptyNow);

        writeSeedVersion(SeedVersion::v1_stockFidelity);
    } else {
        // D-7 escape hatch: vintage fiction seed (kept for users who
        // explicitly opted out via Settings → Seeding). Identical to
        // the pre-D1 implementation so a returning opt-out sees the
        // exact same artefacts they had before.
        const SeedVersion v = rolesEmptyNow ? SeedVersion::v0_legacyFiction
                                            : readSeedVersion();
        if (rolesEmptyNow) {
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
            // v0 fiction: plan was Subagent. Keep that exact behaviour
            // so the opt-out path round-trips a legacy user verbatim.
            planRole.mode = Role::Mode::Subagent;
            if (!saveRole(planRole)) {
                qDebug() << "StorageManager: failed to seed built-in role 'plan'";
            }

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
        if (teamsEmptyNow) {
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
        writeSeedVersion(v);
    }
}

void StorageManager::seedStarterTeamStock(bool teamsEmpty) const
{
    // D1-5 / D-9: stock-aligned Starter Team. Four Specialists
    // (one per native agent) bound to the same model, metadata
    // carrying `default_agent = "starter-build"` so the renderer
    // lifts it to top-level `default_agent` per D-11 (and emits the
    // v2 `defaultAgent` mirror per D-1 / §5.9).
    //
    // Behavioural shape:
    //   * test_starter_team_apply applies the freshly-seeded team to a
    //     fresh project + asserts `opencode debug config` exits 0.
    //   * The Team has `parentTeamId` unset + `metadata.native = false`
    //     so a user who clones it (D3-3) gets a non-native copy.
    if (!teamsEmpty) {
        return;
    }

    const QString modelId = staticPickModel();

    struct SpecSeed {
        QString id;
        QString roleId;
        QString name;
    };
    const QList<SpecSeed> specs = {
        {QStringLiteral("starter-build"),    QStringLiteral("build"),    QStringLiteral("Starter Build")},
        {QStringLiteral("starter-plan"),     QStringLiteral("plan"),     QStringLiteral("Starter Plan")},
        {QStringLiteral("starter-general"),  QStringLiteral("general"),  QStringLiteral("Starter General")},
        {QStringLiteral("starter-explore"),  QStringLiteral("explore"),  QStringLiteral("Starter Explore")},
    };
    for (const SpecSeed &seed : specs) {
        Specialist s;
        s.id = seed.id;
        s.roleId = seed.roleId;
        s.modelId = modelId;
        s.name = seed.name;
        if (!saveSpecialist(s)) {
            qDebug() << "StorageManager: failed to seed starter Specialist"
                     << seed.id;
        }
    }

    Team starter;
    starter.id = QStringLiteral("starter-team");
    starter.name = QStringLiteral("Starter Team");
    starter.description = QStringLiteral(
        "Default team mirroring stock opencode's four primary agents "
        "(Build, Plan, General, Explore). Tab-switch between Build and "
        "Plan; General and Explore are subagents surfaced via @.");
    starter.version = QStringLiteral("1.0.0");

    // Both primaries (`build` + `plan`) so a user gets tab-switching
    // out of the box — matches the Starter Team v0 behaviour.
    starter.primarySpecialistIds.append(QStringLiteral("starter-build"));
    starter.primarySpecialistIds.append(QStringLiteral("starter-plan"));

    for (const SpecSeed &seed : specs) {
        Team::SpecialistBinding binding;
        binding.roleId = seed.roleId;
        binding.specialistId = seed.id;
        starter.specialists.append(binding);
    }

    // D-11: surface `metadata.default_agent` so the renderer lifts
    // to top-level `default_agent` (and v2 `defaultAgent`). The
    // placeholder text matches the literal "starter-build" id, which
    // the Team::specialists map above resolves to the `build` Role
    // (and therefore the spec bound to `anthropic/claude-sonnet-4-6`).
    QJsonObject meta;
    meta.insert(QStringLiteral("default_agent"),
                 QStringLiteral("starter-build"));
    starter.metadata = meta;

    if (!saveTeam(starter)) {
        qDebug() << "StorageManager: failed to seed default 'Starter Team'";
    }
}
