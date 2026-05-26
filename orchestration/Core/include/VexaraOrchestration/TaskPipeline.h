#pragma once

#include "VexaraOrchestration/AgentSnapshot.h"
#include "VexaraOrchestration/ITaskBackend.h"
#include "VexaraOrchestration/TaskBackendRegistry.h"
#include "VexaraOrchestration/TaskContext.h"

#include <QString>

namespace VexaraOrchestration {

enum class PipelineStage {
    Idle,
    Orchestrator,
    Builder,
    Supervisor,
    Done,
};

struct TaskPipelineState {
    QString userTask;
    TaskContext context;
    QString orchestratorPlan;
    QString builderResult;
    QString supervisorVerdict;
    QString planTranscript;
    PipelineStage activeStage = PipelineStage::Idle;
    bool runOrchestrator = false;
    bool runSupervisor = false;
};

class TaskPipelineHooks {
public:
    static bool shouldRunOrchestrator(const TaskBackendRegistry& registry);
    static bool shouldRunSupervisor(const TaskBackendRegistry& registry);
    static bool roleSupportsCapability(AgentRole role,
                                       TaskBackendCapability capability,
                                       const TaskBackendRegistry& registry);
};

} // namespace VexaraOrchestration
