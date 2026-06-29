# CLAUDE.md — Agent Brief for opencode-meta-qt

**What this project is**
A Qt6 companion for [opencode](https://opencode.ai) that lets users design reusable **Role/Specialist/Team/Trial** configurations, browse the live provider catalog, preview valid `opencode.json`, compare variants, and safely apply them globally or per-project.

## Read This First — In This Order

1. **`/home/clinton/dev/opencode-meta/docs/OPENCODE-CONFIG-INTROSPECTION.md`** — the authoritative source for every schema, line number, permission key, `$schema` URL, and provider discovery surface opencode-meta touches. **No `opencode.json`, agent `.md`, or schema test is valid without conforming to the rules in §1, §3, §4, §6, §7, §8, and §12.3.** If a future agent asks "what shape should this file take?" the answer is in that document, not here.
2. **`ROADMAP.md`** (this repo) — the living status, decision log, and the active milestone. Pick the single in-progress phase and execute it.
3. **`docs/PARADIGM.md`** (this repo) — the data model, GUI rules, and now the **Config Generation Contract** that ties everything back to the introspection report.

## Hard Rules (Read Before Touching Anything)

- **⚠️ PURGE WARNING — DO NOT read anything under `archive/`.** The `archive/` directory holds historical documents from earlier Template/Profile iterations. They are frozen. Reading them is a waste of context and will mislead you about the current model. Treat `archive/` as if it does not exist.
- **⚠️ LEGACY WARNING — do not import or copy from superseded adapter/model code.** Once a milestone explicitly says "purge superseded files" in ROADMAP.md, also remove them from `CMakeLists.txt` and any test lists.
- **Every generated `opencode.json` must satisfy the one-line rule in §12.3 of OPENCODE-CONFIG-INTROSPECTION.md.** No exceptions. If you cannot satisfy it, do not commit the code.
- **After every generation change, run** `opencode debug config` against the emitted file (or `tmpdir/opencode.json` for tests) and assert exit code 0 + no `InvalidError`. This is non-negotiable.
- **Never** use the words "Template" or "Profile" in new user-facing UI, new code, new tests, or new docs.

## Current Reality (as of 2026-06-28)

- The Role/Specialist/Team/Trial data model and controller layer are in place.
- The five-view MainWindow cutover (Lab Overview / Roles / Teams / Trials / Projects) is done.
- **Phase E is partially complete.** The Specialist editing UX works inside `TeamEditorWidget` and the apply trio (`test_team_renderer`, `test_apply_team`, `test_starter_team_apply`) passes. But several external flows still stub or placeholder:
  - `TeamsWidget` shows a placeholder "New Team" button and a non-functional Delete button.
  - The "Apply Team…" dialog is reachable from the Teams menu but not from `TeamEditorWidget`.
  - The Compare dialog is a friendly placeholder.
  - The `createRoleRequested` signal is not yet wired through `MainWindow` for inline role authoring from the Add Specialist flow.
  - Cross-view navigation beyond the Lab Overview quick actions is incomplete.
- **`opencode.meta` TeamRenderer does not yet follow §12.3.** It still emits hard-coded provider/model strings from the static `models-dev.md` snapshot instead of live discovery, emits no v2 sidecar keys, and does not run the `opencode debug config` validation gate. The fixes tracked in ROADMAP.md Phase G are binding.

## How to Work Here

1. Read `OPENCODE-CONFIG-INTROSPECTION.md` (§1, §3, §4, §6, §7, §8, §11, §12). Read `ROADMAP.md`. Pick the single **In Progress** milestone — only one should be In Progress at a time.
2. Before implementing, find every file the change will touch and grep them for the superseded terms (`Template`, `Profile`, `oldAdapter`, `OpencodeSchemaAdapter`). Anything that still depends on superseded code is a purge candidate.
3. Implement. Then before handoff:
   - Run the **smoke-test trio**: `ctest --test-dir build-dev --output-on-failure -R "test_team_renderer|test_apply_team|test_starter_team_apply"`.
   - Run **`opencode debug config`** against any newly-emitted `opencode.json` (or write a tempdir fixture) and capture the exit code + stderr into the milestone checklist.
   - Update the milestone's progress checklist and the Decision Log ("Decision + Why" line).
   - Purge any new dead code, stale tests, or superseded widgets created by your change.
4. Never mark a milestone **Done** unless both its UI acceptance criteria AND the `opencode debug config` validation pass.

## Key Concepts

- **Role** — stable job description + system prompt + permission profile. Edited **only** in the Roles view. Maps 1:1 to an `agent` entry in `opencode.json`.
- **Specialist** — concrete filling of a Role with a specific `provider/model`. Bound inside Teams. The atomic model-swap unit.
- **Team** — named, versionable collection of Specialists with one or more primaries. The primary unit users curate. Renders to `opencode.json.agent` (one entry per Specialist).
- **Trial** — recorded experiment: which Team was applied to which project, notes, 1–5 ratings, snapshot of the rendered config.
- **Schema adapter contract** — every emission *must* round-trip through `ConfigV1.Info` shape AND emit parallel v2 keys (`agents`, `permissions`, `providers`, `system`, `disabled`) so the migration bridge at `packages/core/src/v1/config/migrate.ts:35` stays happy.

## Build / Run / Test Discipline

**Run these after every meaningful change**, in this order:

```bash
# 1. Configure + build
cmake -S . -B build
cmake --build build --parallel

# 2. Run the Phase C0 smoke-trio script (apply-path correctness)
scripts/ci_smoke_trio.sh
#   – internally pins the canonical regex
#     test_team_renderer|test_apply_team|test_starter_team_apply|test_contract_checker
#   – defaults to build/; override with BUILD_DIR=build-dev for the preset path.
#   – exits non-zero on any missing target or any failing test.

# 3. Validate any emitted opencode.json against the live runtime
opencode debug config  # in a tempdir that contains the generated file
```

`.clangd` already points at `build-dev/compile_commands.json`. If clangd shows red squiggles, regenerate with `cmake -S . -B build-dev` first.

The full test list is in `tests/CMakeLists.txt`. The apply-path trio is the minimum bar; expand it whenever the milestone touches generation, storage, or apply. `scripts/ci_smoke_trio.sh` (Phase C0-4) is the authoritative runner for the regression bar — call it from CI instead of inlining the regex.

## Handoff Expectation

Every implementing agent must leave ROADMAP.md updated at handoff:

- Mark the milestone **Done** only after `ctest` plus `opencode debug config` are both green.
- Add a "Decision + Why" line to the Decision Log for any non-obvious choice.
- Add concrete next-agent instructions (the very last section).
- If you removed or staged any files for purge, list them so the next agent can run the final delete.

This file is intentionally short. ROADMAP.md is the living document; OPENCODE-CONFIG-INTROSPECTION.md is the schema contract.
