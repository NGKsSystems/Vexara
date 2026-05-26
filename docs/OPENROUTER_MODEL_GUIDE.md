# OpenRouter models in Vexara

Use **Settings → Agent Roles** to assign backends and models per role.

## Recommended layout (Aider + Ollama for code)

| Role | Backend | Model source |
|------|---------|----------------|
| **Orchestrator (Planner)** | OpenRouter HTTP | Free reasoning model first |
| **Supervisor (Review)** | OpenRouter HTTP | Same family as Planner |
| **Builder** | **Aider CLI** | `ollama/qwen2.5-coder:14b` (local) |

OpenRouter handles **planning and review** (language-only). **Do not** use OpenRouter for heavy coding if you already use Aider + Ollama.

## Model dropdown order

When Orchestrator or Supervisor uses **OpenRouter HTTP**, click **Refresh model lists**:

1. **FREE** models first (sorted by fit for plan/review)
2. Then **paid** models, cheapest combined $/M tokens first
3. Labels include: `FREE · plan/review · provider/model — display name`

## Good free picks (typical on OpenRouter)

| Task | Examples (IDs change — pick what Refresh shows as FREE) |
|------|-----------------------------------------------------------|
| Planner | `google/gemini-2.0-flash-exp:free`, `meta-llama/llama-3.3-70b-instruct:free`, `deepseek/deepseek-r1-distill-llama-70b:free` |
| Supervisor | Same as Planner (JSON + reasoning; no repo tools needed) |
| Builder | **Use Aider + Ollama** — not OpenRouter |

Avoid **coder**-tuned models (`*-coder*`) for Planner/Supervisor — they are tuned for code, not plan JSON.

## Setup steps

1. Add **OPENROUTER_API_KEY** in Settings (API keys section).
2. Set **Orchestrator** and **Supervisor** backend to **OpenRouter HTTP**.
3. Click **Refresh model lists** — pick a **FREE · plan/review** model for each.
4. Keep **Builder** on **Aider CLI** with your Ollama model.
5. Save, restart pipeline: **Clear queued** → **Resume queue** on Master if paused.

## API key

Keys are stored in Windows Credential Manager. Model list is fetched from `https://openrouter.ai/api/v1/models`.
