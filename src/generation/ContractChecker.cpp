#include "generation/ContractChecker.h"

#include <QJsonArray>

#include <algorithm>
#include <utility>

#include "generation/ProviderCatalog.h"

namespace {

// ---------- Static allow-lists ----------

// Top-level keys from report §4 (ConfigV1.Info lines 33–166). Order is
// roughly the order opencode itself lists them (low line numbers first),
// but the validator treats the set as unordered.
//
// The trailing six keys are the v2 sidecar mirrors from report §5 plus
// Phase G5: every emitted `opencode.json` now ships both the v1
// snake_case keys AND the v2 camelCase companions so the migration
// bridge at packages/core/src/v1/config/migrate.ts:35 stays happy.
const std::vector<QString> &topLevelKeys()
{
    static const std::vector<QString> keys = {
        QStringLiteral("$schema"),
        QStringLiteral("shell"),
        QStringLiteral("logLevel"),
        QStringLiteral("server"),
        QStringLiteral("command"),
        QStringLiteral("skills"),
        QStringLiteral("references"),
        QStringLiteral("reference"),
        QStringLiteral("watcher"),
        QStringLiteral("snapshot"),
        QStringLiteral("plugin"),
        QStringLiteral("share"),
        QStringLiteral("autoshare"),
        QStringLiteral("autoupdate"),
        QStringLiteral("disabled_providers"),
        QStringLiteral("enabled_providers"),
        QStringLiteral("model"),
        QStringLiteral("small_model"),
        QStringLiteral("default_agent"),
        QStringLiteral("username"),
        QStringLiteral("mode"),
        QStringLiteral("agent"),
        QStringLiteral("provider"),
        QStringLiteral("mcp"),
        QStringLiteral("formatter"),
        QStringLiteral("lsp"),
        QStringLiteral("instructions"),
        QStringLiteral("layout"),
        QStringLiteral("permission"),
        QStringLiteral("tools"),
        QStringLiteral("attachment"),
        QStringLiteral("enterprise"),
        QStringLiteral("tool_output"),
        QStringLiteral("compaction"),
        QStringLiteral("experimental"),
        // Phase G5 — v2 sidecar mirrors (.agents / .permissions / ...
        // per report §5 and PARADIGM §5.1). The validator treats them as
        // additional valid top-level keys; per-agent contents are walked
        // via the v1 `agent` map (which now also contains the v2 keys
        // `system`/`disabled`/`request`/`permissions` inside each
        // entry — see `agentFields()` below).
        QStringLiteral("agents"),
        QStringLiteral("permissions"),
        QStringLiteral("providers"),
        QStringLiteral("snapshots"),
        QStringLiteral("smallModel"),
        QStringLiteral("attachments"),
        // Phase D2-2 / D-11: v2 mirror of `default_agent`. Same
        // scalar shape as the v1 key (string) so per D-1 "only emit
        // v2 sidecars when v2 form is structurally correct" this
        // mirror is safe.
        QStringLiteral("defaultAgent"),
    };
    return keys;
}

// Agent fields from report §7.1 (ConfigAgentV1.Info KNOWN_KEYS, agent.ts:43).
// Includes the deprecated `tools` and `maxSteps` because KNOWN_KEYS still
// routes them through normalize() rather than rejecting them.
//
// The trailing four keys (`system`, `disabled`, `request`, `permissions`)
// are the v2 sidecar mirrors from report §7.2 + migrateAgent at
// packages/core/src/v1/config/migrate.ts:106-125. `TeamRenderer` now
// emits them alongside the v1 entries when the v1 inputs exist, so the
// allow-list must accept them or the dual-emitted config trips the
// "unknown agent field" gate at load time (Phase G5 spec).
const std::vector<QString> &agentFields()
{
    static const std::vector<QString> fields = {
        QStringLiteral("model"),
        QStringLiteral("variant"),
        QStringLiteral("temperature"),
        QStringLiteral("top_p"),
        QStringLiteral("prompt"),
        QStringLiteral("tools"),
        QStringLiteral("disable"),
        QStringLiteral("description"),
        QStringLiteral("mode"),
        QStringLiteral("hidden"),
        QStringLiteral("color"),
        QStringLiteral("options"),
        QStringLiteral("steps"),
        QStringLiteral("maxSteps"),
        QStringLiteral("permission"),
        // Phase G5 — v2 sidecar fields inside each agent entry. Mirrors
        // report §7.2 (ConfigAgent.Info) and migrateAgent at
        // packages/core/src/v1/config/migrate.ts:106-125:
        //   system     -> v1 prompt
        //   disabled   -> v1 disable
        //   request    -> v1 request (provider body/headers)
        //   permissions -> flattened v1 permission (Ruleset form)
        QStringLiteral("system"),
        QStringLiteral("disabled"),
        QStringLiteral("request"),
        QStringLiteral("permissions"),
    };
    return fields;
}

// §6.1 Permission keys that allow either an Action string or a per-pattern
// Record<pattern, Action>. 10 keys per the report.
const std::vector<QString> &permissionRuleKeys()
{
    static const std::vector<QString> keys = {
        QStringLiteral("read"),
        QStringLiteral("edit"),
        QStringLiteral("glob"),
        QStringLiteral("grep"),
        QStringLiteral("list"),
        QStringLiteral("bash"),
        QStringLiteral("task"),
        QStringLiteral("external_directory"),
        QStringLiteral("lsp"),
        QStringLiteral("skill"),
    };
    return keys;
}

// §6.1 Permission keys that only accept an Action value (no pattern form).
const std::vector<QString> &permissionActionOnlyKeys()
{
    static const std::vector<QString> keys = {
        QStringLiteral("todowrite"),
        QStringLiteral("question"),
        QStringLiteral("webfetch"),
        QStringLiteral("websearch"),
        QStringLiteral("doom_loop"),
    };
    return keys;
}

// §6.1 Action vocabulary (Schema.Literals at permission.ts:5).
const std::vector<QString> &actions()
{
    static const std::vector<QString> a = {
        QStringLiteral("ask"),
        QStringLiteral("allow"),
        QStringLiteral("deny"),
    };
    return a;
}

// §7.1 mode vocabulary (ConfigAgentV1.Info.mode union).
const std::vector<QString> &agentModes()
{
    static const std::vector<QString> m = {
        QStringLiteral("primary"),
        QStringLiteral("subagent"),
        QStringLiteral("all"),
    };
    return m;
}

bool contains(const std::vector<QString> &haystack, const QString &needle)
{
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

} // namespace

// ---------- ContractCheckResult ----------

ContractCheckResult ContractCheckResult::pass()
{
    return ContractCheckResult{};
}

ContractCheckResult ContractCheckResult::fail(QStringList errs)
{
    ContractCheckResult r;
    r.ok = false;
    r.errors = std::move(errs);
    return r;
}

ContractCheckResult ContractCheckResult::failOne(const QString &err)
{
    ContractCheckResult r;
    r.ok = false;
    r.errors.append(err);
    return r;
}

void ContractCheckResult::merge(const ContractCheckResult &other)
{
    if (!other.ok) {
        ok = false;
    }
    for (const QString &e : other.errors) {
        errors.append(e);
    }
    for (const QString &w : other.warnings) {
        warnings.append(w);
    }
}

// ---------- ContractChecker ----------

ContractChecker::ContractChecker() = default;
ContractChecker::~ContractChecker() = default;

const std::vector<QString> &ContractChecker::allowedTopLevelKeys()
{
    return topLevelKeys();
}

const std::vector<QString> &ContractChecker::allowedAgentFields()
{
    return agentFields();
}

const std::vector<QString> &ContractChecker::allowedPermissionRuleKeys()
{
    return permissionRuleKeys();
}

const std::vector<QString> &ContractChecker::allowedPermissionActionOnlyKeys()
{
    return permissionActionOnlyKeys();
}

const std::vector<QString> &ContractChecker::allowedActions()
{
    return actions();
}

const std::vector<QString> &ContractChecker::allowedAgentModes()
{
    return agentModes();
}

bool ContractChecker::parseModel(const QString &modelId,
                                 QString *providerId,
                                 QString *modelIdOut,
                                 QString *errorOut)
{
    if (modelId.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("model string is empty");
        }
        return false;
    }
    const int slashIndex = modelId.indexOf(QLatin1Char('/'));
    if (slashIndex <= 0 || slashIndex == modelId.size() - 1) {
        // First '/' must split at position 1..len-1 and both sides
        // must be non-empty (report §8.3 + Brand.pipe checks for
        // non-empty strings).
        if (errorOut) {
            *errorOut = QStringLiteral(
                "model string %1 does not parse as provider/model (missing or leading/trailing '/')")
                .arg(modelId);
        }
        return false;
    }
    if (providerId) {
        *providerId = modelId.left(slashIndex);
    }
    if (modelIdOut) {
        *modelIdOut = modelId.mid(slashIndex + 1);
    }
    return true;
}

ContractCheckResult ContractChecker::checkSchemaToken(const QJsonObject &candidate) const
{
    static const QString kSchema = QStringLiteral("$schema");
    static const QString kExpected = QStringLiteral("https://opencode.ai/config.json");

    if (!candidate.contains(kSchema)) {
        return ContractCheckResult::failOne(QStringLiteral(
            "missing \"$schema\" at top level (must be the literal "
            "\"https://opencode.ai/config.json\" per report §3)"));
    }
    const QString value = candidate.value(kSchema).toString();
    if (value != kExpected) {
        return ContractCheckResult::failOne(QStringLiteral(
            "\"$schema\" must be the literal %1 (got %2)")
            .arg(kExpected, value));
    }
    return ContractCheckResult::pass();
}

ContractCheckResult ContractChecker::checkTopLevelKeys(const QJsonObject &candidate) const
{
    QStringList errs;
    const auto &allowed = topLevelKeys();
    for (const QString &key : candidate.keys()) {
        if (!contains(allowed, key)) {
            errs.append(QStringLiteral(
                "unknown top-level key %1 (not in report §4 ConfigV1.Info)")
                .arg(key));
        }
    }
    if (!errs.isEmpty()) {
        return ContractCheckResult::fail(errs);
    }
    return ContractCheckResult::pass();
}

ContractCheckResult ContractChecker::checkPermissionBlock(
    const QString &blockName,
    const QJsonObject &permission,
    const QString &contextPath) const
{
    QStringList errs;

    const auto &ruleKeys = permissionRuleKeys();
    const auto &actionOnlyKeys = permissionActionOnlyKeys();
    const auto &allowedActions = actions();

    for (const QString &key : permission.keys()) {
        const QString thisPath = contextPath + QStringLiteral("/") + key;
        const QJsonValue value = permission.value(key);

        const bool ruleKey = contains(ruleKeys, key);
        const bool actionOnlyKey = contains(actionOnlyKeys, key);

        if (!ruleKey && !actionOnlyKey) {
            errs.append(QStringLiteral(
                "unknown permission key %1 in %2 (not in report §6.1's 15 legal keys)")
                .arg(key, blockName));
            continue;
        }

        if (actionOnlyKey) {
            // Must be a single Action string.
            if (!value.isString()) {
                errs.append(QStringLiteral(
                    "%1: %2 only accepts an Action (ask|allow|deny), not %3")
                    .arg(thisPath, key, value.type() == QJsonValue::Null
                              ? QStringLiteral("null")
                              : QStringLiteral("object/array")));
                continue;
            }
            if (!contains(allowedActions, value.toString())) {
                errs.append(QStringLiteral(
                    "%1: %2 must be one of ask|allow|deny (got %3)")
                    .arg(thisPath, key, value.toString()));
            }
            continue;
        }

        // ruleKey: accept Action string or per-pattern object.
        if (value.isString()) {
            if (!contains(allowedActions, value.toString())) {
                errs.append(QStringLiteral(
                    "%1: %2 must be one of ask|allow|deny (got %3)")
                    .arg(thisPath, key, value.toString()));
            }
        } else if (value.isObject()) {
            const QJsonObject pat = value.toObject();
            if (pat.isEmpty()) {
                errs.append(QStringLiteral(
                    "%1: %2 pattern object must have at least one entry")
                    .arg(thisPath, key));
                continue;
            }
            for (const QString &pattern : pat.keys()) {
                if (pattern.isEmpty()) {
                    errs.append(QStringLiteral(
                        "%1: %2 pattern key must be non-empty")
                        .arg(thisPath, key));
                    continue;
                }
                const QJsonValue patVal = pat.value(pattern);
                if (!patVal.isString() || !contains(allowedActions, patVal.toString())) {
                    errs.append(QStringLiteral(
                        "%1: %2[%3] must be one of ask|allow|deny (got %4)")
                        .arg(thisPath,
                             key,
                             pattern,
                             patVal.isString() ? patVal.toString()
                                               : QStringLiteral("non-string")));
                }
            }
        } else {
            errs.append(QStringLiteral(
                "%1: %2 must be an Action string or pattern object, not %3")
                .arg(thisPath,
                     key,
                     value.type() == QJsonValue::Null
                         ? QStringLiteral("null")
                         : QStringLiteral("array/other")));
        }
    }

    if (!errs.isEmpty()) {
        return ContractCheckResult::fail(errs);
    }
    return ContractCheckResult::pass();
}

ContractCheckResult ContractChecker::checkAgentEntry(
    const QString &agentName,
    const QJsonObject &entry,
    const QString &contextPath) const
{
    return checkAgentEntry(agentName, entry, contextPath, nullptr);
}

ContractCheckResult ContractChecker::checkAgentEntry(
    const QString &agentName,
    const QJsonObject &entry,
    const QString &contextPath,
    const ProviderCatalog *catalog) const
{
    ContractCheckResult r;

    const auto &allowed = agentFields();
    for (const QString &key : entry.keys()) {
        if (!contains(allowed, key)) {
            r.errors.append(QStringLiteral(
                "%1: unknown agent field %2 (not in report §7.1 KNOWN_KEYS for %3)")
                .arg(contextPath, key, agentName));
            r.ok = false;
        }
    }

    // model — must be parseable as provider/model (report §8.3) AND,
    // when a live catalog is provided, must actually exist there.
    if (entry.contains(QStringLiteral("model"))) {
        const QJsonValue mv = entry.value(QStringLiteral("model"));
        if (!mv.isString()) {
            r.errors.append(QStringLiteral(
                "%1.model: must be a string of the form provider/model")
                .arg(contextPath));
            r.ok = false;
        } else {
            r.merge(checkModelString(QStringLiteral("model"),
                                     mv.toString(),
                                     contextPath,
                                     catalog));
        }
    }

    // mode — one of primary|subagent|all (report §7.1 mode union).
    if (entry.contains(QStringLiteral("mode"))) {
        const QJsonValue mv = entry.value(QStringLiteral("mode"));
        if (!mv.isString() || !contains(agentModes(), mv.toString())) {
            r.errors.append(QStringLiteral(
                "%1.mode: must be one of primary|subagent|all (got %2)")
                .arg(contextPath,
                     mv.isString() ? mv.toString()
                                   : QStringLiteral("non-string")));
            r.ok = false;
        }
    }

    // permission — §6.1 schema.
    if (entry.contains(QStringLiteral("permission"))) {
        const QJsonValue pv = entry.value(QStringLiteral("permission"));
        if (!pv.isObject()) {
            r.errors.append(QStringLiteral(
                "%1.permission: must be a JSON object (got %2)")
                .arg(contextPath, pv.type() == QJsonValue::Null
                                      ? QStringLiteral("null")
                                      : QStringLiteral("non-object")));
            r.ok = false;
        } else {
            const QString blockName = QStringLiteral("agent.%1.permission").arg(agentName);
            r.merge(checkPermissionBlock(blockName,
                                          pv.toObject(),
                                          contextPath + QStringLiteral(".permission")));
        }
    }

    // Nested permission appears inside `options.permission`? No — only the
    // top-level per-agent `permission` counts. The nested position is
    // rejected by §7.1 KNOWN_KEYS above (only `model`/`variant`/... are
    // listed).

    return r;
}

ContractCheckResult ContractChecker::checkAgentMap(const QJsonObject &candidate) const
{
    return checkAgentMap(candidate, nullptr);
}

ContractCheckResult ContractChecker::checkAgentMap(
    const QJsonObject &candidate,
    const ProviderCatalog *catalog) const
{
    ContractCheckResult r;
    if (!candidate.contains(QStringLiteral("agent"))) {
        // Absent agent map is acceptable: defaults merge at runtime.
        return r;
    }
    const QJsonValue ag = candidate.value(QStringLiteral("agent"));
    if (!ag.isObject()) {
        r.errors.append(QStringLiteral(
            "top-level \"agent\" must be a JSON object (Record<name, AgentDef>)"));
        r.ok = false;
        return r;
    }
    const QJsonObject map = ag.toObject();
    if (map.isEmpty()) {
        // Empty map is permissible but worth flagging.
        r.warnings.append(QStringLiteral(
            "top-level \"agent\" is an empty map — no agents will be loaded"));
        return r;
    }
    for (const QString &name : map.keys()) {
        const QJsonValue v = map.value(name);
        if (!v.isObject()) {
            r.errors.append(QStringLiteral(
                "agent.%1: must be a JSON object (got %2)")
                .arg(name, v.type() == QJsonValue::Null
                               ? QStringLiteral("null")
                               : QStringLiteral("non-object")));
            r.ok = false;
            continue;
        }
        r.merge(checkAgentEntry(name,
                                 v.toObject(),
                                 QStringLiteral("agent.%1").arg(name),
                                 catalog));
    }
    return r;
}

ContractCheckResult ContractChecker::checkModelString(
    const QString &key,
    const QString &value,
    const QString &contextPath,
    const ProviderCatalog *catalog) const
{
    QString err;
    QString providerId;
    QString modelId;
    if (!parseModel(value, &providerId, &modelId, &err)) {
        // err is the human-readable reason, prefix with key + context.
        Q_UNUSED(key);
        return ContractCheckResult::failOne(QStringLiteral(
            "%1.model: %2").arg(contextPath, err));
    }
    // G1 live-catalog gate. Only run when the caller actually supplied
    // a loaded catalog; structural-only callers (apply_helpers::commit
    // without catalog) keep the §8.3-first-/-split check and skip this.
    if (catalog && catalog->isLoaded()) {
        if (!catalog->isValidModel(value)) {
            return ContractCheckResult::failOne(QStringLiteral(
                "%1.model: %2 is not in the live provider catalog (%3)")
                .arg(contextPath, value,
                     QStringLiteral("see <Global.Path.cache>/models.json")));
        }
    }
    return ContractCheckResult::pass();
}

ContractCheckResult ContractChecker::validateDetailed(const QJsonObject &candidate) const
{
    return validateDetailed(candidate, nullptr);
}

ContractCheckResult ContractChecker::validateDetailed(
    const QJsonObject &candidate,
    const ProviderCatalog *catalog) const
{
    ContractCheckResult merged;

    merged.merge(checkSchemaToken(candidate));
    merged.merge(checkTopLevelKeys(candidate));
    merged.merge(checkAgentMap(candidate, catalog));

    // Top-level `model` and `small_model` — report §4 + §12.3.
    // Structural check that both segments are non-empty around the first '/'.
    // G1 will replace this with a live-catalog lookup; the structural check
    // is the deferred-as-now contract gate.
    if (candidate.contains(QStringLiteral("model"))) {
        const QJsonValue mv = candidate.value(QStringLiteral("model"));
        if (!mv.isString()) {
            ContractCheckResult sub;
            sub.ok = false;
            sub.errors.append(QStringLiteral(
                "top-level \"model\" must be a string of the form provider/model"));
            merged.merge(sub);
        } else {
            merged.merge(checkModelString(QStringLiteral("model"),
                                          mv.toString(),
                                          QStringLiteral("top-level model"),
                                          catalog));
        }
    }
    if (candidate.contains(QStringLiteral("small_model"))) {
        const QJsonValue mv = candidate.value(QStringLiteral("small_model"));
        if (!mv.isString()) {
            ContractCheckResult sub;
            sub.ok = false;
            sub.errors.append(QStringLiteral(
                "top-level \"small_model\" must be a string of the form provider/model"));
            merged.merge(sub);
        } else {
            merged.merge(checkModelString(QStringLiteral("small_model"),
                                          mv.toString(),
                                          QStringLiteral("top-level small_model"),
                                          catalog));
        }
    }

    // Top-level `permission` (rare but legal per §4). Same rules as the
    // agent-level block.
    if (candidate.contains(QStringLiteral("permission"))) {
        const QJsonValue pv = candidate.value(QStringLiteral("permission"));
        if (!pv.isObject()) {
            ContractCheckResult sub;
            sub.ok = false;
            sub.errors.append(QStringLiteral(
                "top-level \"permission\" must be a JSON object"));
            merged.merge(sub);
        } else {
            merged.merge(checkPermissionBlock(QStringLiteral("top-level permission"),
                                              pv.toObject(),
                                              QStringLiteral("permission")));
        }
    }

    // Phase D2-3 / D-11: top-level `default_agent` is a string scalar
    // (report §4 / core/src/v1/config/config.ts:80). Anything else —
    // e.g. a list or number — is a hard reject. Empty string is
    // permitted (opencode picks "build" in that case per stock
    // agent.ts:330-340).
    if (candidate.contains(QStringLiteral("default_agent"))) {
        const QJsonValue v = candidate.value(QStringLiteral("default_agent"));
        if (!v.isString()) {
            ContractCheckResult sub;
            sub.ok = false;
            sub.errors.append(QStringLiteral(
                "top-level \"default_agent\" must be a string scalar "
                "(got %1) per report §4 / D2-3 D-11")
                .arg(v.type() == QJsonValue::Null
                          ? QStringLiteral("null")
                          : QStringLiteral("non-string")));
            merged.merge(sub);
        }
    }

    return merged;
}

bool ContractChecker::validate(const QJsonObject &config, QString *errorMessage)
{
    // Spec'd primary entry (Phase G3). Side-effect free. On first
    // violation populates *errorMessage with a single human-readable
    // reason and returns false; on success returns true and (optionally)
    // clears *errorMessage. Implementation reuses the detailed per-rule
    // checks below so the allow-lists stay in lock-step with tests.
    ContractChecker checker;
    ContractCheckResult result = checker.validateDetailed(config);
    if (!result.ok) {
        if (errorMessage) {
            if (!result.errors.isEmpty()) {
                *errorMessage = result.errors.first();
            } else {
                *errorMessage = QStringLiteral(
                    "contract check failed (no error message produced)");
            }
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool ContractChecker::validate(const QJsonObject &config,
                                const ProviderCatalog *catalog,
                                QString *errorMessage)
{
    // Phase G1 strict entry. Walks every rule from the G3 spec, plus
    // performs a real lookup of every model string against the live
    // catalog when `catalog` is non-null and loaded. If catalog is
    // null or failed to load, we fall back to the structural §8.3
    // check (so callers without a live catalog continue to work).
    ContractChecker checker;
    ContractCheckResult result = checker.validateDetailed(config, catalog);
    if (!result.ok) {
        if (errorMessage) {
            if (!result.errors.isEmpty()) {
                *errorMessage = result.errors.first();
            } else {
                *errorMessage = QStringLiteral(
                    "contract check failed (no error message produced)");
            }
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}
