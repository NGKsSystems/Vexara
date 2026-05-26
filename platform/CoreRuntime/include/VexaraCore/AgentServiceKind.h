#pragma once

#include <QVector>

#include <QString>

namespace VexaraCore {

enum class AgentServiceKind {
    None,
    GrokCli,
    OpenAiHttp,
    OpenRouterHttp,
    OpenClawCli,
    AiderCli,
};

QString agentServiceKindLabel(AgentServiceKind kind);
QString agentServiceKindSettingsLabel(AgentServiceKind kind);
AgentServiceKind agentServiceKindFromString(const QString& value);
QString agentServiceKindToString(AgentServiceKind kind);
QVector<AgentServiceKind> assignableAgentServices();

} // namespace VexaraCore
