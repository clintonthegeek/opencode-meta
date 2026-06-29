#include "TeamRenderer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "models/Role.h"
#include "models/Specialist.h"
#include "models/Team.h"

namespace {

// Flatten a v1 ConfigPermissionV1.Info object into a v2 Permission.Ruleset
// (array of {action, resource, effect}). Mirrors the projection at
// packages/core/src/v1/config/migrate.ts:74-91:
//   * flat Action form ("edit": "allow") -> one rule with resource "*"
//   * pattern form ("edit": {"*.md": "deny"}) -> one rule per pattern.
// The result is emitted under the v2 per-agent `permissions` key so the
// migration bridge (config.ts:135 -> migrate.ts:35) can re-read the file
// without losing per-pattern rules.
QJsonArray flattenPermissions(const QJsonObject &v1Permission)
{
    QJsonArray out;
    for (const QString &key : v1Permission.keys()) {
        const QJsonValue v = v1Permission.value(key);
        if (v.isString()) {
            QJsonObject rule;
            rule.insert(QStringLiteral("action"), key);
            rule.insert(QStringLiteral("resource"), QStringLiteral("*"));
            rule.insert(QStringLiteral("effect"), v.toString());
            out.append(rule);
            continue;
        }
        if (v.isObject()) {
            const QJsonObject pat = v.toObject();
            for (const QString &pattern : pat.keys()) {
                QJsonObject rule;
                rule.insert(QStringLiteral("action"), key);
                rule.insert(QStringLiteral("resource"), pattern);
                const QJsonValue ev = pat.value(pattern);
                if (ev.isString()) {
                    rule.insert(QStringLiteral("effect"), ev.toString());
                }
                out.append(rule);
            }
        }
    }
    return out;
}

} // namespace

// Phase C4-1 / D-6: the renderer STRIPS any permission key that is
// not part of the canonical §6.1 set before emission. The Editor
// surfaces an inline warning while the user is composing (so they can
// fix the source Role), but the file on disk MUST only ever carry the
// 15 legal keys — anything else triggers a hard `InvalidError` at
// opencode load time (see report §6.1 + §12.3). The strip happens
// regardless of the editor's warning state, so a saved-then-loaded
// Role with a stray `foo` key still produces a clean file.
static const QSet<QString> &canonicalPermissionKeySet()
{
    static const QSet<QString> set = []() {
        // Mirrors the 15-key canonical list in
        // RoleEditorDialog::canonicalPermissionKeys() and report §6.1.
        QSet<QString> s;
        const QStringList list = {
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
            QStringLiteral("todowrite"),
            QStringLiteral("question"),
            QStringLiteral("webfetch"),
            QStringLiteral("websearch"),
            QStringLiteral("doom_loop"),
        };
        for (const QString &k : list) {
            s.insert(k);
        }
        return s;
    }();
    return set;
}

void TeamRenderer::ensureSubagentTaskRule(QJsonObject *perms, Role::Mode m)
{
    // Phase C1-2 / D-3: per OPENCODE-CONFIG-INTROSPECTION §6.4,
    // `deriveSubagentSessionPermission` force-denies any subagent
    // session that does not carry a `task: allow` rule. Inject the
    // escape hatch here at render time so the apply path never has to
    // know about this runtime quirk. Primary-only roles are not
    // affected — the runtime only force-denies when the agent is
    // delegated to as a subagent.
    if (!perms) {
        return;
    }
    if (m != Role::Mode::Subagent && m != Role::Mode::All) {
        return;
    }
    // Honour user intent: if the Role already carries any `task`
    // rule (string OR object form) leave it untouched — explicit
    // `task: "deny"` is exactly the escape valve some workflows want.
    if (perms->contains(QStringLiteral("task"))) {
        return;
    }
    perms->insert(QStringLiteral("task"), QStringLiteral("allow"));
}

QSet<QString> TeamRenderer::liftAgentStringMetadata(QJsonObject &agentObj,
                                                    const Role &role)
{
    // Phase D2-1 / D-10: lift `metadata.{native,hidden,color}` from
    // the stock-fidelity Role into the matching v1 agent field. Each
    // lift is shape-gated so a wrong-typed value triggers a one-shot
    // qWarning instead of a silent broken entry. The lift success
    // set is returned so callers (and tests) can assert exactly which
    // paths were lifted.
    QSet<QString> lifted;

    // `native` — boolean metadata.sub-key. Stock opencode routes
    // `native` through `options` because agent.ts:43 KNOWN_KEYS does
    // not list it (the normalize function folds unknown keys into
    // options.native). Lift into options.native — preserve any
    // options the user already set.
    if (role.metadata.contains(QStringLiteral("native"))) {
        const QJsonValue v = role.metadata.value(QStringLiteral("native"));
        if (!v.isBool()) {
            qWarning("TeamRenderer: liftAgentStringMetadata skipping "
                     "metadata.native on role id=%s — expected boolean, "
                     "got %s (Phase D2-1 D-10 contract)",
                     qPrintable(role.id.isEmpty()
                                    ? QStringLiteral("(unnamed)")
                                    : role.id),
                     v.type() == QJsonValue::Null
                         ? QStringLiteral("null")
                         : QStringLiteral("non-bool"));
        } else {
            QJsonObject options = agentObj.value(QStringLiteral("options"))
                                      .toObject();
            options.insert(QStringLiteral("native"), v);
            agentObj.insert(QStringLiteral("options"), options);
            lifted.insert(QStringLiteral("options.native"));
        }
    }

    // `hidden` — boolean metadata sub-key. v1 KNOWN_KEYS lists
    // `hidden` (agent.ts:43) so it lands at the agent's top-level.
    if (role.metadata.contains(QStringLiteral("hidden"))) {
        const QJsonValue v = role.metadata.value(QStringLiteral("hidden"));
        if (!v.isBool()) {
            qWarning("TeamRenderer: liftAgentStringMetadata skipping "
                     "metadata.hidden on role id=%s — expected boolean, "
                     "got %s (Phase D2-1 D-10 contract)",
                     qPrintable(role.id.isEmpty()
                                    ? QStringLiteral("(unnamed)")
                                    : role.id),
                     v.type() == QJsonValue::Null
                         ? QStringLiteral("null")
                         : QStringLiteral("non-bool"));
        } else {
            agentObj.insert(QStringLiteral("hidden"), v);
            lifted.insert(QStringLiteral("hidden"));
        }
    }

    // `color` — string metadata sub-key. Color can be a hex string
    // `#RRGGBB` or a theme name `primary|secondary|accent|success|
    // warning|error|info` per agent.ts:7-10. We shape-gate on "is
    // string" only — the runtime enforces the actual pattern/value
    // match.
    if (role.metadata.contains(QStringLiteral("color"))) {
        const QJsonValue v = role.metadata.value(QStringLiteral("color"));
        if (!v.isString()) {
            qWarning("TeamRenderer: liftAgentStringMetadata skipping "
                     "metadata.color on role id=%s — expected string, "
                     "got %s (Phase D2-1 D-10 contract)",
                     qPrintable(role.id.isEmpty()
                                    ? QStringLiteral("(unnamed)")
                                    : role.id),
                     v.type() == QJsonValue::Null
                         ? QStringLiteral("null")
                         : QStringLiteral("non-string"));
        } else {
            agentObj.insert(QStringLiteral("color"), v);
            lifted.insert(QStringLiteral("color"));
        }
    }

    return lifted;
}

QJsonObject TeamRenderer::render(const Team &team,
                                 const QMap<QString, Specialist> &specialists,
                                 const QMap<QString, Role> &roles)
{
    QJsonObject root;

    // Always emit the official schema so OpenCode and users can validate.
    root.insert(QStringLiteral("$schema"), QStringLiteral("https://opencode.ai/config.json"));

    // Build a reverse index so we can map primarySpecialistIds → role ids.
    // Team.specialists is defined as roleId → specialistId (PARADIGM §2.3).
    QMap<QString, QString> specialistToRole;
    for (const auto &binding : team.specialists) {
        const QString &roleId = binding.roleId;
        const QString &specId = binding.specialistId;
        if (roleId.isEmpty() || specId.isEmpty()) {
            continue;
        }
        specialistToRole.insert(specId, roleId);
    }

    // Choose default_agent from the team's primary specialists (PARADIGM §3).
    QString defaultAgentName;
    for (const QString &primarySpecId : team.primarySpecialistIds) {
        const QString roleId = specialistToRole.value(primarySpecId);
        if (roleId.isEmpty()) {
            continue;
        }
        if (!roles.contains(roleId)) {
            continue;
        }
        defaultAgentName = roleId;
        break;
    }

    QJsonObject agentsObj;

    // Render each specialist binding into an agent entry. For each
    // (roleId → specialistId) pair we resolve the Role and Specialist and
    // map them into the current AgentConfig shape (PARADIGM §5).
    for (const auto &binding : team.specialists) {
        const QString &roleId = binding.roleId;
        const QString &specId = binding.specialistId;

        const auto specIt = specialists.constFind(specId);
        const auto roleIt = roles.constFind(roleId);
        if (specIt == specialists.constEnd() || roleIt == roles.constEnd()) {
            // Skip incomplete bindings; a minimal but valid config is better
            // than emitting broken agent entries.
            continue;
        }

        const Specialist &spec = specIt.value();
        const Role &role = roleIt.value();

        // Agent name matches the Role id / Team.specialists key so that
        // downstream tools (and users) see the expected identifiers
        // (e.g. "build", "plan", "general").
        const QString agentName = role.id.isEmpty() ? roleId : role.id;

        QJsonObject agentObj;

        if (!spec.modelId.isEmpty()) {
            // Bind the concrete model from the Specialist (PARADIGM §2.2).
            agentObj.insert(QStringLiteral("model"), spec.modelId);
        }

        // Resolve the final system prompt:
        // - start from Role.systemPrompt
        // - override with Specialist.promptOverride when present.
        //
        // This is the natural hook for future prompt interpolation or
        // variable substitution if we extend Roles/Teams (see PARADIGM §5).
        QJsonValue promptValue;
        if (!spec.promptOverride.isUndefined() && !spec.promptOverride.isNull()) {
            promptValue = spec.promptOverride;
        } else {
            promptValue = role.systemPrompt;
        }
        if (!promptValue.isUndefined() && !promptValue.isNull()) {
            agentObj.insert(QStringLiteral("prompt"), promptValue);
        }

        if (!role.description.isEmpty()) {
            agentObj.insert(QStringLiteral("description"), role.description);
        }

        // Preserve the Role's mode; Team.primarySpecialistIds determines
        // which agent becomes default_agent rather than changing per-agent
        // mode flags.
        agentObj.insert(QStringLiteral("mode"), Role::modeToString(role.mode));

        // Preserve Role.permissions as a raw object. The current OpenCode
        // schema expects agent-level "permission" (PARADIGM §2.1). Copy
        // into a local object so we can mutate it without touching the
        // caller's Role — Phase C1-2 needs to inject `task: allow` for
        // subagent-mode agents that lack an explicit `task` rule.
        QJsonObject perms = role.permissions;
        TeamRenderer::ensureSubagentTaskRule(&perms, role.mode);

        // Phase C3-2 / D-5: when the role is read-only AND primary,
        // omit `edit` and `bash` keys entirely so weak-model tool-call
        // failures (report §6.4) can't bite. Per-rule Action overrides
        // are preserved (if the user explicitly set `edit: "deny"` on
        // a non-read-only role and then toggled read-only on a SECOND
        // role, that second role loses its edit/bash entries). The
        // qWarning catches the surprising case where the role had
        // explicit edit / bash values that we are *removing* on
        // render — this is a deliberate choice for the read-only
        // flag, and the warning makes it visible in the test log so
        // we don't accidentally drop user intent.
        bool droppedReadOnlyWriteKeys = false;
        if (role.readOnly && role.mode == Role::Mode::Primary) {
            if (perms.contains(QStringLiteral("edit"))) {
                perms.remove(QStringLiteral("edit"));
                droppedReadOnlyWriteKeys = true;
            }
            if (perms.contains(QStringLiteral("bash"))) {
                perms.remove(QStringLiteral("bash"));
                droppedReadOnlyWriteKeys = true;
            }
        }
        if (droppedReadOnlyWriteKeys) {
            qWarning("TeamRenderer: role id=%s (mode=Primary readOnly=true) "
                     "dropping 'edit' and 'bash' permission keys per "
                     "OPENCODE-CONFIG-INTROSPECTION §6.4 / C3-2 D-5; the "
                     "opencode defaults will take over for read-only "
                     "primary roles",
                     qPrintable(role.id.isEmpty() ? QStringLiteral("(unnamed)")
                                                 : role.id));
        }

        // Phase C4-1 / D-6: strip any permission key that is not in
        // the canonical §6.1 set. The editor lets the user type free-
        // form keys (so a mis-typed or pre-v1 key isn't lost on edit),
        // but the file MUST only ever carry the 15 legal keys. The
        // qWarning records the dropped keys so the user can fix the
        // source Role. The strip happens regardless of warning state.
        QStringList strippedKeys;
        {
            QStringList toRemove;
            const QSet<QString> canonical = canonicalPermissionKeySet();
            for (auto it = perms.constBegin(); it != perms.constEnd(); ++it) {
                if (!canonical.contains(it.key())) {
                    toRemove.append(it.key());
                }
            }
            for (const QString &k : toRemove) {
                perms.remove(k);
                strippedKeys.append(k);
            }
        }
        if (!strippedKeys.isEmpty()) {
            qWarning("TeamRenderer: role id=%s stripped %d non-canonical "
                     "permission key(s) [%s] before emission per "
                     "OPENCODE-CONFIG-INTROSPECTION §6.1 / C4-1 D-6; the "
                     "opencode runtime rejects unknown keys with InvalidError. "
                     "Edit the source Role to remove these keys; the editor's "
                     "Permissions tab surfaces the warning inline.",
                     qPrintable(role.id.isEmpty() ? QStringLiteral("(unnamed)")
                                                 : role.id),
                     strippedKeys.size(),
                     qPrintable(strippedKeys.join(QStringLiteral(", "))));
        }

        if (!perms.isEmpty()) {
            agentObj.insert(QStringLiteral("permission"), perms);
        }

        // D-4 / Phase C1-1: do NOT emit `tools`. Per OPENCODE-CONFIG-INTROSPECTION
        // §6.3 the runtime folds `tools:{write:false}` into `permission:{edit:"deny"}`
        // "fragilely"; we drop the deprecated key on render and rely on the
        // RoleEditorDialog banner to nudge legacy `tools` blocks toward the
        // Permissions tab. `role.tools` is still loaded/edited (see Role.cpp)
        // so the editor's migration surface keeps working.
        // Today's `Role::tools` carries only deprecated `{name: true|false}`
        // entries, so no non-deprecated key can survive — the conditional
        // emission below stays as a placeholder until the spec introduces a
        // non-deprecated `tools` key.

        // Phase G5 — v2 sidecar emission alongside the v1 keys.
        // `system` mirrors the v1 `prompt` value so the migration bridge
        // (migrateAgent at packages/core/src/v1/config/migrate.ts:106-125)
        // can re-read the agent under v2-only runtimes.
        if (!promptValue.isUndefined() && !promptValue.isNull()) {
            agentObj.insert(QStringLiteral("system"), promptValue);
        }

        // `disabled` mirrors v1 `disable` when present. The renderer does
        // not currently emit `disable`, so this slot is a forward-
        // compatibility no-op today but is wired so a future field on
        // Role/Specialist that maps to `disable` lights up the v2 mirror
        // automatically once the v1 emission lands.
        if (agentObj.contains(QStringLiteral("disable"))) {
            agentObj.insert(QStringLiteral("disabled"),
                            agentObj.value(QStringLiteral("disable")));
        }

        // `request` mirrors v1 `request` when present.
        if (agentObj.contains(QStringLiteral("request"))) {
            agentObj.insert(QStringLiteral("request"),
                            agentObj.value(QStringLiteral("request")));
        }

        // v2 `permissions` is the flattened Ruleset form of v1
        // `permission` (packages/core/src/v1/config/migrate.ts:74-91).
        // Use the (possibly-injected) local `perms` so the v2 mirror
        // picks up the C1-2 `task: allow` injection for subagent-mode
        // agents. Without this, v2 would lag v1 by one rule.
        if (!perms.isEmpty()) {
            agentObj.insert(QStringLiteral("permissions"),
                            flattenPermissions(perms));
        }

        // Phase D2-1 / D-10: lift `metadata.native / hidden / color`
        // to the matching v1 agent field. Done last so any prior
        // v2-mirror write (`options`) wins through a fresh merge on
        // the lift side. The helper logs a one-shot qWarning per
        // shape mismatch so users can fix the source Role if a
        // future stock key change trips a new shape gate.
        TeamRenderer::liftAgentStringMetadata(agentObj, role);

        agentsObj.insert(agentName, agentObj);
    }

    if (!agentsObj.isEmpty()) {
        // Current config schema uses top-level "agent" for the agent map.
        root.insert(QStringLiteral("agent"), agentsObj);

        // Phase G5 — v2 sidecar: emit the camelCase `agents` mirror of
        // the v1 `agent` map. Same QJsonObject — the per-agent v2 fields
        // are baked into the same entries (see loop above), so `agents`
        // is a complete Ruleset-form mirror.
        root.insert(QStringLiteral("agents"), agentsObj);
    }

    // Phase G5 / Phase C1-3 — top-level v2 mirrors for every v1 key the
    // renderer may emit. Today TeamRenderer only emits `agent`/`agents`,
    // but the mirror sites below are wired so a future emission of
    // `provider` / `snapshot` / `small_model` (e.g. when Roles grow
    // metadata.user-pinned provider overrides, or when a Team gains a
    // top-level model snapshot / small-model override) automatically
    // lights up the v2 partner key without revisiting this function.
    //
    // Per Phase C1-3 / D-1, the `permission -> permissions` and
    // `attachment -> attachments` 1:1 mirrors are INTENTIONALLY OMITTED:
    // the v2 form of those keys is structurally different from v1
    // (top-level `permissions` is a `Permission.Ruleset` array per
    // report §6.2 / `packages/schema/src/permission.ts:64`, not a
    // `ConfigPermissionV1.Info` object — and `attachments` is a list
    // attachment behaviour surface, not a 1:1 object copy). A wrong-
    // shaped v2 mirror would be either silently coerced by the
    // migration bridge (`packages/core/src/v1/config/migrate.ts:35`)
    // or trigger `InvalidError` at `opencode debug config` time. Per
    // D-1's "files that nobody can run" rationale, OMIT is the safer
    // default. If/when the renderer grows a top-level v1 `permission`
    // or `attachment` source, the mirror MUST be a structurally-correct
    // Ruleset / list (not a 1:1 copy) before this site is re-enabled.
    if (root.contains(QStringLiteral("provider"))) {
        root.insert(QStringLiteral("providers"),
                    root.value(QStringLiteral("provider")));
    }
    if (root.contains(QStringLiteral("snapshot"))) {
        root.insert(QStringLiteral("snapshots"),
                    root.value(QStringLiteral("snapshot")));
    }
    if (root.contains(QStringLiteral("small_model"))) {
        root.insert(QStringLiteral("smallModel"),
                    root.value(QStringLiteral("small_model")));
    }

    // If we found a primary specialist, wire its roleId as default_agent.
    if (!defaultAgentName.isEmpty()) {
        root.insert(QStringLiteral("default_agent"), defaultAgentName);
    } else if (!agentsObj.isEmpty()) {
        // Fallback: pick the first Primary/All agent so the config remains
        // usable even if the Team omitted primarySpecialistIds. This matches
        // the adapter's defaulting behavior.
        const QStringList agentNames = agentsObj.keys();
        for (const QString &name : agentNames) {
            const QJsonObject agent = agentsObj.value(name).toObject();
            const QString mode = agent.value(QStringLiteral("mode")).toString();
            if (mode == QLatin1String("primary") || mode == QLatin1String("all")) {
                root.insert(QStringLiteral("default_agent"), name);
                break;
            }
        }
    }

    // Phase D2-2 / D-11: explicit `team.metadata.default_agent`
    // override wins over the inferred value above. The renderer
    // surfaces whatever string the user put under team.metadata
    // (snake_case) or team.metadata.defaultAgent (camelCase). The v2
    // `defaultAgent` mirror is emitted only when the source value is
    // itself a string — per D-1 "only emit v2 sidecars when v2 form
    // is structurally correct".
    auto liftDefaultAgent = [&](const QString &metaKey) {
        if (!team.metadata.contains(metaKey)) {
            return;
        }
        const QJsonValue v = team.metadata.value(metaKey);
        if (!v.isString()) {
            qWarning("TeamRenderer: team.metadata.%s is not a string; "
                     "skipping as default_agent override (Phase D2-2 D-11 "
                     "contract)",
                     qPrintable(metaKey));
            return;
        }
        const QString s = v.toString();
        if (s.isEmpty()) {
            // Empty == no override; the inferred default_agent above
            // still wins. No qWarning — empty override is an explicit
            // "use the default".
            return;
        }
        root.insert(QStringLiteral("default_agent"), s);
        // v2 mirror (Phase D2-2 / D-1 symmetric safe scalars).
        root.insert(QStringLiteral("defaultAgent"), s);
    };
    liftDefaultAgent(QStringLiteral("default_agent"));
    liftDefaultAgent(QStringLiteral("defaultAgent"));

    // Phase C6-1 / D-1 / report §5.9: lift `Role::metadata.<kind>` to
    // the corresponding v1 top-level key. The Role model exposes
    // arbitrary `metadata` as a free-form QJsonObject; we honour four
    // documented sub-keys (mcpEntries, lspEntries, formatterEntries,
    // referenceEntries) and merge them into the renderer output.
    //
    // Behavioural contract:
    //   * If MULTIPLE Roles declare entries for the same key, the
    //     merge is shallow-merge by id — last-write-wins per
    //     delegate / record. This is what the opencode runtime expects
    //     (the v1 union shape is `Record<id, ...Entry>`, report §4).
    //   * Each entry's id becomes the top-level key; the value is
    //     copied through unmodified so the user controls the exact
    //     shape (boolean for lsp global, Record<string, Entry>
    //     otherwise). Custom keys are NOT validated — that's the
    //     ContractChecker's job at commit time (C2 / D-2).
    //   * v2 `providers` mirror is NOT emitted here; that's a
    //     structurally-correct mirror distinct from v1 `provider` and
    //     per D-1 we only emit v2 sidecars when the v2 form is
    //     structurally correct.
    auto mergeMapFromRoles = [&root, &roles](const QString &metaKey,
                                              const QString &topLevel) {
        QJsonObject merged;
        for (auto rIt = roles.constBegin(); rIt != roles.constEnd(); ++rIt) {
            const Role &r = rIt.value();
            if (!r.metadata.contains(metaKey)) {
                continue;
            }
            const QJsonValue v = r.metadata.value(metaKey);
            if (!v.isObject()) {
                // Invalid shape — skip with a qWarning; ContractChecker
                // would catch this anyway at commit time, but skipping
                // here keeps the renderer output minimal.
                qWarning("TeamRenderer: role id=%s metadata.%s is not an "
                         "object; skipping per Phase C6-1 / report §5.9 "
                         "shape contract",
                         qPrintable(r.id.isEmpty()
                                        ? QStringLiteral("(unnamed)")
                                        : r.id),
                         qPrintable(metaKey));
                continue;
            }
            const QJsonObject entries = v.toObject();
            for (auto eIt = entries.constBegin();
                 eIt != entries.constEnd(); ++eIt) {
                merged.insert(eIt.key(), eIt.value());
            }
        }
        if (!merged.isEmpty()) {
            root.insert(topLevel, merged);
        }
    };

    // C6-1: the four workspace-server surfaces documented in §5.9.
    mergeMapFromRoles(QStringLiteral("mcpEntries"), QStringLiteral("mcp"));
    mergeMapFromRoles(QStringLiteral("lspEntries"), QStringLiteral("lsp"));
    mergeMapFromRoles(QStringLiteral("formatterEntries"),
                      QStringLiteral("formatter"));
    mergeMapFromRoles(QStringLiteral("referenceEntries"),
                      QStringLiteral("references"));

    // C6-3: provider-options lift. Same pattern as C6-1 but the
    // top-level key is `provider` (v1) and we explicitly do NOT emit
    // the v2 `providers` mirror unless the source value is a
    // structurally-correct Provider entry. Today the v1 shape is
    // `Record<id, ConfigProviderV1.Info>` (opencode
    // packages/core/src/v1/config/provider.ts:76) which IS the
    // shape we copy through, so the v2 `providers` mirror is a
    // 1:1 same-shape copy and is therefore safe to emit per D-1.
    QJsonObject providerMap;
    for (auto rIt = roles.constBegin(); rIt != roles.constEnd(); ++rIt) {
        const Role &r = rIt.value();
        if (!r.metadata.contains(QStringLiteral("providerOptions"))) {
            continue;
        }
        const QJsonValue v = r.metadata.value(QStringLiteral("providerOptions"));
        if (!v.isObject()) {
            qWarning("TeamRenderer: role id=%s metadata.providerOptions is "
                     "not an object; skipping per Phase C6-3 contract",
                     qPrintable(r.id.isEmpty()
                                    ? QStringLiteral("(unnamed)")
                                    : r.id));
            continue;
        }
        const QJsonObject entries = v.toObject();
        for (auto eIt = entries.constBegin();
             eIt != entries.constEnd(); ++eIt) {
            providerMap.insert(eIt.key(), eIt.value());
        }
    }
    if (!providerMap.isEmpty()) {
        // Per C1-3 / D-1 the `provider -> providers` mirror is kept
        // (same QJsonObject shape — both v1 and v2 use a Record<id,
        // ...Entry> form here). v2 `providers` is a partial mirror
        // (no extra keys, just camelCase alias per Schema's
        // `packages/core/src/config/provider.ts:65`); a structurally
        // correct 1:1 alias is therefore safe.
        root.insert(QStringLiteral("provider"), providerMap);
        root.insert(QStringLiteral("providers"), providerMap);
    }

    return root;
}
