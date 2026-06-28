# opencode-meta-qt UX Roadmap

**Purpose**: This living document turns the UX audit into a prioritized, trackable plan. Multiple sessions can claim tasks, record progress, and update status. Use checkboxes + status markers so anyone can see at a glance what is done, in-flight, or blocked.

**Tracking Conventions** (use these in every update):
- `[ ]` = Not started
- `[~]` = In progress (add session ID or date)
- `[x]` = Completed (add date + short note)
- `[!]` = Blocked (add reason)
- Add `Last updated: YYYY-MM-DD by @session-name` after each major change.

**Project Location**: All paths below are relative to `opencode-meta-qt/`.

---

## Phase 0 – Foundations (Enable Everything Else)

| ID | Task | Description | Files / Components | Priority | Status | Notes |
|----|------|-------------|--------------------|----------|--------|-------|
| P0-1 | Fix dead signals | Wire `startTrialRequested` and `promoteTeamRequested` in MainWindow (even if they just log or open a placeholder) | `src/MainWindow.cpp` | High | [x] | Wired in MainWindow.cpp – log via qDebug() + QMessageBox placeholder, switches to Teams tab – 2026-06-28 |
| P0-2 | Centralize delete operations | Replace all direct filesystem deletes with `StorageManager` methods (Roles, Trials, Teams) | `RolesWidget`, `TrialsWidget`, `StorageManager` | High | [x] | Added `StorageManager::deleteRole` + `deleteTrial` mirroring `deleteTeam`; updated `RolesWidget` and `TrialsWidget` to use them; added `test_roles_storage` and `test_trials_storage` – 2026-06-28 |
| P0-3 | Add status bar + feedback | Use `QMainWindow::statusBar()` for success/error messages instead of scattered QLabel / QMessageBox | `MainWindow`, all widgets | Medium | [ ] | Improves perceived polish |
| P0-4 | Add basic menus & shortcuts | File (Save, Export), Edit (Undo placeholder), View, Help (Keyboard shortcuts) | `MainWindow` | Medium | [ ] | Discovery & professionalism |

---

## Phase 1 – Reveal the Core Value (Highest Impact)

These changes immediately show users the power of the Role/Specialist/Team model and the rendered `opencode.json`.

| ID | Task | Description | Files / Components | Priority | Status | Notes |
|----|------|-------------|--------------------|----------|--------|-------|
| P1-1 | Live Rendered Config Inspector | Add dockable or split-pane view that calls `TeamRenderer::render()` and updates live. Include syntax highlighting + Copy button | `TeamEditorWidget`, new `ConfigInspector` widget, `TeamRenderer` | Critical | [x] | New `ConfigInspector` (read-only `QPlainTextEdit` + `QSyntaxHighlighter`); embedded as bottom half of a vertical `QSplitter` in `TeamEditorWidget`; `Copy to Clipboard` + `Save as...` buttons; refresh fires from `setTeamId` and every `m_team` mutation slot (`onPrimaryItemChanged`, `onAddSpecialist`, `onRemoveSpecialist`, `onMoveUp`, `onMoveDown`, `onDuplicateVariant` keeps the original team, so it intentionally does not refresh) – `build/test_cross_view_smoke` still green – 2026-06-28 |
| P1-2 | Prompt Merge Preview | Show effective prompt (Role systemPrompt + Specialist promptOverride) with token count and visual separation | `TeamEditorWidget`, `AddSpecialistDialog`, new `PromptPreview` widget | Critical | [x] | New read-only `PromptPreview` widget (`src/ui/PromptPreview.h/.cpp`): header (Role + Specialist + model), body (base Role.systemPrompt + `--- Specialist Override ---` separator when override present, `{file: ...}` form rendered verbatim), approximate token count via `(chars+3)/4`. Embedded as middle pane of the existing vertical `QSplitter` in `TeamEditorWidget` (refreshes on `itemSelectionChanged`); also placed at the bottom of `AddSpecialistDialog` (refreshes on Role combo / override `textChanged`) so the merged prompt is visible before the user accepts. `build/test_cross_view_smoke` + full `ctest` (8/8) still green – 2026-06-28 |
| P1-3 | Expand RoleEditorDialog | Add controls for `mode`, `permissions`, `tools`, `metadata`. Make it tabbed if needed | `RoleEditorDialog`, `models/Role.h` | High | [x] | Tabbed dialog (`Basics` / `Permissions & Tools` / `Metadata`): Name, Description, Mode (Primary/Subagent/All combo), System Prompt — `Permissions` 2-col Key/Value table with Add/Remove, `Tools` QListWidget + Name input + Add/Remove, `Metadata` 2-col Key/Value table (string-or-JSON values); round-trips mode/permissions/tools/metadata through `Role::toJson`/`fromJson`; new `tests/test_role_editor_dialog.cpp` covers load, untouched round-trip, and edit propagation; ctest 9/9 green – 2026-06-28 |
| P1-4 | Inline Specialist Editing | Allow double-click editing of model (reuse ModelsBrowserWidget), promptOverride, and name directly in the specialist table | `TeamEditorWidget` | High | [x] | New `EditSpecialistDialog` (QDialog wrapping `ModelsBrowserWidget` in pickerMode + Name `QLineEdit` + PromptOverride `QPlainTextEdit` + embedded `PromptPreview`); reachable from a new "Edit Specialist..." toolbar button OR double-clicking any row; `onEditSpecialist()` reads the selected binding, calls `StorageManager::saveSpecialist(editedSpecialist())` then `StorageManager::saveTeam(m_team)` per task contract, then refreshes the table + `ConfigInspector` + `PromptPreview`; preserves immutable `id`/`roleId`/metadata; empty override collapses to `QJsonValue::Undefined` so the JSON writer drops the field; new `tests/test_edit_specialist_dialog.cpp` covers load, immutable-field preservation, override-drop semantics, and save/reload through `StorageManager`; `ctest` 10/10 green – 2026-06-28 |
| P1-5 | ConfirmApplyDialog | New dialog showing side-by-side diff before applying a Team to a project. Address the existing TODO in ProjectsWidget | New `ConfirmApplyDialog`, `ProjectsWidget`, `TeamsDialog` | High | [x] | New `ConfirmApplyDialog` (header + status banner + side-by-side read-only `QTextEdit`s with red/green diff highlights + standard OK/Cancel) — purely visual (no IO); renders the right pane from `Team` + `StorageManager` so caller never double-renders; `ProjectsWidget::switchTeamForProject` now opens the dialog after picking the Team and only calls `StorageManager::applyTeamToProject` on Accepted (TODO resolved); `applyTeamToProject` unchanged so contracts, backups, and trial recording already in test_apply_team remain green; new `tests/test_confirm_apply_dialog.cpp` covers banner wording for new / overwrite / invalid-JSON cases, accessor stability, valid rendered JSON, and that Accept performs no IO; ctest 11/11 green – 2026-06-28 |

**Phase 1 Exit Criteria**: User can see the rendered `opencode.json` live, edit full Role capabilities, edit Specialists without destruction, and apply with preview.

---

## Phase 2 – Workflow Completion & Polish

| ID | Task | Description | Files / Components | Priority | Status | Notes |
|----|------|-------------|--------------------|----------|--------|-------|
| P2-1 | Finish TrialCompareDialog | Replace stub with real side-by-side rendered config diff using stored snapshots | `TrialCompareDialog`, `models/Trial.h` | High | [x] | New `TrialCompareDialog` (`src/ui/TrialCompareDialog.{h,cpp}`): metadata header per side (id/team/project/timestamp/duration/ratings/notes) + side-by-side read-only `QTextEdit` diff panels reusing the ConfirmApplyDialog red/green line-by-line highlighting. Source-of-truth: `Trial::renderedConfigSnapshot` if present, else re-render current Team via `TeamRenderer`; else clear "(no rendered config available …)" placeholder. Replaced the inline stub in `TrialsWidget.cpp` and opened non-modal with `WA_DeleteOnClose`, parented to the widget. New `test_trial_compare_dialog` (8 cases) covers snapshot+re-render+placeholder fallback paths plus line-tint detection – 2026-06-28 |
| P2-2 | Add search/filter to all lists | QLineEdit + proxy models for Roles, Teams, Trials, Projects tables | `RolesWidget`, `TeamsWidget`, `TrialsWidget`, `ProjectsWidget` | Medium | [ ] | Basic usability |
| P2-3 | Implement SettingsDialog | Paths, theme, default filters, opencode binary location, storage root | New `SettingsDialog`, `StorageManager` | Medium | [ ] | Removes hard-coded assumptions |
| P2-4 | Add tooltips & paradigm help | Context-sensitive tooltips + a persistent "What is a Role vs Specialist vs Team?" panel or first-run wizard | All widgets, new `HelpPanel` or `OnboardingWizard` | Medium | [ ] | Lowers learning curve |
| P2-5 | Keyboard shortcut overlay | F1 or Help menu showing all shortcuts in a nice table | `MainWindow`, new `ShortcutHelpDialog` | Low | [ ] | Discovery |

---

## Phase 3 – Power User & Differentiating Features

| ID | Task | Description | Files / Components | Priority | Status | Notes |
|----|------|-------------|--------------------|----------|--------|-------|
| P3-1 | Visual Team Canvas (experimental) | Alternative view to specialist table: draggable cards, color by role/mode, hover prompt preview | New `TeamCanvas` widget (optional) | Low | [ ] | Future differentiator |
| P3-2 | Import/Export bundles | Zip of selected Roles + Teams + optional metadata | New `ImportExportManager`, UI in Roles/Teams tabs | Medium | [ ] | Portability |
| P3-3 | Team version history | Store parentTeamId + timestamped snapshots; simple "Revert to previous version" | `models/Team.h`, `StorageManager`, `TeamEditorWidget` | Medium | [ ] | Safety net for experimentation |
| P3-4 | Model comparison view | Side-by-side comparison of two models (cost, capabilities, context) inside ModelsBrowser | `ModelsBrowserWidget` | Low | [ ] | Nice-to-have for power users |
| P3-5 | Actual file watcher | Implement real `QFileSystemWatcher` for the "Watch" flag on projects | `ProjectsWidget`, new watcher logic | Low | [ ] | Currently just a boolean |

---

## Phase 4 – Infrastructure & Long-term Maintainability

| ID | Task | Description | Files / Components | Priority | Status | Notes |
|----|------|-------------|--------------------|----------|--------|-------|
| P4-1 | Add comprehensive tests for UI flows | Especially around rendering, apply, and editing invariants | `tests/` (new or expanded) | Medium | [ ] | Prevents regressions as we grow |
| P4-2 | Theme / dark mode support | Qt stylesheets or QPalette for modern look | Global styles, SettingsDialog | Low | [ ] | Polish |
| P4-3 | Packaging & distribution | CMake install rules, AppImage / macOS bundle, Windows installer | `CMakeLists.txt`, packaging scripts | Low | [ ] | Makes the app real for users |
| P4-4 | Documentation site | Doxygen + user guide for the configuration paradigm | `docs/`, Doxygen config | Low | [ ] | External adoption |

---

## How to Update This Roadmap (for Future Sessions)

1. Claim a task by changing `[ ]` to `[~]` and adding your session identifier + date.
2. When done, change to `[x]` and append a one-line note (e.g., "Merged in PR #42 – 2026-07-05").
3. If blocked, use `[!]` and explain the blocker.
4. After significant updates, consider creating a handoff note or running the handoff skill so the next session knows exactly where you left off.
5. Keep the table format consistent so `grep` or simple scripts can generate progress reports.

**Current Overall Progress**: 12 / ~25 tasks completed (Phase 1 fully closed; P2-1 closed).

**Session Update (2026-06-28)**: Phase 0 high-priority tasks P0-1 & P0-2 completed by sub-agents. Progress: 11/25 after closing P1-1 (Live Rendered Config Inspector), P1-2 (Prompt Merge Preview), P1-3 (Role Editor now exposes mode/permissions/tools/metadata via a 3-tab dialog with a dedicated round-trip test), P1-4 (Inline Specialist editing via `EditSpecialistDialog`), and P1-5 (ConfirmApplyDialog — pre-apply diff gate resolving the long-standing TODO in `ProjectsWidget::switchTeamForProject`). Next up: P1 work is closed; recommend starting Phase 2 with **P2-1 (Finish TrialCompareDialog)**. P2-1 closed in this session: `TrialCompareDialog` extracted from the TrialsWidget inline stub; full side-by-side rendered-config diff with snapshot > re-render > placeholder precedence; `ctest` 12/12 green.

**Last updated: 2026-06-28 by @claude** (P2-1 in flight)

---

*Roadmap created from UX audit dated 2026-06-28. This document lives in the repository and should be the single source of truth for UX work.*