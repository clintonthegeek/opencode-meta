# CLAUDE.md — Agent Brief for opencode-meta-qt

**What this project is**  
A Qt companion for opencode that helps users design reusable agent Templates, discover models, compose/compare Profiles (applyable configurations), preview valid opencode config, and safely apply it globally or per project.

**Current reality (as of 2026-06-27)**  
- The app is under active development.  
- MVP pieces exist (model fetching works reasonably well).  
- Documentation has been reset to carry the baton for future agents.  
- This is not a completed product.

**Source of truth for roadmap and progress**  
Read `ROADMAP.md` first. It contains:
- Product direction and v1 scope.
- Milestone definitions (M0–M6 + future benchmarking path).
- Baton protocol: how every agent must update status, record decisions, and hand off.
- Current status and next-agent instructions.

**How to work here**  
1. Start with `ROADMAP.md`. Identify the single current milestone (only one should be In Progress).
2. Implement, then before handing off:
   - Update the milestone’s progress checklist.
   - Record any decisions with a short “Decision + Why” line in the decision log or the milestone section.
   - Set status if it changes.
   - Write concrete next-agent instructions.
3. Never mark a milestone Done unless its acceptance criteria are met.
4. Keep changes focused; prefer the schema adapter layer before large UI expansion (opencode schema has changed and will change again).

**Key concepts (short version)**  
- Template: reusable agent definition.  
- Profile: exact, validated, applyable configuration derived from a Template.  
- Model Collection: optional saved filter/list (convenience, not mandatory top-level v1 workflow).  
- Apply target: global `~/.opencode/config.json` or project-local override.  
- Schema adapter: isolates UI from opencode schema churn; must preserve unknown fields.

**Build / run notes**  
- Qt6/C++ CMake project.  
- Follow the project’s existing CMake and clangd setup conventions (see root instructions if you need to re-establish compile_commands.json).  
- Do not invent commands here; check existing build scripts or README for the current workflow.

**Handoff expectation**  
Every implementing agent is expected to leave ROADMAP.md updated so the next agent can pick up without confusion. This file (CLAUDE.md) is intentionally short; the roadmap is the living document.
