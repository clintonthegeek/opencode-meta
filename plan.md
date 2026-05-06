# Plan for OpenCode Meta Qt Tool

## Overview
This document outlines the development plan for a Qt6/C++ graphical tool for creating and deploying OpenCode configurations, based on the v1spec.md specification. The tool will provide a user-friendly GUI for managing templates, profiles, models, and projects. The existing Python TUI in opencode-meta-tui is considered a starting point for ideas but not for code reuse, as it's too difficult to use. Focus on a proper graphical interface first.

The project will be developed in `/home/clinton/dev/opencode-meta/opencode-meta-qt`. Git repo initialized for version control. Commits will be made frequently after each major step.

## High-Level Phases
1. **Project Setup (Phase 1)**
   - Set up CMakeLists.txt for Qt6/C++ project, inspired by PlanStan and QtHugo examples.
   - Create basic Qt application skeleton (MainWindow with mode selector).
   - Configure clangd and build system (use setup-clangd skill).
   - Ensure it builds and runs a empty window.
   - Commit: Basic project structure.

2. **Data Model Implementation (Phase 2)**
   - Implement JSON-based data models for Templates, Profiles, Models Cache, and Projects as per spec.
   - Use Qt's QJsonDocument, QJsonObject for serialization/deserialization.
   - Create classes: Template, Profile, ModelInfo, ProjectRecord.
   - Storage: Implement file I/O under ~/.opencode-meta/.
   - Commit: Data model classes and storage.

3. **UI Modes Implementation (Phase 3)**
   - **Templates Mode**: QListWidget for templates, editor dialog for agent definitions.
   - **Profiles Mode**: List of profiles, editor with template selection and model assignments, preview pane.
   - **Models Browser Mode**: QTableView with search/filter, fetch from models.dev/api.json using QNetworkAccessManager.
   - **Projects Mode**: List of projects (scan filesystem), apply profiles.
   - Top-level: QTabWidget or custom mode selector.
   - Actions: Buttons/menus for create/edit/delete/export/apply.
   - Commit per mode, e.g., "Implement Templates Mode UI".

4. **Integration and Features (Phase 4)**
   - Cross-mode navigation (e.g., from Profiles to Models).
   - Profile application: Generate opencode.json, handle merges/overwrites, copy prompts.
   - Model testing: API key verification.
   - Project detection: Scan for opencode.json or .opencode/.
   - Error handling, validation.
   - Commit: "Integrate profile application", "Add model fetch and test".

5. **Testing and Polish (Phase 5)**
   - Unit tests for data models and generation.
   - UI testing: Ensure intuitive flows.
   - Build and run tests after each commit.
   - Documentation: Update README, add user guide.
   - Commit: "Add tests", "Polish UI".

## Technologies
- Qt6: Core, Widgets, Network (for models.dev).
- C++20.
- JSON handling: Qt's built-in.
- No KDE dependencies; keep simple.
- Storage: QDir, QFile for ~/.opencode-meta/.
- Git: Commit often, with descriptive messages.

## Risks and Mitigations
- Complexity of UI: Start with MVP for one mode, expand.
- JSON schema compliance: Validate generated opencode.json against spec.
- API fetch: Handle network errors, cache models.

## Timeline
- Phase 1: Immediate.
- Phase 2: Next.
- Phases 3-5: Iterative, based on testing.

Updates will be made as development progresses.
