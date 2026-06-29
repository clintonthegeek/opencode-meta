// TeamRenderer
// Minimal controller that renders a Team (Roles + Specialists) into a
// concrete opencode.json configuration. See docs/PARADIGM.md §2.3 (Team),
// §3 (Topology & OpenCode Mapping), and §5 (Controller Responsibilities).
//
// This renders opencode.json from the Team/Specialist/Role data model.
//
// Extension point: prompt "mad-libs" / variable substitution can be applied
// at the point where we resolve the final agent prompt before inserting it
// into the config. For now we pass Role.systemPrompt / Specialist.promptOverride
// through verbatim.

#pragma once

#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>

#include "models/Role.h"

class Team;
class Specialist;
class Role;

class TeamRenderer
{
public:
    // Render a complete opencode.json object from a Team definition and the
    // referenced Specialist/Role libraries. The output is shaped so that the
    // current OpenCode schema (config.json) can consume it: top-level
    // "$schema", "default_agent", and an "agent" object mapping agent
    // name → AgentConfig.
    static QJsonObject render(const Team &team,
                              const QMap<QString, Specialist> &specialists,
                              const QMap<QString, Role> &roles);

    // Phase C1-2 / D-3: per OPENCODE-CONFIG-INTROSPECTION §6.4,
    // `deriveSubagentSessionPermission` force-denies any subagent
    // that does not carry a `task: allow` rule. This helper injects
    // `task: "allow"` into `*perms` when the agent's mode is
    // subagent/all AND the user has not already set `task` explicitly.
    // Explicit values (including "deny") are preserved verbatim.
    static void ensureSubagentTaskRule(QJsonObject *perms, Role::Mode m);

    // Phase D2-1 / D-10: lift the per-agent stock-fidelity metadata
    // sub-keys (`metadata.native`, `metadata.hidden`, `metadata.color`)
    // up to the matching v1 agent fields. `native` is NOT in the v1
    // KNOWN_KEYS (agent.ts:43) so stock opencode routes it through
    // `options.native`; `hidden` and `color` ARE in KNOWN_KEYS so they
    // land at the agent's top-level. Each lift runs only when the
    // metadata sub-value has the correct schema shape; a wrong shape
    // is dropped with a one-shot qWarning so the user can fix the
    // source Role without a silent false-positive lift.
    //
    // Returns the set of agent-path strings that were lifted
    // (e.g. {"options.native", "hidden", "color"}). Empty on miss or
    // on every shape mismatch. Tests use this for assertion
    // bookkeeping.
    static QSet<QString> liftAgentStringMetadata(QJsonObject &agentObj,
                                                 const Role &role);
};
