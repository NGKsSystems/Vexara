#include "VexaraOrchestration/TaskPipeline.h"

#include "VexaraOrchestration/TaskBackendRegistry.h"

namespace VexaraOrchestration {

namespace {

bool roleReadyForCapability(AgentRole role,
                            TaskBackendCapability capability,
                            const TaskBackendRegistry& registry)
{
    if (registry.serviceForRole(role) == VexaraCore::AgentServiceKind::None) {
        return false;
    }
    if (!registry.isRoleConfigured(role)) {
        return false;
    }
    const ITaskBackend* backend = registry.backendForRole(role);
    return backend && backend->capabilities().testFlag(capability);
}

} // namespace

bool TaskPipelineHooks::roleSupportsCapability(AgentRole role,
                                               TaskBackendCapability capability,
                                               const TaskBackendRegistry& registry)
{
    return roleReadyForCapability(role, capability, registry);
}

bool TaskPipelineHooks::shouldRunOrchestrator(const TaskBackendRegistry& registry)
{
    return roleReadyForCapability(AgentRole::Orchestrator, TaskBackendCapability::Planning, registry);
}

bool TaskPipelineHooks::shouldRunSupervisor(const TaskBackendRegistry& registry)
{
    return roleReadyForCapability(AgentRole::Supervisor, TaskBackendCapability::Review, registry);
}

} // namespace VexaraOrchestration
