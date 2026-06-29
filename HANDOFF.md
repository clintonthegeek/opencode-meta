# Session Handoff — opencode-meta-qt — 2026-06-28 → Compliance Roadmap

> Continuation note. The previous handoff (`HANDOFF-pre-role-team-trial-2026-06-28.md`,
> archived from the broken-fast-coder Stage 5 era) describes a *pre-*
> Role/Specialist/Team/Trial state whose model classes no longer match
> `src/models/*.h`. Treat it as historical context only.
>
> Two further contaminations are silently present in that file's narrative:
> (a) it talks about "Stage 5 widget damage" that has long since been repaired;
> (b) it assumes `QMap<QString,QString> specialists`, but `src/models/Team.h`
> now uses ordered `QList<Team::SpecialistBinding>`. A fresh agent should
> trust this handoff and `ROADMAP.md` first; the archive only for nostalgia.

---

## What We Were Working On

Producing a sequenced, executable pathway that gets opencode-meta-qt from
"works with hand-written exports" to "every emitted `opencode.json`
round-trips through `opencode debug config` against the live opencode
1.17.11 runtime AND keeps working as the runtime evolves to v2."

The deliverable is documentation: an archived `ROADMAP.md`, a freshly
written `ROADMAP.md` that enumerates Phase C0…C7 with per-milestone tests,
and this HANDOFF for a fresh agent to pick up the work.

## Current Status

| Artifact | Path | State |
|----------|------|-------|
| Archived pre-compliance roadmap | `docs/archive/ROADMAP-2026-06-28-pre-compliance-plan.md` | Created (verbatim copy of prior `ROADMAP.md`). |
| Archived pre-role-team-trial handoff | `HANDOFF-pre-role-team-trial-2026-06-28.md` | Created (verbatim copy of stale `HANDOFF-2026-06-28.md`). |
| Live `ROADMAP.md` | `ROADMAP.md` | Rewritten — Compliance Pathway with Phases C0…C7 and Decisions D-1…D-8. |
| Live `HANDOFF.md` (this file) | `HANDOFF.md` | New handoff per skill structure. |
| Source code | `src/**` | **Not touched** by this handoff session — purely docs. |

`ctest` last reported 16/16 green by the closing UX session
(2026-06-28, commit `055d86c` cluster). Specifically the trio
`test_team_renderer | test_apply_team | test_starter_team_apply`.
`test_contract_checker` is also green per the prior agent; we promote it
into the documented smoke-trio in ROADMAP § C0-4.

## Decisions Made (must be revisited before coding starts)

These are the explicit choices the new ROADMAP commits to. Fresh agent
should affirmatively accept or amend each **in writing** under the
corresponding `D-x` row before touching code.

- **D-1** Stratified v2 emission; do not 1:1-mirror v1 keys whose v2 shape
  is a Ruleset. Concretely: drop the top-level `permission → permissions`
  and `attachment → attachments` mirror arms in `TeamRenderer.cpp:209–228`.
- **D-2** Always pass `ProviderCatalog::instance()` into `apply_helpers::commit`
  from `applyTeamToProject`. Fail-loud if catalog is unloadable.
- **D-3** Auto-inject `task: "allow"` for `Role.mode ∈ {subagent, all}`
  unless the Role has an explicit `task` rule.
- **D-4** Stop emitting `tools:`; emit `permission:` only. Editor surfaces
  a non-blocking deprecation banner.
- **D-5** Add `Role::readOnly`. Renderer omits `edit`/`bash` for primary
  read-only roles (key drop, not `ask`).
- **D-6** Strip non-§6.1 permission keys at render time with a `qWarning`;
  don't lose data during editor session, don't ship invalid files at write.
- **D-7** New `tests/test_runtime_opencode_debug.cpp` running an integration
  skippable-greppable test (skip when no binary, fail CI on skip).
- **D-8** Defer `.md` generation to C5 (after C0–C4 land).

These eight decisions are the gates between "in flight" and "ready to start".

## Files Modified (this session only)

```
opencode-meta-qt/
├── HANDOFF.md                                         NEW
├── HANDOFF-pre-role-team-trial-2026-06-28.md          NEW (archive copy)
├── ROADMAP.md                                         REWRITTEN
└── docs/archive/
    └── ROADMAP-2026-06-28-pre-compliance-plan.md      NEW (archive copy)
```

No application code, no tests, no `src/` files touched. Verified with
`git status` (see "Verification" below).

## Problems Encountered

- **Naming collision with the broken Stage 5 handoff.** `HANDOFF-2026-06-28.md`
  existed at root with narrative for a state that no longer exists. Resolved
  by copying the content to `HANDOFF-pre-role-team-trial-2026-06-28.md` and
  flagging it at the top of `HANDOFF.md`.
- **Phase-letter vs `Cx-y` ID drift.** `docs/PARADIGM.md` and some inline
  code comments still reference the old Phase A…G letters (e.g. "Phase G3"
  for the pre-write validation gate). The new ROADMAP adopts `Cx-y` IDs and
  cross-references the letters under §3 of the new ROADMAP.
- **Build-dir convention drift.** `CLAUDE.md` calls for `build-dev/`, but
  the prior ROADMAP references `build/` and the prior agent's test invocation
  used `--test-dir build`. The new ROADMAP §0 publishes the corrected command
  and asks for a `scripts/ci_smoke_trio.sh` to lock it in.

## Next Steps (ordered)

A fresh agent picks up here.

1. **Read these, in order:**
   - `ROADMAP.md` (this repo) — full Decision log + sequencer.
   - `/home/clinton/dev/opencode-meta/docs/OPENCODE-CONFIG-INTROSPECTION.md` — report.
   - `docs/PARADIGM.md` — paradigm + binding contract (§5).
   - `CLAUDE.md` — operating rules (build-dev, smoke trio, no resurrection).
2. **Affirm or amend** the §1 decisions in `ROADMAP.md` (D-1 … D-8). Update
   the row before coding; do not silently revert.
3. **Claim C0-1** in `ROADMAP.md` (change `[ ]` to `[~]` + session + date).
   This is the single smallest behavioural fix on the path: route
   `StorageManager::applyTeamToProject` (line 768) through
   `apply_helpers::commit` instead of `applyConfigWithBackup`. Tests
   to add are listed in `ROADMAP.md` § C0-1.
4. **Run the smoke trio** after every meaningful change:
   ```bash
   cmake -S . -B build-dev
   cmake --build build-dev --parallel
   ctest --test-dir build-dev --output-on-failure \
         -R "test_team_renderer|test_apply_team|test_starter_team_apply|test_contract_checker"
   ```
5. **Do not start any later phase** until the C0 exit gate is green
   (`scripts/ci_smoke_trio.sh` + `test_runtime_opencode_debug.cpp` either
   run locally or skipped-but-failing in CI).
6. **For each milestone:** write the test first, watch it fail, make it
   pass, mark the row `[x]`, add a "Decision + Why" line if you made an
   unexpected call.
7. **When C7-1 lands**, you're done. Update the "Last updated" footer of
   `ROADMAP.md` and re-export `HANDOFF.md` with that final state.

## Build / Test State

- **Last reported build/test result (closing UX session, 2026-06-28):**
  `ctest` 16/16 green. Smoke trio referenced above was confirmed green by
  the prior assessment.
- **This session:** No code changed. No build/test rerun required by
  `CLAUDE.md` (the handoff skill and the user request explicitly say
  "non-destructive verification; maybe no build needed").
- **Mandated gate for next milestone:** `scripts/ci_smoke_trio.sh` should
  be added at C0-4 and CI should reject any skip of
  `test_runtime_opencode_debug.cpp` per D-7.

## Context That Matters (not in CLAUDE.md / code)

- **The pre-`Role/Specialist/Team/Trial` model classes are gone.** Don't
  search for `Template`, `Profile`, `OpencodeSchemaAdapter`. They do not
  exist in `src/`. `legacy/` has already been purged.
- **`test_starter_team_apply` writes to `/tmp/opencode-meta-qt-starter-project`.**
  It does NOT clean up. If you re-run it, expect that directory to still
  exist and contain a valid `opencode.json`. Don't be confused by it on CI.
- **`ContractChecker::validate` accepts a `ProviderCatalog*` overload.**
  Today no caller passes one; that's gap #5 from the prior assessment
  (which is what C0-1 + C0-2 + C2-2 collectively fix).
- **`opencode debug config` is the load-time gate.** It emits the
  `ConfigV1.Info` resolution of whatever file it finds at the project root.
  Its exit code is the source of truth for whether a config is valid. The
  prior 13-gap assessment explicitly notes this gate was not exercised in
  tests; ROADMAP § C0-3 + C7-1 are the fixes.
- **`Phase A…G` letter comments in `docs/PARADIGM.md` and source files**
  are a parallel taxonomy from an older planning cycle. The new ROADMAP's
  `Cx-y` IDs are the active ones; the letters are only referenced for
  historical continuity in ROADMAP §3.
- **v2 `permissions` is a Ruleset (array of `{action, resource, effect}`),**
  not a Record<string, Action>. See report §5 + §6.2. This is the
  reason D-1 says NOT to 1:1-mirror v1 `permission` → v2 `permissions`
  at the top level. Per-agent `permissions` (flattened) is a different
  story; the renderer already does that in `flattenPermissions()` and
  the per-agent emission in `TeamRenderer.cpp:181-186`.
- **`test_contract_checker` reads from `tests/test_contract_checker.cpp`
  lines 109-119 for the "illegal permission key" case.** That fixture
  uses `"rm"` as the bad key. If a future agent adds `"rm"` to the
  allow-list (it won't, but just in case) the test will go silently green
  for the wrong reason.

## Concrete Anchors (paste-in paths for fresh agent)

```
opencode-meta-qt/
├── ROADMAP.md                                 # this compliance roadmap
├── HANDOFF.md                                 # this handoff
├── CLAUDE.md                                  # project operating rules
├── docs/
│   ├── PARADIGM.md                            # §5 is binding
│   └── archive/
│       └── ROADMAP-2026-06-28-pre-compliance-plan.md
├── src/
│   ├── generation/
│   │   ├── TeamRenderer.cpp                   # line 209–228 v2 mirror block; must be re-shaped per D-1
│   │   ├── ContractChecker.cpp                # already implements D-1, D-2 structurally
│   │   └── ProviderCatalog.cpp                # already implements D-2 lazy-load
│   ├── apply_helpers.cpp                      # commit() is the gate (C0-1 + C0-2)
│   ├── storage/StorageManager.cpp             # line 768 — applyTeamToProject bypass
│   └── models/Role.h                          # no readOnly field yet (C3-1)
└── tests/
    ├── test_contract_checker.cpp              # covers $schema, top-level, agent, permission, model
    ├── test_apply_team.cpp                    # extends to illegal-config in C0-1
    ├── test_team_renderer.cpp                 # extends in C1 + C3
    └── test_runtime_opencode_debug.cpp        # NEW per C0-3
```

---

**Last updated: 2026-06-28 by @opencode (Grok 4.3) — compliance roadmap
committed; no source-code changes.**
