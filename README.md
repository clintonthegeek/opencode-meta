# OpenCode Meta Qt

A Qt6/C++ GUI tool (under active development) for creating and deploying [OpenCode](https://opencode.ai) configurations.

Manage reusable agent templates, model profiles, and project configurations from a clean graphical interface. Apply profiles to any project (or globally) to generate a valid `opencode.json`.

> **Note**: This project is in active development. Features described below are planned or partially implemented; see `ROADMAP.md` for current status.

---

## Features

| Mode | What it does |
|------|-------------|
| **Templates** | Define agent roles, permissions, prompt content, and default agents. Export/duplicate templates. |
| **Profiles** | Assign concrete model IDs to each agent role in a template. Preview the generated `opencode.json` live. Apply globally or per-project. |
| **Models Browser** | Search and filter models from [models.dev](https://models.dev). Filter by provider, cost tier, context window, reasoning, and tool-use support. Test API key connectivity per provider. |
| **Projects** | Scan the filesystem for OpenCode projects, assign profiles, view diffs between the current config and a profile, and track watch state. |

Cross-mode navigation: the **Browse Models...** button in Profiles switches directly to the Models Browser so you can look up model IDs without losing your place.

---

## Prerequisites

- Qt 6.5 or later (Core, Widgets, Network, Test modules)
- CMake 3.19+
- C++17-capable compiler (GCC 11+, Clang 14+)

---

## Building

```bash
# Configure
cmake -S . -B build-dev

# Build
cmake --build build-dev --parallel

# Run
./build-dev/opencode-meta-qt
```

### GitHub Releases

When this repository is hosted on GitHub as `clintonthegeek/opencode-meta`, creating a GitHub Release will trigger the `Release (Linux)` workflow. That workflow:

- Installs Qt 6 on an Ubuntu runner
- Configures and builds a Release binary
- Packages `opencode-meta-qt` into `opencode-meta-linux-x86_64.tar.gz`
- Attaches that archive as an asset on the GitHub Release

---

## Running Tests

```bash
ctest --test-dir build-dev --output-on-failure
```

Tests cover Template/Profile serialization round-trips, schema adapter validation and unknown-field preservation, `opencode.json` generation, models browser filters/cache, and safe apply-with-backup helpers.

---

## Data Storage

All data is stored under `~/.opencode-meta/`:

```
~/.opencode-meta/
  templates/<id>/
    template.json
    prompts/          # optional prompt files
  profiles/<id>.json
  models-cache.json   # snapshot of models.dev/api.json
  projects.json
  default-profile.json
```

---

## Archive And Historical Docs

Older design documents that are no longer the active plan are kept under
`archive/` in this repository (for example `archive/plan.md`). Treat anything
under `archive/` as read-only historical reference; use this README and
`ROADMAP.md` for current behavior.

---

## Usage

### Templates Mode

1. Click **Create New** to define a template.
2. Add agent roles (name, mode, description, prompt, permissions).
3. Set a default agent.
4. Save. Templates are versioned JSON stored under `~/.opencode-meta/templates/`.

### Profiles Mode

1. Click **Create New** to create a profile.
2. Select a base template; model assignment rows appear for each agent.
3. Enter the exact model ID (e.g. `anthropic/claude-sonnet-4-6`) for each role.
   Use **Browse Models...** to switch to the Models Browser and look up IDs.
4. The preview pane shows the generated `opencode.json` live.
5. Click **Apply (Global)** to write `~/.config/opencode/opencode.json`.

### Models Browser

1. Click **Fetch Models** to pull the latest list from models.dev.
2. Filter by provider, cost tier, context window, reasoning, or tool-use support.
3. Use **Manage Subscriptions** to restrict the list to your active providers.
4. Select a provider and click **Test Connection** to verify API key access.

### Projects Mode

1. Click **Scan** to find projects (up to 3 levels deep) containing `opencode.json` or `.opencode/`.
2. Select a project and click **Apply Profile** to write the rendered `opencode.json`.
   An overwrite confirmation is shown if the file already exists.
3. Click **View Diffs** to compare the current file against any profile before applying.

---

## Architecture

```
src/
  generation.h/.cpp       — renderProfileToConfig(): Template + Profile → QJsonObject
  models/                 — Template, Profile, AgentDef, ModelInfo, ProjectRecord
  storage/StorageManager  — JSON persistence under ~/.opencode-meta/
  ui/                     — Qt widget classes (one per mode)
tests/
  test_cross_view_smoke.cpp   — F5 cross-view end-to-end walk (Lab → Teams → Apply → Trials + restart)
  test_team_renderer.cpp      — TeamRenderer v1+v2 emission (Phase G5)
  test_apply_team.cpp         — Apply Team path with backup (Phase E apply trio)
  test_starter_team_apply.cpp — Seeded starter-team apply round-trip
  test_teams_storage.cpp      — Teams CRUD on-disk round-trip (Phase F4)
  test_contract_checker.cpp   — ContractChecker validation gate (Phase G3)
```

Legacy Template/Profile-era tests (`test_apply.cpp`, `test_generation.cpp`,
`test_models.cpp`, `test_opencode_schema_adapter.cpp`, `test_profile_compare.cpp`,
`test_templates.cpp`, `test_models_browser.cpp`) were removed in Phase A3; their
subjects were superseded by the Role/Specialist/Team/Trial world.
