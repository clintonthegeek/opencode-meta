# Plan — Phase D: Stock Agent Fidelity (Option A)

**Date:** 2026-06-29
**Owner:** opencode-meta-qt
**Status:** `[ ]` not started
**Linked roadmap:** `opencode-meta-qt/ROADMAP.md` §5 "Future Work / Post-C"

---

## 1. Executive Summary & Rationale

The first-run seeding in `StorageManager::seedDefaultsIfNeeded`
(`src/storage/StorageManager.cpp:902`) currently creates a coarse,
opencode-meta-owned approximation of the three stock opencode agents
(`build`, `plan`, `general`) plus a "Starter Team" that wires `build`
and `plan`. The approximation drifts from stock in three ways that are
visibly expensive for users:

1. **Wrong `mode` on Plan.** Stock opencode's `plan` is a **primary**
   agent with edit tools *denied* but plans files *allowed*
   (`~/src/opencode/packages/opencode/src/agent/agent.ts:156`). Our
   seed sets `planRole.mode = Role::Mode::Subagent` (StorageManager.cpp:961).
   A user who selects "Plan" in the opencode TUI gets subagent semantics
   instead of the primary-mode plan tab they expect, and the
   `permission.merge` defaults that stock `plan` relies on
   (`plan_exit: "allow"` etc.) are entirely absent from the rendered
   config.
2. **No permission defaults.** Stock opencode seeds every native agent
   from `Permission.fromConfig({...})` with the full default ruleset
   (`*:allow`, `question:deny`, `plan_enter:deny`, `plan_exit:deny`,
   `doom_loop:ask`, `read:{*:allow, *.env:ask, *.env.*:ask, *.env.example:allow}`,
   `external_directory:{*:ask, $tmp/*:allow, $skill/*:allow, $reference/*:allow}`)
   (`~/src/opencode/packages/opencode/src/agent/agent.ts:119`).
   opencode-meta-qt seeds Roles whose `permissions: {}` is empty, so
   the rendered config has no permission block at all and the runtime
   silently gives the user an effectively unbounded tool surface.
3. **No Explore subagent, no hidden primaries.** Stock opencode ships
   seven native agents (`build`, `plan`, `general`, `explore`,
   `compaction`, `title`, `summary`) and tags each with `native: true`
   plus a `mode` (`compaction/title/summary` are hidden primaries).
   We seed only three and never surface the "native" concept. Users
   who don't manually enable Explore lose stock's headline codebase
   navigation agent.

**Option A (this plan):** keep the user's five-view shell and
`Role / Specialist / Team / Trial` schema, but make the
**default-on-empty-storage seed** match stock opencode's
`build/plan/general/explore` agents as closely as the opencode-meta-qt
data model allows — same names, same modes, same permission shapes,
same embedded `prompt.txt` strings, plus a `native: true` flag that
the editor treats as a read-only tag and the renderer surfaces into
emitted configs.

**Out of scope (Option B / C):**
- *Option B* — drop the `Role / Specialist / Team / Trial` shell
  entirely and emit `~/.config/opencode/opencode.json` directly from a
  per-user wizard.
- *Option C* — keep the opencode-meta shell but read stock's
  `~/.config/opencode/opencode.json` as the source of truth and import
  on first-run instead of duplicating.

Both B and C require breaking schema changes; Option A is purely
additive inside the existing model.

**Acceptance (informed by `~/src/opencode/packages/opencode/src/agent/agent.ts:140`):**

> A first-run install of opencode-meta-qt that ships an empty
> `~/.opencode-meta/`, after one click of "Apply", writes
> `opencode.json` whose `agent` block has at least `build`, `plan`,
> `general`, `explore` entries — all four passing `opencode debug
> config` with exit 0 and no `InvalidError`. The `build` entry
> advertises `mode: primary`; `plan` advertises `mode: primary` with
> `permission` containing `plan_exit: allow`, `task.general: deny`,
> and an `edit: { "*": "deny", "<plans-dir>/*": "allow" }` block;
> `general` and `explore` advertise `mode: subagent`. Each entry
> carries a `native: true` marker.

No behavior outside the seed changes in this plan (existing user
data is preserved verbatim — see §7).

---

## 2. Stock ↔ Current Seeded Comparison

Sources for the "Stock" column:
- Agent table + permissions: `~/src/opencode/packages/opencode/src/agent/agent.ts:140` (built-in agents).
- Prompt strings: `~/src/opencode/packages/opencode/src/agent/prompt/{explore,compaction,title,summary}.txt`.
- v1 schema surface (what `opencode debug config` validates): `~/src/opencode/packages/core/src/v1/config/agent.ts:12` and the report at `/home/clinton/dev/opencode-meta/docs/OPENCODE-CONFIG-INTROSPECTION.md` §4, §6, §7.

Sources for the "Currently seeded" column:
- Seeder: `src/storage/StorageManager.cpp:902–1039`.
- Team shell: `src/storage/StorageManager.cpp:985–1037`.

| Aspect | Stock opencode (`agent.ts:140`) | opencode-meta-qt `seedDefaultsIfNeeded` | Option A target |
|---|---|---|---|
| Agent set length | 7 natives (`build`, `plan`, `general`, `explore`, `compaction`, `title`, `summary`) | 3 (`build`, `plan`, `general`); hidden natives not seeded | 7 seeded (`build`, `plan`, `general`, `explore`, `compaction`, `title`, `summary`) |
| `build.mode` | `primary`, `native: true` (`agent.ts:153`) | `Role::Mode::Primary` (`StorageManager.cpp:944`) — matches | `primary` + `metadata.native = true` |
| `build.prompt` | none (uses `prompt/none`, system is empty) | "You are the primary Build agent in an OpenCode workspace…" (`StorageManager.cpp:939`) | empty string (delete the seeded prompt) |
| `build.permission` | `defaults + { question: "allow", plan_enter: "allow" }` | empty `Role::permissions: {}` | full stock rule set; document the merge in an inline comment |
| `plan.mode` | `primary`, `native: true` (`agent.ts:181`) | **`Role::Mode::Subagent`** (`StorageManager.cpp:961`) — **diverges** | `primary` + `metadata.native = true` |
| `plan.permission` | `defaults + { question: "allow", plan_exit: "allow", task: { general: "deny" }, edit: { "*": "deny", "<plans>/*.md": "allow" } }` | empty `Role::permissions: {}` | full stock rule set (`plan_exit: allow` + `task.general: deny` + `edit.{...}` block) |
| `general.mode` | `subagent`, `native: true` (`agent.ts:193`) | `Role::Mode::Subagent` (`StorageManager.cpp:978`) — matches | `subagent` + `metadata.native = true` |
| `general.permission` | `defaults + { todowrite: "deny" }` | empty | `*`: allow + `todowrite: deny` plus C1-2 `task: "allow"` injection (per `ROADMAP.md` §2 / D-3) |
| `explore.mode` | `subagent`, `native: true` (`agent.ts:216`) | **not seeded** | `subagent` + `metadata.native = true` |
| `explore.permission` | `*: deny` + grep/glob/list/bash/webfetch/websearch/read: allow + readonlyExternalDirectory (`agent.ts:198`) | — | full stock rule set verbatim |
| `explore.prompt` | `PROMPT_EXPLORE` (18 lines, `explore.txt`) | — | embed as `Role.systemPrompt` (or load at runtime from a Qt resource file `:/stock_prompts/explore.txt`) |
| `compaction / title / summary` | hidden primaries (`mode: primary`, `hidden: true`, `*: deny`, custom prompt) | not seeded | seeded as `metadata.native = true` + `metadata.hidden = true`; prompt via resource |
| `native` marker | runtime field on `Info` (`native?: boolean` at `core/src/v1/config/agent.ts:24` — not in `KNOWN_KEYS` agent.ts:43, so emits as `options.native`) | — | `Role::metadata.native` (boolean); renderer lifts into emitted `options.native` (per D-1 §6.2 Ruleset safety) |
| `hidden` | `hidden?: boolean` (`core/src/v1/config/agent.ts:27`) | — | `Role::metadata.hidden` (boolean); renderer lifts into emitted `hidden` |
| `color` | `Color` (hex `#RRGGBB` or named `primary/secondary/...`, `core/src/v1/config/agent.ts:7`) | — | `Role::metadata.color` (string); renderer lifts into emitted `color` |
| `temperature / topP` | `Finite` (`agent.ts:18–19`) | — | defer to D-2 (not required for parity) |
| `default_agent` | top-level `string` per `core/src/v1/config/config.ts:80` | — | `Team.metadata.default_agent` (string); renderer lifts as top-level `default_agent` (per D-1 1:1 mirror — shape is the same scalar in v1 and v2) |
| Starter Team binding | n/a (opencode runtime ships agents, not teams) | `Starter Team` with two Specialists (`build`, `plan`) bound to `anthropic/claude-sonnet-4-6` (`StorageManager.cpp:988`) | keep Starter Team as-is; add `metadata.default_agent = "build"`. Document that the binding to `anthropic/claude-sonnet-4-6` is deferred to D-2 (no model evolution in Option A) |

---

## 3. Files To Touch (exact paths)

### `~/dev/opencode-meta/opencode-meta-qt`

| Path | Reason |
|---|---|
| `src/storage/StorageManager.h` | update the `seedDefaultsIfNeeded` docstring reference (§7 reference to PARADIGM is wrong now) |
| `src/storage/StorageManager.cpp:902–1039` | rewrite all four `Role` seed blocks (build, plan, general, NEW explore) + add 3 hidden Roles (compaction, title, summary) + add `default_agent` to Starter Team. New `seedCompactionTitleSummaryHelpers()` private helper. |
| `src/storage/StorageManager.cpp:30–50` | bump the seeder central logging to include the new version banner (so a user with logs can tell which seed version is in place) |
| `src/storage/StorageManager.cpp:124` (header) | add `enum class SeedVersion { v0_legacyFiction, v1_stockFidelity }` and persist under new QSettings key `storage/seed_version` (default = `v1_stockFidelity`) |
| `src/models/Role.h` | **no new fields** — `metadata` is already `QJsonObject`; `Role::native / hidden` are *metadata* sub-keys (`{native:true}`, `{hidden:true}`). Keep slim. |
| `src/generation/TeamRenderer.cpp:148–271` | (1) in the per-agent loop, after `permissions` construction, lift `role.metadata.native/hidden/color` into per-agent `options.native`, `hidden`, `color` keys. (2) In the trailing top-level loop, lift `team.metadata.default_agent` into a top-level `default_agent` key (and v2 mirror per D-1 since shapes match). |
| `src/generation/ContractChecker.cpp` | accept the new top-level `default_agent` key (string) — currently `validate` may treat it as "unknown top-level" and emit a warning |
| `src/generation/ProviderCatalog.{h,cpp}` | **no change** — Option A does not change the catalog surface |
| `src/ui/RoleEditorDialog.h:147–212` | (1) add `QLabel *m_nativeBadge` next to the `idLabel`; (2) display (read-only) when the loaded Role has `metadata.native = true`; (3) make the `idEdit` and `nameEdit` `setReadOnly(true)` when native is true; (4) add a "Reset to stock permissions" `QPushButton` in Permissions tab next to the existing Reset button (objectName `roleEditor.resetStockPermissionsButton`) |
| `src/ui/RoleEditorDialog.cpp` (loadFromRole / applyToRole) | round-trip `metadata.native` and `metadata.hidden`. When `metadata.native == true`, lock the `id/name` fields and surface the badge. |
| `src/ui/RolesWidget.cpp/h` | (1) paint a "stock" badge on Role rows whose `metadata.native == true`. (2) Disable the Delete action in the context menu for native Roles (greyed out + tooltip "stock-defined Roles are read-only — clone instead") |
| `src/ui/TeamsWidget.cpp/h` | show a "default" badge next to the Starter Team card; ensure native team metadata renders a small footnote |
| `src/ui/TeamEditorWidget.cpp/h` | new field — a `QLineEdit` ("Default agent name", objectName `teamEditor.defaultAgentEdit`) on the Team header. `loadTeam` reads `metadata.default_agent`; `applyToTeam` writes back. Empty is allowed (= "no default override, opencode picks 'build'"). |
| `src/ui/SettingsDialog.h:6–34` | document the QSettings keys `settings/seed_stock_defaults` (bool, default `true`) and `settings/reset_seed_on_next_launch` (bool, default `false`) |
| `src/ui/SettingsDialog.cpp` | add two new rows in the buildUi() flow: (1) "Seed stock-aligned defaults on first run" `QCheckBox` (`settingsDialog.seedStockDefaultsCheckBox`); (2) "Reset storage to stock defaults on next launch" `QCheckBox` (`settingsDialog.resetSeedCheckBox`) — both with buddy labels and tooltips. |
| `src/storage/StorageManager.cpp::seedDefaultsIfNeeded` | read the two new settings keys and branch: `if (settings/seed_stock_defaults == false) return;` keeps the legacy fiction-seed path; `if (settings/reset_seed_on_next_launch == true) wipe roles + teams + re-seed` runs the D-3 reset path |
| `src/storage/ImportExportManager.cpp/h` | on `exportTeam`, exclude `metadata.native` from the export payload (or keep + add a `__wasNative: true` marker file-level field); on import, NEVER overwrite an existing native Role with a same-id imported Role (the importer sets the native flag, but a future user must "clone" to edit) |
| `src/ui/TeamsWidget.cpp/h` (clone action) | add `Action Clone` to the TeamsWidget context menu; clones are user-named, `metadata.native` is **unset** on the clone |
| `tests/seed/` | new `tests/CMakeLists.txt` entry wiring `test_seed_stock_fidelity` |
| `tests/test_seed_stock_fidelity.cpp` | new test target — slot list in §5 |
| `tests/test_starter_team_apply.cpp` | extend with **3 new slots** (per §5) |
| `tests/test_role_editor_dialog.cpp` | extend with **3 new slots** (per §5) |
| `tests/test_contract_checker.cpp` | extend with **2 new slots** (per §5) |
| `docs/PARADIGM.md` §2.1 (Role) | add `metadata.native`, `metadata.hidden`, `metadata.color` to the documented sub-key table; reference the Phase D plan |
| `docs/PARADIGM.md` §5.9 (Optional top-level surfaces) | add `default_agent` to the documented table |
| `opencode-meta-qt/ROADMAP.md` | link to this plan from §5 |

### `~/src/opencode` (read-only references for the implementer)

| Path | Reason |
|---|---|
| `~/src/opencode/packages/opencode/src/agent/agent.ts:140` | ground truth for the four native seed agents |
| `~/src/opencode/packages/opencode/src/agent/agent.ts:119` | ground truth for the `defaults` Permission ruleset |
| `~/src/opencode/packages/opencode/src/agent/prompt/explore.txt` | verbatim source for `Role.explore.systemPrompt` |
| `~/src/opencode/packages/opencode/src/agent/prompt/compaction.txt` | verbatim source for `Role.compaction.systemPrompt` |
| `~/src/opencode/packages/opencode/src/agent/prompt/title.txt` | verbatim source for `Role.title.systemPrompt` |
| `~/src/opencode/packages/opencode/src/agent/prompt/summary.txt` | verbatim source for `Role.summary.systemPrompt` |
| `~/src/opencode/packages/core/src/v1/config/agent.ts:12` | v1 known-keys surface for `native/hidden/color/default_agent` |
| `~/src/opencode/packages/core/src/v1/config/config.ts:80` | top-level `default_agent` shape (string scalar — v1 and v2 share it) |
| `~/src/opencode/packages/opencode/src/config/agent.ts:62` (the normalize rule) | `tools:{name:false} → permission:{name:"deny"}` — feeds the D-4 path our seed already relies on |

---

## 4. UX Requirements

### 4.1 `RoleEditorDialog`

**Hard requirements** (each is a manual-review checkpoint):

1. `m_nativeBadge` (objectName `roleEditor.nativeBadge`) — a yellow
   `QLabel` reading literally `native (stock-defined)`. Visible only
   when the loaded Role has `metadata.native == true`. Hidden
   otherwise (no leftover space).
2. `m_idLabel` and `m_nameEdit` are **read-only** when `native == true`.
   The id is stable; the name is the stock display string ("Build",
   "Plan", "General", "Explore", "Compaction", "Title", "Summary").
   Disabling `setReadOnly(true)` on these fields is acceptable but
   greyed-out visuals are mandated.
3. New `m_resetStockPermissionsButton` ("Reset to stock permissions",
   objectName `roleEditor.resetStockPermissionsButton`) — visible only
   when `native == true`. On click, sets the Permissions table to the
   embedded stock defaults for this Role. Existing Reset-to-defaults
   button is not affected.
4. `m_modeCombo` is **read-only** when `native == true` — the mode is
   part of the contract.
5. Tools tab: unchanged (Tools deprecation banner from C1-4 / D-4 stays
   in place; a native Role has no `tools` so the banner should not
   light up).
6. The "Save" / OK button stays enabled at all times — native Roles
   are editable but only within the boundaries above; saving a native
   Role preserves `metadata.native = true`.
7. **Tooltip mandatory:** The native badge carries the tooltip
   "This Role matches a stock opencode agent. The id, name, and mode
   are read-only. Clone to make a custom variant."

### 4.2 `SettingsDialog`

1. New section "Seeding" containing two rows:
   - **Seed stock-aligned defaults on first run** (`QCheckBox`,
     objectName `settingsDialog.seedStockDefaultsCheckBox`).
     Default checked. Persisted under
     `settings/seed_stock_defaults` (bool). Tooltip: "When the
     storage root is empty, seed it with the four stock opencode
     agents (Build, Plan, General, Explore). Uncheck to seed the
     legacy opencode-meta approximation instead."
   - **Reset storage to stock defaults on next launch** (`QCheckBox`,
     objectName `settingsDialog.resetSeedCheckBox`). Default unchecked.
     Persisted under `settings/reset_seed_on_next_launch` (bool).
     Tooltip: "Wipes the existing roles and teams JSON and re-runs
     the stock-aligned seed. Use this if you want to compare your
     current settings with stock."
2. The current "Storage root" row stays; the new section sits below
   it.
3. Validation flow unchanged — these are checkboxes so no path
   validation needed.

### 4.3 Role list views (`RolesWidget`, `TeamsWidget`, `TeamEditorWidget`)

1. `RolesWidget` rows whose `Role::metadata.native == true` show a
   `QIcon` "stock" badge (12 px) in the name column. Right-click
   `Delete` action is greyed out + tooltip "stock-defined Roles are
   read-only — Clone instead".
2. `RolesWidget` rows whose `Role::metadata.hidden == true` are
   hidden by default; a new filter chip "Show hidden stock agents"
   (objectName `rolesWidget.showHiddenCheckBox`) reveals them.
3. `TeamsWidget` shows a literal `default` badge next to whatever
   Team has `metadata.default_agent` set. The Starter Team gets the
   badge by default.
4. `TeamsWidget` adds a new right-click `Clone` action that produces
   a non-native Team with `parentTeamId` set to the cloned Team's id.
5. `TeamEditorWidget`: a new `QLineEdit` "Default agent name"
   (objectName `teamEditor.defaultAgentEdit`) sits below the version
   row. Empty allowed; placeholder text reads "build". Saving writes
   `team.metadata.default_agent` (string) and the renderer lifts it.

### 4.4 Status bar / validation

- When `seed_stock_defaults == false` AND a fresh launch empties the
  storage root, status bar surfaces a non-blocking warning: "Legacy
  opencode-meta seed selected — see Settings → Seeding".
- When `reset_seed_on_next_launch == true`, the first launch after a
  checkbox flip surfaces a modal acknowledgement: "Storage will be
  reset to stock defaults on next launch. Continue?" with Cancel /
  Reset Storage buttons.

---

## 5. Phased Work Breakdown (D1 – D4)

### Phase D1 — Data model + seed parity (no UI yet)

**Goal:** Rebuild the seed block so the on-disk Roles match stock
exactly, behind the existing early-out gate at `StorageManager.cpp:924`.

| ID | Task | Files / Functions | Tests | DoD |
|----|------|-------------------|-------|-----|
| D1-1 | Embed stock permission defaults as a private `QHash<QString,QJsonObject>` named `kStockDefaults` in an anonymous namespace at the top of `StorageManager.cpp`. Keys are `build`, `plan`, `general`, `explore`, `compaction`, `title`, `summary`. Values are `QJsonObject` mirroring `agent.ts:140–264` lining-by-lining. Add a unit test that fires `Permission.merge`-equivalent shape checks on each. | `src/storage/StorageManager.cpp` (top of file, new anon-ns) | `test_seed_stock_fidelity::stockDefaults_ForBuild_isCorrectShape`, `*_ForPlan_isCorrectShape`, `*_ForGeneral_isCorrectShape`, `*_ForExplore_isCorrectShape`, `*_ForCompaction_isCorrectShape`, `*_ForTitle_isCorrectShape`, `*_ForSummary_isCorrectShape` | Seven test slots green; any divergence from `agent.ts:140` is a documented exception with a `// differs from stock because: …` comment. |
| D1-2 | Add `SeedVersion` enum + `QSettings storage/seed_version` key. On startup, read the value. New seed writes `v1_stockFidelity`; legacy seed (the D-7 escape-hatch below) writes `v0_legacyFiction`. | `src/storage/StorageManager.h` (enum), `src/storage/StorageManager.cpp:124` | `test_seed_stock_fidelity::versionDefaultsTo_V1_StockFidelity` | Enum exists; QSettings round-trips; default is `v1_stockFidelity`. |
| D1-3 | Rewrite the four main block seeds (`build`, `plan`, `general`, `explore`) in `seedDefaultsIfNeeded` using `kStockDefaults`. Move `plan.mode` from `Subagent` → `Primary`. Set `role.metadata["native"] = true` on each. | `src/storage/StorageManager.cpp:933–982` (rewrite) | `test_seed_stock_fidelity::seededBuild_role_PermissionsMatchStockPlan`, `seededBuild_metadataNativeIsTrue`, `seededPlan_modeIsPrimary`, `seededGeneral_metadataNativeIsTrue`, `seededExplore_modeIsSubagent`, `seededExplore_permissionsRead_andGlob_andGrep_AreAllow` | All slots green; storage has 7 native Roles; the Plan Role is `Mode::Primary`. |
| D1-4 | Add three hidden primary seeds (`compaction`, `title`, `summary`). `metadata.native = true`, `metadata.hidden = true`. `systemPrompt` from embedded `prompt/*.txt`. Use `Qt::Q_INIT_RESOURCE` style load from a `:/stock_prompts/*.txt` Qt resource (resource files added to `opencode-meta-lib` CMake target; declare `opencode_meta_stock_prompts_init` in `CMakeLists.txt`). | new `resources/stock_prompts/{compaction,title,summary}.txt` (verbatim copies from `~/src/opencode/packages/opencode/src/agent/prompt/`), `src/CMakeLists.txt` resource block, `src/storage/StorageManager.cpp:982` (new block) | `test_seed_stock_fidelity::seededHidden_NativeAndHiddenFlagsAreTrue`, `seededCompaction_systemPromptMatchesStockTxt`, `seededTitle_systemPromptMatchesStockTxt`, `seededSummary_systemPromptMatchesStockTxt`, `seededRolesTotal_IsAtLeastSeven` | Storage has 7 native Roles after a fresh launch; each hidden Role's `systemPrompt` round-trips through `Role::toJson`/`fromJson` exactly. |
| D1-5 | New private helper `StorageManager::seedStarterTeam_v1()` called when `teamsEmpty`. Pull the model-id constant out into a `kStarterSpecialistModel = "anthropic/claude-sonnet-4-6"` constant at the top of the file. Add 4 Specialists to mirror stock's primaries + the explore subagent (`starter-build`, `starter-plan`, `starter-general`, `starter-explore`). Wire `team.metadata["default_agent"] = "starter-build"`. | `src/storage/StorageManager.cpp:985–1037` (rewrite) | `test_seed_stock_fidelity::seededStarterTeam_hasDefaultAgent_SetToBuild`, `seededStarterTeam_bindsExploreSpecialist`, `seededSpecialists_countIsAtLeastFour` | Three slots green; StarterTeam now contains 4 SpecialistBindings. |
| D1-6 | Smoke-trio is pinned to `scripts/ci_smoke_trio.sh` (C0-4 / ROADMAP.md §0 build-dir note). Extend the smoke-trio regex to include `test_seed_stock_fidelity` ONLY after D1 is closed, so D-phase work doesn't drag in the rebuilt seed during C-phase regression. Phase D-8 reversal in `ROADMAP.md` §1 is the explicit gate; explicit D-8 reversal in D5 below (D7 mirror in this plan) sets that. | `opencode-meta-qt/CLAUDE.md`, `opencode-meta-qt/scripts/ci_smoke_trio.sh` (regex update only after Phase D exit gate) | The script itself | Smoke trio (5+N green) still green on `build/`. |

**D1 Exit gate:** All D1 slots green; `scripts/ci_smoke_trio.sh`
green; `test_compliance_signoff` (C7-1 / ROADMAP.md §2) still green
because the seeded Team+Spec set is superset-compatible.

### Phase D2 — Renderer lifts + ContractChecker

**Goal:** The seed's `metadata.native/hidden/color/default_agent`
fields actually reach the emitted `opencode.json`.

| ID | Task | Files / Functions | Tests | DoD |
|----|------|-------------------|-------|-----|
| D2-1 | Per-agent loop in `TeamRenderer::render`: after `permissions` are written, inspect `role.metadata` for `native`, `hidden`, `color`. Lift `native → options.native`, `hidden → hidden` (top-level on agent — same key as v1 schema), `color → color`. Lift only when the underlying value is the schema-correct shape. Use a small `liftAgentStringMetadata(agentObj, role, key, schemaValidator)` helper to keep the lift predicates honest. | `src/generation/TeamRenderer.cpp:148–271` | `test_team_renderer::nativeHiddenColor_liftFromRoleMetadata` NEW: scene-with-Plan-Role-typed-as-primary produces an agent with `mode: primary`, `hidden: false`, `color: <role.color>`; scene-with-Compaction-Role-typed-as-hidden produces `mode: primary, hidden: true`. | Two slots green; emitted v1 keys match the v1 known-keys surface at `core/src/v1/config/agent.ts:43`. |
| D2-2 | Team-level trailing loop: lift `team.metadata.default_agent` → top-level `default_agent`. Emit a v2 mirror `defaultAgent` only if the v1 key is a string (per D-1 — symmetric safe scalars). | `src/generation/TeamRenderer.cpp:271` (new lift block) | `test_team_renderer::defaultAgent_liftFromTeamMetadata` NEW: a Team with `metadata.default_agent = "build"` produces a top-level `default_agent: "build"` AND `defaultAgent: "build"`. | One slot green; emitted keys match `config.ts:80` (v1) and the v2 mirror path. |
| D2-3 | `ContractChecker::validate`: accept the new `default_agent` (top-level string). Add the key to the v1 canonical top-level key set with an inline `// §4 + Phase D2-3` marker. | `src/generation/ContractChecker.cpp:validate` (canonical top-level key list edit) | `test_contract_checker::acceptsDefaultAgent_topLevelString` NEW: rendered fixture with `default_agent = "build"` validates with zero warnings; `test_contract_checker::rejectsDefaultAgent_nonString` NEW: `default_agent = []` produces 1 warning. | Two slots green; no regression in any existing slot. |
| D2-4 | `Phase C6-2` (D-2 mirror): extend the `metadata.<sub>Entries → top-level` lift block in `TeamRenderer::render` to recognize a new sub-key `metadata.defaultAgent`. Same merge semantics (last-write-wins across Teams). | `src/generation/TeamRenderer.cpp` (extend C6-1 loop) | `test_team_renderer::metadataLiftsDefaultAgentViaSubKey` NEW | Slot green. |

**D2 Exit gate:** All D2 slots green; existing C-phase slots still
green; `test_compliance_signoff` still green because the v2 sidecar
workaround (`stripV2Sidecar` in C0-3) already drops the new keys at
runtime.

### Phase D3 — UX surfaces

**Goal:** A user interacting with the seeded data sees the right
affordances — native badges, hidden filter, clone action, default-agent
editor line, Settings seeding toggles.

| ID | Task | Files / Functions | Tests | DoD |
|----|------|-------------------|-------|-----|
| D3-1 | `RoleEditorDialog`: native badge label + readonly id/name/mode + Reset-to-stock-permissions button + round-trip `metadata.native/hidden`. Implement UX §4.1 verbatim. | `src/ui/RoleEditorDialog.{h,cpp}` | `test_role_editor_dialog::nativeBadge_VisibleWhenRoleIsNative`, `nativeBadge_HiddenWhenRoleIsNotNative`, `idAndName_editsAreReadOnlyWhenNative`, `mode_comboIsReadOnlyWhenNative`, `resetStockPermissionsButton_invisibleWhenRoleIsNotNative` | 5 slots green; manual + screenshot of all four native Roles (build/plan/general/explore + hidden compaction/title/summary). |
| D3-2 | `RolesWidget`: stock badge column + Delete-disabled-on-native + Show-hidden filter chip + Clone action. | `src/ui/RolesWidget.{h,cpp}` | `test_roles_widget_widget::stockBadge_visibleForNativeRole`, `deleteAction_disabledForNativeRole`, `showHiddenCheckBox_revealsHiddenRoles`, `cloneAction_producesNonNativeClone` | 4 slots green. |
| D3-3 | `TeamsWidget`: default-agent badge + Clone action. | `src/ui/TeamsWidget.{h,cpp}` | `test_teams_widget_widget::defaultAgentBadge_visibleWhenSet`, `starterTeam_carriesDefaultAgentBadgeByDefault`, `cloneAction_producesNonNativeTeamCloneWithParentId` | 3 slots green. |
| D3-4 | `TeamEditorWidget`: new `QLineEdit` ("Default agent name", objectName `teamEditor.defaultAgentEdit`) — round-trips `team.metadata.default_agent`. | `src/ui/TeamEditorWidget.{h,cpp}` | `test_team_editor_widget::defaultAgentEdit_roundTripsThroughMetadata`, `defaultAgentEdit_emptyValueIsAllowed` | 2 slots green. |
| D3-5 | `SettingsDialog`: seeding section with two checkboxes per UX §4.2. | `src/ui/SettingsDialog.{h,cpp}` | `test_settings_dialog::seedStockDefaultsCheckBox_persists`, `resetSeedCheckBox_persists`, `seedStockDefaults_false_skipsSeedAtFreshLaunch`, `resetSeed_true_wipesAndReseedsAtNextLaunch` (the last slot is a `StorageManager`-wiring test, not strictly `SettingsDialog`) | 4 slots green; Settings::Values struct gains two bools. |
| D3-6 | `ImportExportManager::exportTeam`: skip `metadata.native` and `metadata.hidden` from the export payload (they are project-local metadata, not shareable). New `metadata.__wasNative` flag at the file level for round-trip clarity. | `src/storage/ImportExportManager.{cpp,h}` | `test_import_export::nativeMetadataStripped_onExport`, `wasNativeFlagSet_onExport`, `importDoesNotMarkImportedRolesAsNative` | 3 slots green. |
| D3-7 | Wire `SettingsDialog::Values.seedStockDefaults` and `.resetSeedOnNextLaunch` into `StorageManager::seedDefaultsIfNeeded`. The reset path: check the QSettings flag, wipe `roles/` + `teams/` json files, set `seed_version = v1_stockFidelity`, run the v1 seed. The legacy-fiction seed: only runs when `seedStockDefaults == false` AND storage is empty (D-7 escape-hatch). | `src/storage/StorageManager.cpp:902` (early-out branch) | `test_seed_stock_fidelity::seedStockDefaults_false_usesLegacyFiction`, `resetSeed_true_wipesAndRunsV1Seed` | 2 slots green; existing seed tests still pass under the v1 default. |

**D3 Exit gate:** All D3 slots green; `scripts/ci_smoke_trio.sh`
green; manual UX review of the four screens passes.

### Phase D4 — Compliance + migration

**Goal:** Prove the new seed passes `opencode debug config` and
prove the existing-user migration story is safe.

| ID | Task | Files / Functions | Tests | DoD |
|----|------|-------------------|-------|-----|
| D4-1 | Extend `test_compliance_signoff.cpp` with one new slot `everyNativeAgent_isAcceptedByRuntime` — walk the 7 native Roles through `applyTeamToProject`, shell `opencode debug config`, assert exit 0 + no `InvalidError`. Use the existing `stripV2Sidecar` workaround (C0-3 / D-2 mirror). | `tests/test_compliance_signoff.cpp` | The new slot | Slot green on dev box; QSKIP if no `opencode` binary; CI=1 turns QSKIP into a hard failure per C0-3 parity. |
| D4-2 | New `test_legacy_storage_unaffected_by_seed.cpp`: pre-populate a storage root with a non-empty `roles/` directory (a hand-crafted "legacy fiction" `build` Role whose `mode == Subagent`), call `seedDefaultsIfNeeded`, assert the legacy Role is NOT overwritten and `seed_version` is NOT bumped. This is the migration contract: existing user data is sacred. | `tests/test_legacy_storage_unaffected.cpp` NEW; `tests/CMakeLists.txt` registration | `test_legacy_storage_unaffected_by_seed::legacyBuild_Role_isNotOverwritten`, `legacyStorage_DoesNotBumpSeedVersion`, `legacyStorage_DoesNotAddNativeRoles` | 3 slots green. |
| D4-3 | New `test_seed_opt_out_path.cpp`: with `settings/seed_stock_defaults = false`, fresh-launch with empty storage still seeds, but writes `seed_version = v0_legacyFiction` and the Plan Role is `Mode::Subagent` and metadata.native is absent. This is the D-7 escape-hatch test. | `tests/test_seed_opt_out_path.cpp` NEW | `test_seed_opt_out_path::optOutSeeds_v0_legacyFiction`, `optOut_plan_isSubagent`, `optOut_role_notMarkedNative` | 3 slots green. |
| D4-4 | Update `scripts/ci_smoke_trio.sh` to include `test_seed_stock_fidelity` in the canonical regex with a clear "Phase D added" comment line. This is the **D-7 explicit reversal** of C5-1 — once D-phase is the active work, the seed tests belong in the trio. | `opencode-meta-qt/scripts/ci_smoke_trio.sh` (regex update with comment) | The script itself | Script green; CI consumes the new regex. |
| D4-5 | Update `ROADMAP.md` §5 with this plan linked and `[x]` for the four D phases with the smoke-trio counts inline. Update `Last updated: 2026-06-29 by @opencode (minimax-m3) — Phase D complete; Option A stock fidelity landed`. | `opencode-meta-qt/ROADMAP.md` | — | Document updated. |

**D4 Exit gate:** `scripts/ci_smoke_trio.sh` green on `build/`
(N+ext tests included); `test_compliance_signoff` green; legacy
+ opt-out paths green; ROADMAP.md §5 fully describes the phase as
closed.

---

## 6. Test Strategy & Compliance Impact

### Test Strategy

- **Unit (Phase D1):** `test_seed_stock_fidelity` — pure on-disk
  assertions on the seed output. No QSettings, no UI.
- **Renderer (Phase D2):** extend `test_team_renderer` with 4 new
  slots (native/hidden/color lifts, default-agent lift, sub-key lift,
  C6-2 mirror). Reuse existing fixture idiom (load fixture team →
  inspect emitted JSON).
- **Contract (Phase D2):** extend `test_contract_checker` with 2 new
  slots (accept / reject `default_agent`).
- **Widget (Phase D3):** extend `test_role_editor_dialog`,
  `test_team_editor_widget`, plus `test_roles_widget_widget` /
  `test_teams_widget_widget` (new tiny widget targets if needed).
  Plus 4 new `test_settings_dialog` slots.
- **Storage (Phase D4):** new `test_legacy_storage_unaffected`,
  `test_seed_opt_out_path` to lock the safety rails.
- **Compliance (Phase D4):** extend `test_compliance_signoff` with
  one slot. CI parity already handled by `tests/CMakeLists.txt`
  `FAIL_REGULAR_EXPRESSION` (C0-3).

### Compliance Impact

- **Plus:** emitted configs match stock opencode's defaults, so
  users who came to opencode-meta from stock don't experience the
  drift described in §1.
- **Plus:** `+1` to PARADIGM.md's documented surface
  (`metadata.native / metadata.hidden / metadata.color /
  metadata.default_agent`).
- **Plus:** `test_compliance_signoff` extends cleanly; the existing
  `stripV2Sidecar` workaround covers the new keys (lines that the
  1.17.x runtime flags — `default_agent` is **not** in that list
  because the runtime accepts it via `ConfigV1.Info`).
- **Neutral:** existing user data is preserved (D4-2 locks the
  contract).
- **Risk:** if stock opencode removes one of the seven native agents
  before we ship D1, our seed diverges. Mitigation: D1-1 reads from
  `agent.ts` on every plan read; the seed is a snapshot, so we will
  pin stock opencode commit SHA in `StorageManager.cpp` top-of-file
  comment and bump the seed when stock changes.

---

## 7. Migration / Backward-Compat Notes

1. **Existing user data is sacred.** `seedDefaultsIfNeeded`'s
   early-out at `StorageManager.cpp:924` (`if (!rolesEmpty &&
   !teamsEmpty) return;`) keeps the contract: a user who already has
   any roles or teams JSON files is NEVER re-seeded. D4-2 locks this
   in a test.
2. **Migration is opt-in.** A user with a fresh wish to compare can
   flip the Settings → Seeding → Reset Storage checkbox, click OK,
   and the next launch wipes roles + teams and re-seeds stock.
   Existing user JSON files when present are NOT affected.
3. **Legacy-fiction escape-hatch.** Setting `settings/seed_stock_defaults
   = false` flips the seeder to the v0 path (a `plan` Role as
   `Subagent`, no permission defaults, no native flag). This is for
   any rare user that prefers the older opencode-meta approximation.
4. **Forward-compatibility.** `metadata.native / hidden / color /
   default_agent` are arbitrary QJsonObject sub-keys. They round-trip
   through `Role::fromJson/toJson` because `metadata` is just a
   `QJsonObject`. Future code that ships a new "native" agent is a
   data-only change — no schema migration is needed.
5. **No team/Specialist migration needed.** Teams do not change shape
   in Option A; only the metadata adds one new sub-key.
6. **Pickle stability.** Stock opencode treats `native` as a v1
   `options`-side key (since it's not in `KNOWN_KEYS` at
   `core/src/v1/config/agent.ts:43`). Our emitted v1 file will land
   `options: { native: true, hidden: false }` for non-hidden native
   agents — which the runtime accepts via the `Record<string, Any>`
   at `agent.ts:30`. Verified by `test_compliance_signoff`.
7. **`$schema` URL unchanged.** Option A touches seed content and
   metadata sub-keys; the file-level `"$schema":
   "https://opencode.ai/config.json"` (C0 §12.3) is unaffected.

---

## 8. Open Decisions (added to `ROADMAP.md` §1 when this plan is
adopted)

| ID | Decision | Chosen default | Rationale | Rejected alternative |
|----|----------|----------------|-----------|----------------------|
| D-9 | **Seed-fidelity policy.** Today `seedDefaultsIfNeeded` writes a coarse approximation of stock opencode's agents. | **Option A: stock fidelity inside the opencode-meta shell.** First-run empty storage seeds the 7 native agents + Starter Team with permissions, modes, prompts, and native/hidden flags that mirror stock (`agent.ts:140`). Existing user data is untouched. | Lowest-risk path; users who came from stock don't experience top-level drift. Option A is purely additive inside the existing data model. | Option B (drop the meta shell — emit `~/.config/opencode/opencode.json` directly, schema-breaking). Option C (read stock's file as truth and import — also schema-breaking). |
| D-10 | **Native-vs-custom UI surface.** Today the editor has no signal for which Roles came from stock. | **`metadata.native = true` is the single signal.** Renderer lifts to `options.native`; editor badge paints; Delete is disabled. Clone action is the user's path to a variant. | One truth source, no second class of "stock" object. | Add a `Role::isNative` boolean field (a second source of truth — invites divergence). |
| D-11 | **Default-agent selection.** Today Teams don't carry a `default_agent`. | **`Team.metadata.default_agent` string.** Empty means "no override, opencode picks `build`". Renderer lifts to top-level `default_agent` (and `defaultAgent` v2 mirror per D-1). | Reuses the existing `metadata` field; no Team schema migration. | Add a `Team::defaultAgent` field (schema-changing). |
| D-12 | **Seed reset UX.** Today there's no way to swap a populated storage back to stock. | **Settings checkbox `settings/reset_seed_on_next_launch` wipes roles + teams at next launch and re-seeds.** User gets one modal acknowledgement. | Single launch = single wipe. Safe because the user explicitly opted in. | Auto-migrate existing roles to native defaults (silently rewrites user data — out per §7). |

These decisions join the existing D-1 … D-8 list in ROADMAP §1 if this
plan is accepted by a future session. Acceptance lines must follow the
"Accepted by session @opencode (minimax-m3) on YYYY-MM-DD: Affirm …
No amendment." pattern.

---

## 9. Acceptance Checklist (informational)

- [ ] All D1, D2, D3, D4 slots green.
- [ ] `scripts/ci_smoke_trio.sh` green with the seed tests included.
- [ ] `opencode debug config` against a fresh-launch, freshly-applied
      Starter Team file exits 0 with no `InvalidError`.
- [ ] No regression in any C-phase slot.
- [ ] PARADIGM.md §2.1 + §5.9 updated.
- [ ] ROADMAP.md §5 status reads `[x] done (session @opencode
      (minimax-m3), YYYY-MM-DD)`.
- [ ] `Last updated: 2026-06-29 by @opencode (minimax-m3) — Phase D
      complete; Option A stock fidelity landed` at ROADMAP.md bottom.
- [ ] No new file under `archive/` or `legacy/` (purge warnings kept).
