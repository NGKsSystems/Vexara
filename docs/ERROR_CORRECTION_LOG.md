<!-- markdownlint-disable MD004 MD009 MD012 MD022 MD024 MD026 MD028 MD029 MD032 MD047 MD031 MD033 MD034 MD036 MD040 MD041 MD056 MD058 MD060 -->

# Vexara — Error Correction Log

| Date | Issue | Correction |
|------|-------|------------|
| 2026-05-21 | `ngksgraph init` detected native console stub (no Qt) | Replaced with explicit Qt modular `ngksgraph.toml` and multi-target layout |
| 2026-05-21 | `ngksdevfabric build` without MSVC on PATH (`cl.exe` missing) | `tools/build_release.bat` calls `vcvars64.bat` before fabric build |
| 2026-05-21 | UI showed `â€"` instead of em dash | Replaced Unicode dashes in UI strings with ASCII `-`; added `/utf-8` to `ngksgraph.toml` cflags |
| 2026-05-21 | `TerminalPanel` undefined type when calling `panel()` | Include `TerminalPanel.h` in `MainWindow.cpp` when using dock wrapper |
| 2026-05-21 | Agents panel stale after orchestration updates | `AgentRegistry` emits `agentsChanged`; panel binds `Orchestrator` with signal wiring |
| 2026-05-21 | `VexaraOrchestration` MOC/link for `Q_OBJECT` types | Added `orchestration/Core/include/**/*.h` to `src_glob`; linked `VexaraCore` |
| 2026-05-21 | PowerShell ConPTY showed box chars and `[?25l` in paths | `QPlainTextEdit` does not render ANSI; strip VT sequences in `AnsiStrip.h` before display |
| 2026-05-21 | Garbled paths after strip-only approach | Replaced strip on ConPTY path with `VtParser` + `TerminalScreen` + `PlainTextTerminalRenderer` |
