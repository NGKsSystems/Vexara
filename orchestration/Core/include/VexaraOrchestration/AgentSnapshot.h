#pragma once

#include <QString>

namespace VexaraOrchestration {

enum class AgentRole {
    Builder,
    Supervisor,
    Orchestrator,
};

enum class AgentState {
    Idle,
    Running,
    WaitingReview,
    Blocked,
};

struct AgentSnapshot {
    QString id;
    QString displayName;
    AgentRole role = AgentRole::Builder;
    AgentState state = AgentState::Idle;
    QString statusLine;
    QString planSummary;
    QString modelId;
};

} // namespace VexaraOrchestration
