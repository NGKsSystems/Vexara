<!-- markdownlint-disable MD004 MD009 MD012 MD022 MD024 MD026 MD028 MD029 MD032 MD047 MD031 MD033 MD034 MD036 MD040 MD041 MD056 MD058 MD060 -->

# Vexara — Project Scope

## Product

Vexara is a native Qt (C++) code editor with a multi-agent orchestration layer. It is opinionated, verification-first, and JSON-driven for advanced configuration.

## Current delivery (Phase 0–1 + terminal Phase 2)

A **modular** codebase that builds with **NGKsGraph / DevFabEco** (no CMake/Ninja), compiles to a runnable desktop shell, and separates concerns into linkable libraries.

### Module map

| Module | Role |
|--------|------|
| `platform/CoreRuntime` | App identity, paths, `GlobalSettings` (terminal, models, grok_build, verification) |
| `editor/Explorer` | Project / file tree |
| `editor/CodeEditor` | Line-number editor surface |
| `editor/Workspace` | Tabbed documents, save, find |
| `editor/Highlighting` | Syntax highlighting |
| `editor/Search` | Find-in-file bar |
| `editor/Terminal` | ConPTY terminal + profiles |
| `editor/Shell` | Main window, docks, menus, agents panel, settings |
| `orchestration/Core` | Registry, Grok Build bridge, verification runner, orchestrator |
| `apps/Vexara` | Thin executable entry |

### Build

- `ngksgraph.toml` declares one `exe` and multiple `staticlib` targets with explicit `links`.
- Output: `build_graph/release/bin/Vexara.exe`.

### In scope (implemented)

- Global JSON config (`vexara.json`) and per-project `.vexara/project.json`.
- Editor: explorer, tabs (max 12), highlighting, save/find, open folder/file.
- Terminal: profiles + Windows ConPTY with line-bridge fallback.
- Agents panel: status, plan/pending, task run, approve/reject, verification run.
- External Grok Build CLI integration (user-configured command).
- Verification command runner (default project build script).

### Out of scope (current slice)

- In-process LLM HTTP clients (providers configured in JSON only).
- Phase 3 advanced agent communication views.
- Extension marketplace / uncontrolled plugins.

## Principles

- **Modularity always**: new features land in the smallest library that owns the concern; the app target stays thin.
- **Strong defaults**: minimal UI surface; advanced behavior via JSON.
- **Verification-first**: supervisor + verify command hooks; deeper enforcement in later roadmap phases.
