// AgentMarkdown
// Renders a Specialist + Role pair to a YAML-frontmatter `.md` file
// compatible with the opencode v2 frontmatter shape (report §12.2
// item 6 / §7.2). Lives outside the runtime load path (the runtime
// loads only `opencode.json`), so this module is OPT-IN and OFF by
// default per ROADMAP D-8 — see `StorageManager::applyTeamToProject`
// for the toggle plumbing.
//
// Shape (v2 frontmatter keys, mirrors ConfigAgent.Info at report
// §7.2 + core/config/agent.ts:13):
//   ---                         (frontmatter delimiter)
//   description: <string>
//   model: <provider/model>
//   mode: <subagent|primary|all>
//   hidden: <boolean>            (omitted when default false; rendering
//                                  keeps an explicit `false` only if the
//                                  Role/Specialist want it)
//   color: <hex or named theme>   (omitted when absent)
//   steps: <positive-int>         (omitted when absent)
//   variant: <string>             (omitted when absent)
//   system: <string or reference> (mirrors Role.systemPrompt + override)
//   request: <boolean|string>     (omitted when absent)
//   permissions: <Permission.Ruleset array>
//   disabled: <boolean>           (mirrors v1 `disable`)
//   ---                         (frontmatter delimiter)
//
// Body: the unrendered markdown prompt body for that agent. Uses
// Specialist.promptOverride (literal text) over Role.systemPrompt,
// mirroring the TeamRenderer's prompt-resolution rule. If neither is
// set, the body is empty.

#pragma once

#include <QString>

class Specialist;
class Role;

namespace AgentMarkdown {

// Render the v2 frontmatter + body for the (Specialist, Role) pair.
// Returns a single QString; callers write it to disk. The delimiter
// is the standard YAML `---` on its own line; both opening and
// closing delimiters are present even when the body is empty so a
// blank `prompt.md` is still valid frontmatter.
QString render(const Specialist &specialist, const Role &role);

} // namespace AgentMarkdown
