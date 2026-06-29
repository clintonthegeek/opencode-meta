# OpenCode Meta – Paradigm Specification

**Version:** 0.2  
**Date:** 2026-06-28  
**Status:** Active — supersedes the 0.1 draft and the original Template/Profile roadmap  
**Goal:** Define the user-facing mental model, the data model, and the generation contract that opencode-meta-qt must satisfy. Generation, validation, and apply-path rules are now anchored to `docs/OPENCODE-CONFIG-INTROSPECTION.md`.

---

## 0. How to Read This Document

This is the *what and why*. The *how* — exact file locations, line numbers, schema identifiers, generator rules, and the one-shot sanity check — lives in **`/home/clinton/dev/opencode-meta/docs/OPENCODE-CONFIG-INTROSPECTION.md`** (the "introspection report"). The report is a sibling document; treat it as authoritative for every concrete claim below.

Concretely:

- §1 in this doc ("Data Model") maps onto §4 + §7 of the report.
- §5 ("Config Generation Contract") maps onto §1, §3, §6, §11, §12.
- §6 ("GUI/UX Implications") maps onto the runtime behavior in §6 + §7 (permission/disabled semantics) and §8 (provider/model discovery).

If this doc and the report ever disagree, the report wins.

---

## 1. Guiding Metaphor

We treat the tool as a **laboratory notebook** for AI coding teams.

- You design **Roles** (jobs with system prompts and permissions).
- You create **Specialists** (concrete people filling those Roles with specific models).
- You compose **Teams** (named, reusable groups of Specialists with one or more primaries).
- You run **Trials** (apply a Team to a real project, do work, record observations/ratings).

This cleanly separates:

- What the Role should *think* (Role).
- Which model is *thinking* it (Specialist).
- Which combination we are currently trying (Team).
- How well it actually performed (Trial).

---

## 2. Core Entities

### 2.1 Role

A stable job description, system prompt, and default permission profile.

| Field | Type | Notes |
|---|---|---|
| `id` | string | unique; emitted as `agent.<id>` in `opencode.json` |
| `name` | string | human label ("Coder", "Planner") |
| `description` | string | shown in opencode `@` autocomplete |
| `systemPrompt` | string | body for `agent.<id>.prompt` (v1); mirrors v2 `system` |
| `mode` | `"primary"\|"subagent"\|"all"` | mirrors opencode `agent.<id>.mode` |
| `permissions` | object | the `permission` map emitted under `agent.<id>.permission` |
| `metadata` | object | free-form; not emitted, kept locally |

Built-in Roles shipped with the app: `build` (full access), `plan` (read-only analysis), `general` (multi-step sub-agent), `explore` (snapshot, already provided by opencode).

### 2.2 Specialist

One concrete filling of a Role.

| Field | Type | Notes |
|---|---|---|
| `id` | string | local; never written to `opencode.json` |
| `roleId` | ref → Role | required |
| `modelId` | `provider/model` | **must** parse via `Provider.parseModel` against the live catalog (report §8.3) |
| `name` | string? | free ("Grok-4 Coder") |
| `promptOverride` | string? | small variation on the Role's system prompt; emitted as `options.body` (v2) or appended to `prompt` (v1, deprecated path) |
| `metadata` | object | free-form |

A Specialist is the atomic model-swap unit. Two Specialists with the same `roleId` exist precisely to enable A/B testing.

### 2.3 Team

A named, versionable, reusable collection of Specialists.

| Field | Type | Notes |
|---|---|---|
| `id` | string | local |
| `name` | string | human label |
| `description` | string | free-form |
| `primarySpecialistIds` | string[] | one or more; rendered as `default_agent` (the first) plus `@name` candidates for the rest |
| `specialists` | Specialist[] | order is significant (rendered in array order) |
| `version` | semver/integer | manual bump on each save |
| `parentTeamId` | string? | future-recursive placeholder |
| `metadata` | object | free-form |

**Invariants**:

- At least one `primarySpecialistId`.
- All `specialists[].roleId` must be distinct inside the Team.
- The combined rendered permission set must include a `task` allow for any Specialist declared as `mode: subagent` (see §5.4 — this avoids the explorer-agent error described in report §6.4).

### 2.4 Trial

| Field | Type | Notes |
|---|---|---|
| `id` | string | local |
| `teamId` | ref → Team | required |
| `projectPath` | string | absolute |
| `timestamp` | ISO 8601 | set on creation |
| `notes` | string | free-form |
| `ratings` | object | `{ promptAdherence, codeQuality, overall }`, 1–5 |
| `renderedConfigSnapshot` | string? | the exact `opencode.json` written at apply time |
| `durationMinutes` | int? | optional |

---

## 3. Mapping to opencode

| opencode-meta concept | opencode runtime artifact |
|---|---|
| Role | one entry in `agent` map of `opencode.json` |
| Specialist | binds a `model` string onto the Role's agent entry |
| Team (`specialists[]`) | N entries in the `agent` map (one per Specialist) |
| Team (`primarySpecialistIds[0]`) | the value of `default_agent` |
| Trial | the act of `PATCH /config` (or filesystem write) + a local record |

The `opencode.json` produced by a Team has, structurally:

```
$schema: "https://opencode.ai/config.json"
default_agent: <primary-specialist-role-id>
agent:
  <role-id-1>: { prompt, model, permission, mode, ... }
  <role-id-2>: { prompt, model, permission, mode, ... }
  ...
provider:
  <provider-name>: { ... }      # only if user pinned provider-level options
small_model: "..."              # optional
mcp: { ... }                    # optional, picked up from Roles metadata
instructions: [ ... ]           # optional, concatenated from Roles metadata
```

Plus v2-mirror keys per §5.1 of this document.

---

## 4. Data Storage Layout

```
~/.opencode-meta/
├── roles/<role-id>.json
├── specialists/<specialist-id>.json
├── teams/<team-id>.json
├── trials/<trial-id>.json
├── projects.json              (path → activeTeamId + lastTrialId)
├── models-cache.json          (live snapshot of models.dev)
└── schema-version.txt         ("v2.0-role-team-trial" — bump on data-model changes)
```

The `schema-version` file is read on startup; mismatches trigger an internal migration prompt. Bump explicitly in `StorageManager`.

---

## 5. Config Generation Contract (NEW — Binding)

**This section is the agreement between `TeamRenderer`/`apply_helpers` and opencode's runtime loaders.** Every generated file must pass. The introspection report (notably §3, §4, §6.1, §7.1, §12) is the single source of truth for every rule below.

### 5.1 Always emit the v1 *and* the v2 shape

The runtime loader at `packages/opencode/src/config/config.ts:232` still requires the literal `"$schema":"https://opencode.ai/config.json"` and uses `ConfigV1.Info` at `packages/core/src/v1/config/config.ts:32` as the wire format. The v2 loader at `packages/core/src/config.ts:135` reads the same file but projects it through `ConfigMigrateV1.migrate` (`packages/core/src/v1/config/migrate.ts:35`).

`TeamRenderer` must emit both shapes in one file so the file round-trips through either loader:

- v1 keys (snake_case): `$schema`, `default_agent`, `small_model`, `disabled_providers`, `enabled_providers`, `autoshare`, `autoupdate`, `tool_output`, `attachment`, `tool_output.max_lines`, `compaction`, `permission`, `agent`, `provider`, `mcp`, `lsp`, `formatter`, `instructions`.
- v2 keys (camelCase, sidecar): `agents`, `permissions` (as ordered Ruleset), `providers`, `system` (in agent `.md` frontmatter), `disabled`, `snapshots`.

What you emit must belong to one of the 40+ keys exactly listed in report §4 (`ConfigV1.Info`). Anything else triggers `topLevelExtraKeys` rejection at `parse.ts:74–78`.

### 5.2 The one-line rule (report §12.3)

> Emit a `ConfigV1.Info` object, with `"$schema":"https://opencode.ai/config.json"` at the top, using only the 40+ keys listed in §4, with every `permission` value being one of the 15 legal keys in §6.1, every `agent` entry using only the fields in §7.1, and every `model` string parsable by `Provider.parseModel` against the live catalog.

If we cannot satisfy that rule, we **do not** write the file. The apply path refuses with a clear error.

### 5.3 `$schema` token

Always emit the literal `"$schema": "https://opencode.ai/config.json"` at the top of every `opencode.json`. Report §3 enumerates the four injection sites in opencode itself — we re-emit the same string pre-write so editors and `opencode debug config` agree.

For agent `.md` files (and only those): the frontmatter uses v2 keys (`system`, `request`, `permissions`, `disabled`, `steps`) per report §12.1 item 6, so they parse cleanly under future v2-only runtimes.

### 5.4 Permission generation rules (binding)

These rules are derived from the runtime semantics in report §6 (`Permission.fromConfig`, `Permission.disabled`, `Permission.evaluate`) and §7.4 (subagent derivation in `subagent-permissions.ts:14`).

1. **All 15 legal keys only.** Within `permission`, only these keys are allowed:
   `read, edit, glob, grep, list, bash, task, external_directory, lsp, skill, todowrite, question, webfetch, websearch, doom_loop` (report §6.1).
2. **Action vocabulary is closed.** Every value is exactly `"ask"|"allow"|"deny"` (`packages/core/src/v1/config/permission.ts:5`). `"approve"`, `"forbidden"`, etc. will fail `Schema.Literals`.
3. **Subagent `task` escape hatch (MANDATORY).** Any Specialist whose `role.mode` is `"subagent"` or `"all"` MUST also receive a `task:"allow"` rule. Without it, `deriveSubagentSessionPermission` (`packages/opencode/src/agent/subagent-permissions.ts:14–27`) injects a `task * deny` into the subagent session, and the explorer pattern (report §6.4) recurs.
4. **Read-only Roles.** If `role.mode === "primary"` AND the user has marked the Role "read-only", do NOT emit `edit` or `bash` at all; let the opencode defaults take over. The omission prevents weak-model tool-calling failures (report §6.4).
5. **Deprecated `tools` map must NOT appear.** The runtime folds it into `permission` at `config.ts:552–563` but the load path is fragile. Always emit `permission` directly.
6. **`write` / `patch` → `edit`.** Internally, `disabled` collapses `write`/`patch`/`apply_patch` into `edit` (report §6.3 step 5). The renderer emits only `edit`.
7. **Per-pattern Rules.** `Rule = Action | Record<pattern, Action>` (report §6.1). Allowed for the `Rule`-typed keys (`read, edit, glob, grep, list, bash, task, external_directory, lsp, skill`); Action-only for the rest (`todowrite, question, webfetch, websearch, doom_loop`).
8. **Opencode defaults MERGE.** Our emission is the per-agent `permission`, not the global one. The merged effective ruleset at runtime = defaults + builtin overrides + user global `permission` + per-agent `permission` (in that precedence, *later wins* via `findLast`, report §13 "Subagent inheritance quick rule"). We emit **only** the per-agent slice; we never try to embed defaults ourselves.

### 5.5 Agent entry shape

Per `ConfigAgentV1.Info` (report §7.1, file `packages/core/src/v1/config/agent.ts:83`). The `KNOWN_KEYS` set (line 43) silently routes anything else into `options` — we therefore emit *exactly*:

`model`, `variant`, `temperature`, `top_p`, `prompt`, `description`, `mode`, `hidden`, `color`, `steps`, `permission`, `disable`. Plus an internal `name` (which sinks into `options` legacy-side, which is acceptable).

For agent `.md` files we emit v2 keys: `model`, `variant`, `system`, `request`, `permissions`, `disabled`, `steps`, `description`, `mode`, `hidden`, `color`.

`disable` (v1) ↔ `disabled` (v2). Emit `disable` in JSON config files, `disabled` in `.md` frontmatter.

### 5.6 Live provider / model discovery

`TeamRenderer` does **not** hard-code provider or model strings. Per report §8 and §12.2 item 1:

- On startup, refresh the live catalog via `opencode models --refresh` (CLI) OR read `<Global.Path.cache>/models.json` (path reported in §8.4) OR call `GET /provider` against a running instance (report §10.2).
- `parseModel("provider/model")` semantics: first `/` splits; everything after joins with `/` again (report §8.3). `fireworks-ai/<anything>`, `openrouter/anthropic/claude-3-opus`, and `xai/grok-4.3` are all valid.
- Capability flags (`toolcall`, `reasoning`, `attachment`, `interleaved`, `modalities`) come from models.dev + plugin overrides; they are **never** runtime-probed (report §8.5). Trust the catalog.
- When the user swaps Specialist models, revalidate the Specialist's `permission` set against the new model's capability flags. Models without `toolcall=true` MUST NOT be bound to write/edit-enabled Specialists unless the user explicitly accepts the warning (this is the "hot-swap matrix" in §12.2 item 4).

### 5.7 Schema validation gate (MANDATORY)

After `TeamRenderer` finishes writing an `opencode.json`, **always** run:

```
opencode debug config   # in the directory containing the emitted file
```

- Exit code MUST be 0.
- stderr MUST be empty.
- If `opencode` is not installed on the host, fall back to a pure-JSON validator that emulates `ConfigParse.schema` (`packages/opencode/src/config/parse.ts:35–72`): walk every emitted top-level key against §4 of the report, walk every `permission` key against §6.1, walk every agent against §7.1, and run `parseModel` on every `model` string.

The validate step is called from `apply_helpers::commit()` immediately before filesystem write. Failures short-circuit the write.

### 5.8 `$schema`, version, environment handling

| Concern | Behavior |
|---|---|
| Top-level `$schema` | Always the literal `"https://opencode.ai/config.json"` (report §3) |
| `default_agent` | Set to `team.primarySpecialistIds[0]`'s `roleId` |
| Unknown fields preserved | `OpencodeSchemaAdapter` used to do pass-through; now replaced with a tighter ContractChecker that REJECTS unknown keys (because `parse.ts:74` does anyway) |
| `OPENCODE_CONFIG_CONTENT` / `OPENCODE_PERMISSION` env override | We do NOT write inside the opencode process; users who want env layering set it themselves |

### 5.9 MCP / LSP / formatter / reference

These surface in the GUI but the renderer maps directly to the v1 union shapes:

- `mcp`: `Record<id, ConfigMCPV1.Info \| {enabled: boolean}>` (`packages/core/src/v1/config/mcp.ts:62`). v2 mirror: `mcp.servers` (report §8.3).
- `lsp`: `boolean \| Record<id, Entry>` (`packages/core/src/v1/config/lsp.ts:76`). Custom entries MUST carry `extensions` per `lsp.ts:63–73`.
- `formatter`: `boolean \| Record<id, Entry>` (`packages/core/src/v1/config/formatter.ts:12`).
- `references`: `ConfigReference.Info` (`packages/core/src/config/reference.ts:18`) for git `{repository, branch?, description?, hidden?}` or local `{path, description?, hidden?}` shapes.

Report §12.2 item 8 governs the reference shape; we expose a toggle on the Roles (or Team) view that lifts `references` to the top level of the rendered `opencode.json`.

**Metadata subkeys (Phase C6-1):** the source of truth for these
surfaces inside `Role::metadata` is documented as four sub-keys:

| `Role::metadata.<sub>` | Top-level lift | Shape |
|---|---|---|
| `mcpEntries` | `mcp` | `Record<id, ConfigMCPV1.Info \| {enabled}>` |
| `lspEntries` | `lsp` | `boolean \| Record<id, Entry>` |
| `formatterEntries` | `formatter` | `boolean \| Record<id, Entry>` |
| `referenceEntries` | `references` | `ConfigReference.Info` |

A Role whose `metadata` carries `{ "mcpEntries": { "myMcp": { ... } } }`
lifts to a top-level `mcp.myMcp` entry on render. Multiple Roles
with disjoint ids merge; collisions are last-write-wins per id. The
renderer skips (with `qWarning`) any sub-value that is not a JSON
object, leaving the ContractChecker to surface the load-time
InvalidError if the merge result is structurally invalid.

**Provider options (Phase C6-3):** `Role::metadata.providerOptions` is
a `Record<id, ConfigProviderV1.Info>` that lifts to top-level
`provider` AND the v2 `providers` 1:1 mirror (per D-1 — same shape
in both universes since `packages/core/src/v1/config/provider.ts:76`
and `packages/core/src/config/provider.ts:65` both use
`Record<id, ...Entry>`). Multiple Roles merge by id; collisions are
last-write-wins.

---

## 6. GUI / UX Implications (Detailed Specification)

**Final navigation:** **Lab Overview • Roles • Teams • Trials • Projects**

### 6.1 Semantic language rules (strict)

- **Role** = job description + system prompt. Edited **only** in the Roles view.
- **Specialist** = concrete filling of a Role (model binding + optional tiny prompt override).
- **Team** = reusable collection of Specialists with one or more primaries.
- **Trial** = recorded experiment of a Team on a project.

No "Template". No "Profile". New code, new tests, new docs must use the four-word vocabulary.

### 6.2 Per-view responsibilities

| View | Owns | Reads | Writes | Forbidden |
|---|---|---|---|---|
| Lab Overview | Dashboard, quick actions, recent Trials, project list | All entities, `models-cache.json` | none direct | editing Roles/Teams inline |
| Roles | CRUD on Roles, system prompt editing, permission editing | Roles only | Roles | editing Teams, selecting models |
| Teams | CRUD on Teams, Specialist binding, primaries, variants, duplicate-as-variant, Compare placeholder | Roles, Specialists, `models-cache.json` | Teams, Specialists, Trial seeds | editing Role system prompts (must go via Roles view) |
| Trials | Timeline, side-by-side comparison, "Promote winning Team" | Trials, Teams | Trials, Team metadata (on promote) | editing Roles/Specialists |
| Projects | Project scan, active Team per project, one-click switch + Trial auto-create | filesystem opencode configs | project metadata, new Trial | editing Teams beyond switching |

### 6.3 `TeamEditorWidget` behavior (workspace)

`Teams → TeamEditorWidget` is the heart of the app.

- Specialists table (rows): Primary checkbox (multi-select allowed), Specialist name, roleId + Role name, model display, cost/token badges from the live catalog.
- Add Specialist: `AddSpecialistDialog` opens — Role combo → embedded `ModelsBrowserWidget` in `pickerMode` → optional prompt override. On Accept, the new Specialist persists via `StorageManager` and the table refreshes.
- Row actions: Remove, Move Up, Move Down, **Duplicate as Variant** (creates a new Team whose Specialist set references the same Specialist ids; widget emits `teamVariantCreated(QString)` so Hosts switch to it).
- Compare action: open `TeamCompareDialog`. Semantics still being decided (see open question §8). For now: visual diff of rendered config.
- Apply Team… button on the editor footer (visible on `TeamEditorWidget`, not only on the Teams menu).
- Keyboard shortcuts (widget-scoped, `Qt::WidgetWithChildrenShortcut`): `Ctrl+N` Add, `Del` Remove, `Ctrl+Up/Down` Reorder, `Ctrl+D` Duplicate.

### 6.4 `ModelsBrowserWidget` modes

`ModelsBrowserWidget` is the only widget allowed to talk to the live provider catalog. Two modes:

- **Browse mode** (full table) — used in Lab Overview and Projects.
- **`pickerMode`** — embedded inside `AddSpecialistDialog`, single-select, cost columns visible, free-text filter, "ok" emits `modelAccepted(modelIdString)`.

Provider listing is discovered live (report §8.2 / §12 item 1) and is **not** mappable to `models-dev.md`. As `models-dev.md` ages out, this is enforced by a build-time check (Phase G, ROADMAP.md).

### 6.5 Provider subscription

`ProviderSubscriptionDialog` (kept in `src/ui/`) controls the active provider subset (drives `enabled_providers` / `disabled_providers`). It must:

- List providers against `GET /provider` and `opencode models`, *not* the static markdown.
- Surface `connected[]` so users see which providers have auth.

### 6.6 Cost & token data

Display wherever already present in the catalog cache. No extra fetches. The Capabilities column in the model table comes from `toolcall / reasoning / attachment / interleaved / modalities` (report §8.5).

### 6.7 Cross-view wiring still pending

| Signal | Source | Sink | Status |
|---|---|---|---|
| `teamApplied(QString)` | `TeamsWidget` | `ProjectsWidget`, `TrialsWidget` | wired (Phase D2) |
| `createRoleRequested` | `AddSpecialistDialog` | `MainWindow` (open `RolesWidget`) | **pending** (Phase F2) |
| `trialCreated(Trial)` | `ProjectsWidget` (one-click switch) | `TrialsWidget` | wired (Phase D2) |
| `teamVariantCreated(QString)` | `TeamEditorWidget` | `TeamsWidget` | wired (Phase E4) — **keep** |
| `applyRequestedForTeam(QString)` | `TeamEditorWidget` (footer button) | `MainWindow` → `ApplyTeamDialog` | **pending** (Phase F1) |

Phase F in ROADMAP.md is the work that closes the open cross-view signals.

---

## 7. Storage Migration Path

If a user has pre-Role data on disk:

1. Detect `schema-version.txt` missing or older than `v2.0-role-team-trial`.
2. Detect legacy folders `templates/`, `profiles/`, `default-profile.json`.
3. Auto-convert: each Template → one Role + N Specialists (one per agent with a model pinned). Each Profile → one Team whose Specialists reference those newly minted Specialists.
4. Remove the original files once the new view renders them.
5. Bump `schema-version.txt`.

`StorageManager` owns this path. Those migration files are the only source of the migration logic; once migration runs, they are purge candidates (Phase A3, ROADMAP.md).

---

## 8. Open Questions

- **Compare semantics** — diff rendered `opencode.json` between two Teams OR diff Trials (same team, two runs)? Resolved at Phase F3.
- **Recursion depth** — when a Specialist's Role declares "Team-backed" what is the max depth? v1 is "no recursion"; revisit post-MVP.
- **Prompt override granularity** — full per-specialist `system` string or just an "addition block" appended to the Role's prompt? Default is "addition block" in v1.
- **Snapshot of `renderedConfigSnapshot`** — store as string in the Trial JSON (easy to diff) or as a sidecar file (cleaner for large prompts)? Default is string; revisit at v1.1.

---

## 9. Purge & Archive Discipline (NEW)

This document, the GUI, the tests, and the data model all converge on the Role/Specialist/Team/Trial vocabulary and the live `$schema` contract. Anything that does not is **stale** and must be removed.

### 9.1 What gets purged

| Path | Reason | Trigger |
|---|---|---|
| `archive/` | Historical doc dump; will mislead agents | **Never** to be read again. Treated as deleted semantically. |
| `ProfilesWidget.*`, `ApplyProfileDialog.*`, `ProfileEditorDialog.*`, `TemplateEditorDialog.*`, `TemplatesWidget.*`, `ProfileCompareDialog.*` | Reference superseded Template/Profile widgets | Phase A3 |
| `Template.*`, `Profile.*` | Old model classes | Phase A3 |
| `OpencodeSchemaAdapter.*` | Replaced by **ContractChecker** (Phase G3) | Phase A3 |
| `tests/test_apply.cpp`, `tests/test_generation.cpp`, `tests/test_models.cpp`, `tests/test_opencode_schema_adapter.cpp`, `tests/test_profile_compare.cpp`, `tests/test_templates.cpp`, `tests/test_models_browser.cpp` | Reference the old model or contract | Phase A3 — **Removed 2026-06-28** (test-purge portion; the corresponding subjects were superseded by `test_apply_team`, `test_team_renderer`, `test_contract_checker`, F3's Compare dialog, and G1's live-catalog behavior) |
| `models-dev.md` (project root) | Live catalog must come from `opencode models` / `GET /provider` | Phase G1 (replaced with `docs/CATALOG-SOURCE.md` reference) |
| `v1spec.md` (project root) | Superseded by introspection report | Phase A3 |

### 9.2 How to purge

When a phase says "purge", the implementing agent MUST:

1. `git rm` the listed files in a single sweep.
2. Run `cmake --build build-dev --parallel` and `ctest --test-dir build-dev`. Build must succeed. Tests must pass; if a removed file had a test, the test is removed too.
3. Add the removal to ROADMAP.md Decision Log with the rationale "purge: <file> – superseded by <replacement>".

### 9.3 The "no resurrection" rule

Once a file is purged, its former purpose is **not** re-introduced under a different name without explicit sign-off in ROADMAP.md. If a future agent is tempted to copy a pattern from old code, the answer is no; port the *concept* through the ContractChecker or `TeamRenderer`, never through the old code.

---

## 10. Source-of-Truth Index

| Concern | Authoritative doc | Section(s) |
|---|---|---|
| `opencode.json` shape | OPENCODE-CONFIG-INTROSPECTION.md | §1, §4 |
| `$schema` literal | OPENCODE-CONFIG-INTROSPECTION.md | §3 |
| Permission keys | OPENCODE-CONFIG-INTROSPECTION.md | §6.1 |
| Agent schema | OPENCODE-CONFIG-INTROSPECTION.md | §7.1 |
| Provider discovery | OPENCODE-CONFIG-INTROSPECTION.md | §8 |
| One-line contract | OPENCODE-CONFIG-INTROSPECTION.md | §12.3 |
| Concrete improvements | OPENCODE-CONFIG-INTROSPECTION.md | §12.2 |
| Data model | this doc | §2 |
| Frontmatter | OPENCODE-CONFIG-INTROSPECTION.md | §7.2 |
| GUI rules | this doc | §6 |
| Build/test commands | `README.md` (this repo) | "Building", "Running Tests" |

If the report's line numbers drift (because opencode dev branch moved), update the report **then** update ROADMAP.md. Never update ROADMAP.md first.

---

*End of paradigm specification v0.2.*
