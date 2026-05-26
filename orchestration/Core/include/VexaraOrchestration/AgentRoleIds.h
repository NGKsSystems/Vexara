#pragma once

#include "VexaraOrchestration/AgentSnapshot.h"

#include <QString>

namespace VexaraOrchestration {

QString agentIdForRole(AgentRole role);
QString roleKeyForRole(AgentRole role);
QString roleDisplayName(AgentRole role);

} // namespace VexaraOrchestration
