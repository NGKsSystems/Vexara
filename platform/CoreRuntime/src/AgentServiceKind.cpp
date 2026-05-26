#include "VexaraCore/AgentServiceKind.h"

namespace VexaraCore {

QString agentServiceKindLabel(AgentServiceKind kind)
{
    switch (kind) {
    case AgentServiceKind::GrokCli:
        return QStringLiteral("Grok Build CLI");
    case AgentServiceKind::OpenAiHttp:
        return QStringLiteral("OpenAI HTTP");
    case AgentServiceKind::OpenRouterHttp:
        return QStringLiteral("OpenRouter HTTP");
    case AgentServiceKind::OpenClawCli:
        return QStringLiteral("OpenClaw CLI");
    case AgentServiceKind::AiderCli:
        return QStringLiteral("Aider CLI");
    case AgentServiceKind::None:
        return QStringLiteral("None");
    }
    return QStringLiteral("None");
}

QString agentServiceKindSettingsLabel(AgentServiceKind kind)
{
    switch (kind) {
    case AgentServiceKind::GrokCli:
        return QStringLiteral("Grok Build CLI");
    case AgentServiceKind::OpenAiHttp:
        return QStringLiteral("OpenAI (HTTP)");
    case AgentServiceKind::OpenRouterHttp:
        return QStringLiteral("OpenRouter (HTTP)");
    case AgentServiceKind::OpenClawCli:
        return QStringLiteral("OpenClaw (Planner)");
    case AgentServiceKind::AiderCli:
        return QStringLiteral("Aider (Local Worker)");
    case AgentServiceKind::None:
        return QStringLiteral("None (disabled)");
    }
    return QStringLiteral("None (disabled)");
}

AgentServiceKind agentServiceKindFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("grok_cli") || normalized == QStringLiteral("grok")) {
        return AgentServiceKind::GrokCli;
    }
    if (normalized == QStringLiteral("openai_http") || normalized == QStringLiteral("openai")) {
        return AgentServiceKind::OpenAiHttp;
    }
    if (normalized == QStringLiteral("openrouter_http") || normalized == QStringLiteral("openrouter")) {
        return AgentServiceKind::OpenRouterHttp;
    }
    if (normalized == QStringLiteral("openclaw_cli") || normalized == QStringLiteral("openclaw")) {
        return AgentServiceKind::OpenClawCli;
    }
    if (normalized == QStringLiteral("aider_cli") || normalized == QStringLiteral("aider")) {
        return AgentServiceKind::AiderCli;
    }
    if (normalized == QStringLiteral("none") || normalized.isEmpty()) {
        return AgentServiceKind::None;
    }
    return AgentServiceKind::None;
}

QString agentServiceKindToString(AgentServiceKind kind)
{
    switch (kind) {
    case AgentServiceKind::GrokCli:
        return QStringLiteral("grok_cli");
    case AgentServiceKind::OpenAiHttp:
        return QStringLiteral("openai_http");
    case AgentServiceKind::OpenRouterHttp:
        return QStringLiteral("openrouter_http");
    case AgentServiceKind::OpenClawCli:
        return QStringLiteral("openclaw_cli");
    case AgentServiceKind::AiderCli:
        return QStringLiteral("aider_cli");
    case AgentServiceKind::None:
        return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

QVector<AgentServiceKind> assignableAgentServices()
{
    return {AgentServiceKind::None,
            AgentServiceKind::GrokCli,
            AgentServiceKind::OpenAiHttp,
            AgentServiceKind::OpenRouterHttp,
            AgentServiceKind::OpenClawCli,
            AgentServiceKind::AiderCli};
}

} // namespace VexaraCore
