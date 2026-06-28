# ROADMAP — opencode-meta-qt

**Purpose of this document**
This is the authoritative living roadmap and progress tracker. Future agents read this first, select the current milestone, implement, update status, record decisions with rationale, and hand off clearly.

**Product direction (v1 scope)**
A Qt companion for opencode that lets users:
- Design reusable agent **Templates** (structure, prompts, permissions, defaults).
- Discover and filter **Models** (via models.dev + local caches).
- Compose/compare **Profiles** (exact, applyable configurations derived from a template).
- Preview valid `opencode.json` output.
- Safely apply the result globally or per-project.

**Explicit v1 exclusions**
- Benchmarking / experiment runs / A/B testing harness. A future extension path is preserved (see Future section) but is out of v1.

**Core concepts**
- **Template**: reusable definition of an agent (prompts, permissions, model preferences, mode). Stored locally; exportable.
- **Profile**: concrete, validated configuration ready to apply. Derived from a Template + model assignments + overrides.
- **Model Collection** (optional): saved model filter/list to speed assignment. Not a mandatory top-level v1 workflow.
- **Apply target**: global `~/.opencode/config.json` or a project-local override.
- **Schema adapter layer** (required before major UI expansion): UI edits stable domain objects; an adapter emits current opencode schema, preserves unknown fields, and validates. This isolates future opencode schema changes from the UI.

**Status legend**
- Planned — not started.
- In Progress — active work.
- Blocked — needs external input or dependency.
- Done — acceptance criteria met and verified.

**Baton protocol (how every agent must behave)**
1. Read ROADMAP.md before starting work.
2. Identify the current milestone (only one should be In Progress).
3. Before handing off, update:
   - Progress checklist for the milestone.
   - Any new decisions with a short Decision + Why line.
   - Status change if appropriate.
   - Next agent instructions (concrete next action, files to inspect, known risks).
4. Never mark a milestone Done unless all acceptance criteria are met.
5. Keep this file as the single source of truth for status.

**Current status (updated by agents)**
- M0 Documentation reset and baton protocol: Done (this document created + CLAUDE.md updated).
- M1 OpenCode schema alignment and adapter layer: Done.
- M2 Model cache/browser hardening: Done.
- M3 Profile editor and model-combination workflow: Planned.
- M4 Template editor completion: Planned.
- M5 Project/global apply safety: Planned.
- M6 Tests, validation, docs, release polish: Planned.

---

## M0 — Documentation reset and baton protocol

**Status**: Done  
**Last updated**: 2026-06-27 (completed)

**Goal**  
Establish clear, welcoming, baton-carrying docs so future agents can orient quickly, choose work, track progress, justify decisions, and hand off cleanly.

**Scope**  
- Create ROADMAP.md (this file).
- Rewrite CLAUDE.md as the concise agent brief that points here.
- Supersede the stale plan.md.
- Lightly correct README.md to avoid overstating completeness.
- Define baton protocol and progress conventions.

**Acceptance criteria**
- ROADMAP.md exists and contains product direction, milestones, baton protocol, and decision log.
- CLAUDE.md is short, current, and points to ROADMAP.md.
- plan.md is marked superseded and points forward.
- README.md does not claim the app is complete.
- No application code changes were required for this milestone.

**Progress checklist**
- [x] ROADMAP.md created.
- [x] CLAUDE.md rewritten.
- [x] plan.md superseded.
- [x] README.md lightly corrected.
- [x] Baton protocol documented.

**Implementation notes / decisions**
- Decision: Keep a compact decision log inside ROADMAP.md rather than a separate DECISIONS.md for now. Why: keeps the single source of truth small and easy for agents to maintain.
- Decision: Do not rewrite root v1spec.md. Why: it is a historical baseline; the roadmap here is the living plan.

**Next agent instructions**
- M0 is complete. Begin M1. Inspect `src/models/Template.*`, `src/models/Profile.*`, and `src/models/ModelInfo.*` to understand the existing domain. Create `OpencodeSchemaAdapter` that can load a real `opencode.json`, map to internal models, allow edits, and write back while preserving unknown fields. Update `AgentDef::Mode` to support `All`. Record progress in this roadmap before next handoff.

---

## M1 — OpenCode schema alignment and adapter layer

**Status**: Done  
**Last updated**: 2026-06-27

**Goal**  
Create a stable schema adapter so the UI can evolve independently of opencode schema changes. The adapter must emit valid current `opencode.json`, preserve unknown fields, and validate.

**Scope**
- Define internal domain models for Template, Profile, PermissionRule, ModelRef.
- Implement read/write adapter for current opencode schema (top-level `agent`, `permission`, model/provider policies, mode values `primary`/`subagent`/`all`).
- Ensure unknown fields survive a read→edit→write cycle.
- Add basic validation that prevents emitting obviously invalid configs.

**Acceptance criteria**
- Adapter can load a realistic `opencode.json`, expose it as domain objects, allow edits, and write back an equivalent file.
- Unknown fields are preserved.
- Validation rejects clearly invalid combinations (e.g., contradictory permissions).
- UI code is not required to change yet; adapter is usable from tests or a thin CLI harness.

**Progress checklist**
- [x] Domain models defined (existing Template/AgentDef/Profile used as stable internal domain).
- [x] AgentDef::Mode enum extended with All; modeToString/modeFromString updated.
- [x] Adapter implemented and tested on sample configs (OpencodeSchemaAdapter.h + .cpp created; basic round-trip + validation implemented).
- [x] Unknown-field preservation implemented via _raw_opencode metadata.
- [x] Validation rules added (defaultAgent must be Primary/All and exist).
- [x] Decision log updated with scope/sequencing choices.

**Implementation notes / decisions**
- Decision: Treat the existing `Template`/`AgentDef` and `Profile` classes as the stable internal domain for v1. Create a separate `OpencodeSchemaAdapter` (or similar) that handles conversion to/from the current opencode.json schema. Why: keeps UI and business logic on stable models while isolating schema churn.
- Decision: The adapter must support round-tripping unknown fields at the top level and within agent/permission sections. Why: future-proofs against opencode adding new keys.
- Decision: Update `AgentDef::Mode` to include `All` to match current opencode (`primary`/`subagent`/`all`). Permission model will evolve from the simple enum toward a richer `permission` structure in the adapter layer. Why: aligns with documented opencode changes.

**Next agent instructions**
- M1 is complete (adapter + tests now cover realistic configs and nested permission/unknown-field preservation).
- Move on to M2 (model cache/browser hardening) or later milestones.

---

## M2 — Model cache/browser hardening

**Status**: Done  
**Last updated**: 2026-06-27

**Goal**  
Make model discovery reliable and fast so later profile work has a solid foundation.

**Scope**
- Harden models.dev cache + local provider caches.
- Improve search/filter UX for assigning models to templates/profiles.
- Optional: saved Model Collections as a convenience, not a mandatory top-level workflow.

**Acceptance criteria**
- Model list loads quickly even with many providers.
- Search/filter is responsive and accurate.
- Cache refresh works and handles errors gracefully.

**Progress checklist**
- [x] Normalized `ModelsCache` schema and ensured round-trip stability for model entries.
- [x] Hardened cache loading with a shared population helper used by both startup and refresh paths.
- [x] Implemented offline/error fallback so stale caches are used when the network or JSON response fails.
- [x] Verified search and filter behavior (text, provider, cost tier, context window, capabilities, subscriptions) via headless tests.
- [x] Persisted provider subscription preferences under a stable storage key with tests.

**Implementation notes / decisions**
- Decision: Keep a light-weight `ModelsCache` structure (`timestamp` + flat `models` map) rather than persisting the full provider tree from models.dev. Why: it keeps the on-disk schema small while still preserving raw model JSON for future use.
- Decision: On startup, only caches newer than 24 hours are treated as fresh; on network/JSON errors we still surface any existing cache regardless of age. Why: prioritizes reliability/offline usability without surprising automatic refreshes.
- Decision: Move `ModelsProxyModel` to a header-only helper so its filter behavior can be exercised directly in tests without introducing a separate UI library target. Why: minimal change that improves regression coverage for filters.

**Next agent instructions**
- M2 is complete and backed by unit tests. Proceed to M3 (Profile editor and model-combination workflow), using the existing models browser and cache as the source for model IDs and capabilities.

---

## M3 — Profile editor and model-combination workflow

**Status**: Done  
**Last updated**: 2026-06-27

**Goal**  
Enable users to compose and compare Profiles derived from Templates, assign models, and preview the resulting config.

**Scope**
- Profile editor UI.
- Model assignment and override flows.
- Side-by-side comparison of two Profiles.
- Preview pane showing generated `opencode.json` snippet.

**Acceptance criteria**
- User can create a Profile from a Template, assign models, and see a valid preview.
- Comparison view highlights differences.
- Apply is not yet wired; preview-only is acceptable for M3.

**Progress checklist**
- [x] Profile editor dialog allows selecting a base Template, assigning models per agent, and setting basic global overrides.
- [x] Profiles list widget loads/saves Profiles via StorageManager and shows a rendered opencode.json preview for the selected Profile.
- [x] Profile compare dialog added under Profiles, rendering two saved Profiles side by side using the same renderProfileToConfig path.
- [x] Lightweight top-level diff summary implemented for rendered configs and covered by headless Qt tests.

**Implementation notes / decisions**
- Decision: Keep comparison at the rendered-config layer using renderProfileToConfig so the compare view always reflects the exact opencode.json that preview/apply would emit. Why: avoids duplicating logic and reduces drift risk as the adapter evolves.
- Decision: Start with a top-level diff summary that flags which keys differ between two rendered configs, leaving deeper, line-level visual diffing as a future enhancement if needed. Why: smallest high-value compare surface that still clearly indicates differences.

**Next agent instructions**
- Treat M3 as complete for v1 scope. M4 can focus on expanding the Template editor and richer Template validation; comparison and preview flows should build on the existing renderProfileToConfig and OpencodeSchemaAdapter layers.

---

## M4 — Template editor completion

**Status**: Planned  
**Last updated**: 2026-06-27

**Goal**  
Complete the Template editor so users can define reusable agent definitions.

**Scope**
- Template structure editor (prompts, permissions, mode, defaults).
- Validation that a Template can produce valid Profiles.
- Import/export of Templates.

**Acceptance criteria**
- Users can create/edit/save Templates.
- Templates validate and can be used to generate Profiles.
- Basic import/export works.

**Progress checklist**
- [ ] ...

**Implementation notes / decisions**
- (To be filled by implementing agent)

**Next agent instructions**
- (To be filled by implementing agent)

---

## M5 — Project/global apply safety

**Status**: Done  
**Last updated**: 2026-06-28

**Goal**  
Let users safely apply a Profile to global config or a project override with clear previews and confirmations.

**Scope**
- Apply wizard with diff preview.
- Backup/restore of previous config.
- Project detection and per-project override support.
- Clear warnings about scope of changes.

**Acceptance criteria**
- Apply flow shows a clear diff and requires confirmation.
- Global and project-local targets are supported.
- Backup is created before writing.

**Progress checklist**
- [x] Global apply flow shows a diff (current vs rendered config) and requires confirmation.
- [x] Project apply flow shows a diff (current vs rendered config) and requires confirmation.
- [x] Global and project-local targets are supported via the Profiles and Projects tabs.
- [x] A timestamped backup is created next to any existing config file before writing.

**Implementation notes / decisions**
- Decision: Implemented a small `applyConfigWithBackup` helper in the core library that handles directory creation, timestamped `.bak` backups, and temp-file writes. Why: keeps backup behavior testable and shared by both global and project apply paths without pulling UI into tests.
- Decision: Added `ApplyProfileDialog` as a lightweight "wizard" that presents scope, warnings, a summary of JSON-level changes (when parseable), and a side-by-side text diff before any write occurs. Why: satisfies the roadmap’s safety requirements with minimal new UI surface area.
- Decision: Kept prompt-file copying as a best-effort step after a successful apply for both global (`~/.config/opencode/prompts`) and per-project (`<project>/prompts`) targets. Why: maintains existing behavior while making the potentially destructive config write itself safer.

**Next agent instructions**
- Treat M5 as complete for v1 scope. For M6, consider adding more end-to-end tests around the apply flows (including error cases) and small UX refinements (e.g., surfacing the last backup path) if needed, but the core safety guarantees (diff + confirmation + backup + project/global support) are in place.

---

## M6 — Tests, validation, docs, release polish

**Status**: Planned  
**Last updated**: 2026-06-27

**Goal**  
Ship a reliable v1 with tests, docs, and polish.

**Scope**
- Unit/integration tests for adapter and core workflows.
- End-to-end validation against real opencode configs.
- User and developer documentation.
- Packaging/installer polish.

**Acceptance criteria**
- Test coverage is meaningful for the adapter and profile flows.
- Docs are updated for v1 release.
- App builds and runs cleanly on supported platforms.

**Progress checklist**
- [ ] ...

**Implementation notes / decisions**
- (To be filled by implementing agent)

**Next agent instructions**
- (To be filled by implementing agent)

---

## Future — Benchmarking and experiment runs (post-v1)

**Status**: Planned (explicitly out of v1)  
**Last updated**: 2026-06-27

**Direction**
- Add BenchmarkRun concept: define a set of prompts + model combinations + scoring.
- Run harness that executes against opencode or provider APIs.
- Results storage, comparison, and visualization.
- The schema adapter and profile system should be designed so this layer can be added later without major rework.

**Acceptance criteria for future work**
- (Defined when M6 is complete and v1 is shipped)

---

## Decision log (compact)

- 2026-06-27: Created ROADMAP.md and reset docs. Decision: keep decision log inside ROADMAP.md for now. Why: single source of truth is easier to maintain.
- 2026-06-27: Confirmed v1 excludes benchmarking but preserves future path. Why: keeps scope realistic while allowing later expansion.
- 2026-06-27: Emphasized schema adapter before major UI expansion. Why: opencode schema has changed (agent top-level, permission model, mode values) and will likely change again; adapter isolates UI from churn.

---

## Handoff template (copy this block when handing off)

**Current milestone**: M? — <name>  
**Status**: <Planned|In Progress|Blocked|Done>  
**Last updated**: <date>  
**What was done this session**:
- ...
**Decisions made**:
- Decision: ...
  Why: ...
**Blockers / open questions**:
- ...
**Next concrete action for the next agent**:
- ...
**Files to inspect first**:
- ...
**Risks to watch**:
- ...
