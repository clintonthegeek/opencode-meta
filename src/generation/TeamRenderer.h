// TeamRenderer
// Minimal controller that renders a Team (Roles + Specialists) into a
// concrete opencode.json configuration. See docs/PARADIGM.md §2.3 (Team),
// §3 (Topology & OpenCode Mapping), and §5 (Controller Responsibilities).
//
// This intentionally mirrors the existing Template/Profile → opencode.json
// path but starts from the Team/Specialist/Role data model instead.
//
// Extension point: prompt "mad-libs" / variable substitution can be applied
// at the point where we resolve the final agent prompt before inserting it
// into the config. For now we pass Role.systemPrompt / Specialist.promptOverride
// through verbatim.

#pragma once

#include <QJsonObject>
#include <QMap>
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
    // current OpenCode schema (config.json) and OpencodeSchemaAdapter can
    // consume it: top-level "$schema", "default_agent", and an "agent"
    // object mapping agent name → AgentConfig.
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
};
