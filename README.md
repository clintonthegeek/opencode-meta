# OpenCode Meta Qt

A Qt6/C++ GUI tool for creating and deploying [OpenCode](https://opencode.ai) configurations.

Manage reusable agent templates, model profiles, and project configurations from a clean graphical interface. Apply profiles to any project (or globally) to generate a valid `opencode.json` in seconds.

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

---

## Running Tests

```bash
ctest --test-dir build-dev --output-on-failure
```

Tests cover Template/Profile serialization round-trips and `opencode.json` generation correctness.

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
  test_models.cpp         — Template/Profile serialization round-trips
  test_generation.cpp     — opencode.json generation correctness
```
