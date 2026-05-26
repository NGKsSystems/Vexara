#include "VexaraOrchestration/AgentRoleIds.h"

#include "VexaraCore/AgentExecutionSettings.h"

namespace VexaraOrchestration {

QString agentIdForRole(AgentRole role)
{
    switch (role) {
    case AgentRole::Orchestrator:
        return QStringLiteral("orchestrator-1");
    case AgentRole::Builder:
        return QStringLiteral("builder-1");
    case AgentRole::Supervisor:
        return QStringLiteral("supervisor-1");
    }
    return QString();
}

QString roleKeyForRole(AgentRole role)
{
    switch (role) {
    case AgentRole::Orchestrator:
        return VexaraCore::AgentExecutionSettings::roleKeyOrchestrator();
    case AgentRole::Builder:
        return VexaraCore::AgentExecutionSettings::roleKeyBuilder();
    case AgentRole::Supervisor:
        return VexaraCore::AgentExecutionSettings::roleKeySupervisor();
    }
    return QString();
}

QString roleDisplayName(AgentRole role)
{
    switch (role) {
    case AgentRole::Orchestrator:
        return QStringLiteral("Orchestrator");
    case AgentRole::Builder:
        return QStringLiteral("Builder");
    case AgentRole::Supervisor:
        return QStringLiteral("Supervisor");
    }
    return QStringLiteral("Agent");
}

} // namespace VexaraOrchestration
