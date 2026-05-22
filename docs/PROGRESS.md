<!-- markdownlint-disable MD004 MD009 MD012 MD022 MD024 MD026 MD028 MD029 MD032 MD047 MD031 MD033 MD034 MD036 MD040 MD041 MD056 MD058 MD060 -->

# Vexara — Progress

## 2026-05-21 — Foundation scaffold

- Modular repository layout created (`platform/`, `editor/`, `orchestration/`, `apps/`).
- `ngksgraph.toml` configured for Qt 6 Widgets with multiple static libraries + `Vexara.exe`.
- Runnable shell: project tree, tabbed editor, agents panel bound to orchestrator.
- Build system: NGKsGraph / DevFabEco only.

## 2026-05-21 — Installed DevFabEco build

- `ngksdevfabric build` (release, target `Vexara`) via `tools/build_release.bat` with `vcvars64`.
- `windeployqt` runs from the graph plan; Qt DLLs land in `build_graph/release/bin/`.
- `assets/` copied to `build_graph/release/bin/assets/` on build; window icon loads from logo when present.

## 2026-05-21 — Phase 1 editor foundation

- `VexaraCore`: `GlobalSettings` (`vexara.json`) and `ProjectSettings` (`.vexara/project.json`).
- `VexaraEditorWorkspace`: save active/all tabs, dirty tab titles (`*`), find in file.
- `VexaraEditorSearch`: `FindBar` module (bottom dock).
- `VexaraEditorShell`: File/Edit menus, last project restore, project root from tree.
- VS Code-style **Open** menu: toolbar dropdown + File > Open Folder / Open File dialogs.
- `VexaraEditorHighlighting`: syntax highlighting for cpp, json, md, py, toml, markup.
- `VexaraEditorTerminal`: integrated terminal dock; View > Terminal (`Ctrl+` `).

## 2026-05-21 — Terminal profiles + ConPTY (Phase 2 terminal)

- `VexaraCore::TerminalSettings` in `vexara.json` (`terminal.default_profile`, `terminal.profiles`).
- Built-in defaults: cmd, PowerShell 7, Windows PowerShell, Git Bash (when present).
- `TerminalDock`: **+** menu (new terminal, profile list, select default profile).
- `WinConPty` + `TerminalPanel`: Windows ConPTY pseudo-terminal with line-bridge fallback.

## 2026-05-21 — Multi-agent + configuration (Build Plan Phase 1)

### Configuration (`vexara.json`)

- `models`: profiles (provider, model, api_key / api_key_env), `default_model`, per-agent `agent_assignments`.
- `grok_build`: `command`, `args` (supports `{prompt}`, `{cwd}`), `timeout_ms`.
- `verification`: `command` (default `tools/build_release.bat`), `timeout_ms`.

### Orchestration (`VexaraOrchestration`)

- `AgentRegistry` with live `agentsChanged` signals and plan/pending summaries.
- `GrokBuildBridge`: runs configured external Grok Build CLI via `QProcess`.
- `VerificationRunner`: runs verification command in project folder via `cmd /C`.
- `Orchestrator`: `submitTask`, `approvePendingChanges`, `rejectPendingChanges`, `runVerification`.

### UI

- **Agents** dock: agent list with state/model, plan + pending panes, Run Task / Approve / Reject / Verify.
- **Settings > Preferences**: Grok Build command/args, verification command; models overview (edit full profiles in JSON).

### Build Plan roadmap status

| Roadmap phase | Status |
|---------------|--------|
| Phase 0 Foundations | Done (architecture, modular layout, build) |
| Phase 1 Editor + basic multi-agent | Done (editor core, agents panel, Grok bridge hook, verification hook) |
| Phase 2 Verification layer | Foundation done (verify runner + supervisor status); deeper checklist enforcement later |
| Phase 3 Advanced agent views | Not started (role-based views, comms inspector) |
| Phase 4 Extensibility / docs | Partial (JSON-first config); user docs/onboarding later |

### Operator setup

1. Set `grok_build.command` (and optional `args`) in Settings or `%APPDATA%/.../vexara.json`.
2. Open a project folder; use Agents panel **Run Task** with a prompt.
3. After success, **Approve** or **Reject**; use **Verify** to run the configured build command.

## 2026-05-21 — Copy / paste

- Shared `TextContextMenu.h` (`editor/Common`): right-click Cut, Copy, Paste, Select All on editor, find bar, agents fields.
- **Edit** menu shortcuts (Ctrl+C/V/X/A) dispatch to the focused text widget.
- Terminal: copy from selection; paste into ConPTY session or line-bridge input; clipboard shortcuts pass through.

## 2026-05-21 — Production terminal emulator (Phases 0-6)

- **Architecture** documented in `editor/Terminal/ARCHITECTURE.md`.
- **Transport:** `WinConPty` + `PtyReaderThread` (background `ReadFile`, UI via signals).
- **Emulator:** `TerminalScreen` + `VtParser` (CSI SGR, cursor, erase, UTF-8).
- **View:** `PlainTextTerminalRenderer` maps screen buffer to `QPlainTextEdit` with ANSI colors.
- **Session:** `TerminalSession` wires transport, parser, and `screenUpdated` signal.
- **Input:** `TerminalInputEncoder` (arrows, Ctrl+C/D/Z, Home/End, Tab, etc.).
- **Resize:** debounced `ResizePseudoConsole` on widget resize.
- **Copy:** Ctrl+Shift+C; **Ctrl+C** goes to the shell.
- Line-bridge fallback retained with `AnsiStrip` for legacy `QProcess` mode.

### Remaining (future)

- Live LLM API clients (OpenAI, Anthropic, xAI) — config schema exists; HTTP layer not wired.
- Grok Build: depends on your installed CLI path and argument contract.
- Phase 3 multi-agent views, Phase 4 onboarding docs, plugin/skills registry.
