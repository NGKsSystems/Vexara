# Grok Build CLI — setup for Vexara

Vexara **Run Task** launches xAI’s official **Grok Build** coding agent as an external program. You install that CLI once, point Vexara at `grok.exe`, then describe tasks in the Agents panel.

## What you need

1. **xAI Grok Build CLI** (terminal agent from xAI, not a separate third-party tool).
2. **Access** — early beta is aimed at **SuperGrok Heavy** subscribers; sign-in uses your xAI account or an API key.
3. **API key (recommended for Vexara)** — set `XAI_API_KEY` or paste a key under **Settings → Preferences → API keys** so headless runs do not depend on a browser login.

Official overview: [xAI Docs — Grok Build](https://docs.x.ai/build/overview)  
Product news: [Introducing Grok Build](https://x.ai/news/grok-build-cli)

## Install on Windows

**PowerShell (recommended):**

```powershell
irm https://x.ai/cli/install.ps1 | iex
```

**Git Bash / WSL:**

```bash
curl -fsSL https://x.ai/cli/install.sh | bash
```

Default location after install:

```text
%USERPROFILE%\.grok\bin\grok.exe
```

Ensure that folder is on your `PATH`, or browse to the `.exe` in Vexara Settings.

## Verify it works (outside Vexara)

Open a terminal in your project folder:

```powershell
cd C:\Users\suppo\Desktop\NGKsSystems\Vexara
$env:XAI_API_KEY = "xai-..."   # if not already set
grok -p "List the top-level folders in this repo"
```

If that runs and prints output, the CLI is ready for Vexara.

Interactive mode (optional): run `grok` with no args for the full TUI.

## Configure Vexara

1. **Settings → Preferences (Grok Build & API keys)** or Agents **Setup...**
2. **Grok Build → Executable:**  
   `C:\Users\<you>\.grok\bin\grok.exe` (or use **Browse...**)
3. **Arguments:** (default Vexara suggests after install)

   ```text
   -p {prompt}
   ```

   - `{prompt}` — full composed prompt (see below), not just one line

Vexara wraps your short task in a **Grok Build system prompt** before calling the CLI. Default template includes project path, detected project type, open file, and editor selection. Placeholders:

| Token | Source |
|-------|--------|
| `{prompt}` / `{task}` / `{user_task}` | Your Agents panel task text |
| `{current_project_path}` / `{cwd}` | Open project folder |
| `{detected_type}` | Heuristic (e.g. NGKsGraph, CMake, Node) |
| `{current_file_path}` | Active editor tab path, or `(none)` |
| `{selected_text}` | Editor selection, or `none` |

Override the template in `vexara.json` → `grok_build.prompt_template` (optional).

## Role-based backends

**Settings → Preferences → Agent Roles** is the primary way to assign backends. Changes are saved to `agent_execution.role_backends` in `vexara.json`. Legacy `run_task.backend` is migrated into `role_backends.builder` on load.

Each agent role can use a different service:

```json
"agent_execution": {
  "role_backends": {
    "orchestrator": "none",
    "builder": "grok_cli",
    "supervisor": "none"
  },
  "openclaw": {
    "command": "",
    "agent_id": "main",
    "args": ["agent", "--agent", "{agent_id}", "--local", "--message", "{prompt}", "--json"]
  }
}
```

| Service | Config value | Typical role |
|---------|--------------|--------------|
| Grok Build CLI | `grok_cli` | Builder |
| OpenAI HTTP | `openai_http` | Builder, Supervisor |
| OpenRouter HTTP | `openrouter_http` | Builder, Supervisor |
| OpenClaw CLI | `openclaw_cli` | Orchestrator, Supervisor |
| Disabled | `none` | Any |

Example: Orchestrator plans via OpenClaw, Builder codes via Grok:

```json
"role_backends": {
  "orchestrator": "openclaw_cli",
  "builder": "grok_cli",
  "supervisor": "openai_http"
}
```

HTTP roles need a matching profile in `models.agent_assignments` (e.g. `supervisor-1` → `openai-default`) and an API key in Settings or env (`OPENROUTER_API_KEY`, etc.).

## API key security

- Keys entered in **Settings** are stored in **Windows Credential Manager** (per-user), not in `vexara.json`.
- On first launch after an upgrade, any legacy `api_key` fields in `vexara.json` are moved into Credential Manager and removed from the file.
- Environment variables (`XAI_API_KEY`, etc.) are never written to disk by Vexara.

4. **Save**, open a project folder, enter a task, click **Run Task**.

Example `vexara.json` fragment:

```json
"grok_build": {
  "command": "C:/Users/you/.grok/bin/grok.exe",
  "args": ["-p", "{prompt}"],
  "timeout_ms": 600000
}
```

## If you do not have Grok Build yet

1. Check [x.ai](https://x.ai) / SuperGrok Heavy eligibility for the CLI beta.
2. Create an xAI API key if needed: [console.x.ai](https://console.x.ai).
3. Run the install command above when access is available.
4. Until then, Vexara’s **Run Task** stays disabled; you can still use the editor, terminal, and (optionally) API keys for direct model calls in a future build.

## Not the same as other “grok-cli” packages

Community npm tools (e.g. `@vibe-kit/grok-cli`) are **different** projects. Vexara expects the **official** binary from `https://x.ai/cli/install.ps1` unless you deliberately point **Run Task** at another executable that accepts your args.

## Troubleshooting

| Problem | What to try |
|--------|-------------|
| `grok` not recognized | Add `%USERPROFILE%\.grok\bin` to PATH or use the full path in Settings |
| Auth / login errors | Set `XAI_API_KEY` in environment or in Vexara API keys |
| Run Task starts then exits | Run the same command manually in the project folder; check stderr in the Grok Build output pane |
| Executable locked on rebuild | Close `Vexara.exe` before running `tools\build_release.bat` |
