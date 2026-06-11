# opencode-meta-qt — AI Context

## What This Project Is

A Qt6/C++ GUI tool for creating and deploying [OpenCode](https://opencode.ai) configurations.
It lets you define reusable **templates** (agent roles + permissions), compose **profiles**
(model assignments on top of a template), browse available models from models.dev, and
apply profiles to any project to generate a valid `opencode.json`.

Remote: `git@codeberg.org:clintonthegeek/opencode-meta-qt.git`  
Location on disk: `~/dev/opencode-meta/opencode-meta-qt/`

## Build

```bash
cmake -S . -B build-dev
cmake --build build-dev --parallel
ctest --test-dir build-dev
./build-dev/opencode-meta-qt
```

No KDE deps. Qt6 Core + Widgets + Network + Test only.

## Current Status (2026-06-11)

**All five phases of the plan are complete.**

- Phase 1 (project setup) ✓
- Phase 2 (data models) ✓
- Phase 3 (all four UI modes: Templates, Profiles, Models Browser, Projects) ✓
- Phase 4 (integration):
  - Profile application (per-project + global `~/.config/opencode/opencode.json`) ✓
  - Overwrite confirmation before writing ✓
  - Cross-mode navigation (Browse Models... button in Profiles → Models tab) ✓
  - Project detection/scanning ✓
  - Provider subscription management ✓
  - Test Connection per provider ✓
- Phase 5 (tests + docs):
  - Unit tests: 2 suites, 11 test cases, all passing ✓
  - README ✓
  - .gitignore fixed (build dirs excluded) ✓

The plan.md is complete. No open work items.

## Architecture

```
src/
  generation.h/.cpp       — renderProfileToConfig(Template, Profile) → QJsonObject
  models/                 — Template, Profile, AgentDef, ModelInfo, ProjectRecord
  storage/StorageManager  — JSON persistence under ~/.opencode-meta/
  ui/                     — one widget class per mode + editor dialogs
tests/
  test_models.cpp         — Template/Profile serialization round-trips
  test_generation.cpp     — opencode.json generation output correctness
```

## Key Design Decisions

- **`opencode-meta-lib`** static library (models + storage + generation) lets tests link
  without pulling in Qt Widgets/Network. The main app executable links both the lib and
  the UI-side deps.
- **renderProfileToConfig** is the single source of truth for JSON generation; both
  ProfilesWidget (global apply) and ProjectsWidget (per-project apply) call it.
- No KDE deps — pure Qt6. Storage is hand-rolled JSON under `~/.opencode-meta/`.

## Data Storage Layout

```
~/.opencode-meta/
  templates/<id>/template.json  (+ prompts/ subdirectory)
  profiles/<id>.json
  models-cache.json
  projects.json
  default-profile.json
```

## What a Fresh Session Could Work On

The plan is complete. Possible next directions (not committed to):

- **Improve profile editor**: inline model picker dialog (browse/select from cache
  without leaving the dialog) instead of the current "go to Models tab, look it up,
  come back" flow.
- **Export/import**: template export as a sharable JSON bundle.
- **Watch mode**: auto-reapply a profile when models-cache.json is refreshed.
- **Global instructions support**: the `global_overrides.instructions` array in the
  spec can reference files or URLs — not yet surfaced in the UI.
