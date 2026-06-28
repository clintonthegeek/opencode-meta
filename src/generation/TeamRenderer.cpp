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

        // Preserve Role.permissions and tools as raw objects. The current
        // OpenCode schema expects agent-level "permission" and optional
        // deprecated "tools" fields; we simply pass through what the Role
        // stores (PARADIGM §2.1).
        if (!role.permissions.isEmpty()) {
            agentObj.insert(QStringLiteral("permission"), role.permissions);
        }

        if (!role.tools.isEmpty()) {
            agentObj.insert(QStringLiteral("tools"), role.tools);
        }

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
        if (!role.permissions.isEmpty()) {
            agentObj.insert(QStringLiteral("permissions"),
                            flattenPermissions(role.permissions));
        }

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

    // Phase G5 — top-level v2 mirrors for every v1 key the renderer may
    // emit. Today TeamRenderer only emits `agent`/`agents`, but the
    // mirror sites below are wired so a future emission of `provider`
    // or `permission` (e.g. when Roles grow metadata.user-pinned provider
    // overrides, or when a Team gains a top-level permission profile)
    // automatically lights up the v2 partner key without revisiting this
    // function.
    if (root.contains(QStringLiteral("permission"))) {
        root.insert(QStringLiteral("permissions"),
                    root.value(QStringLiteral("permission")));
    }
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
    if (root.contains(QStringLiteral("attachment"))) {
        root.insert(QStringLiteral("attachments"),
                    root.value(QStringLiteral("attachment")));
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

    return root;
}
