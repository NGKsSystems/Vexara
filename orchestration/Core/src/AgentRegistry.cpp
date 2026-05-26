#include "VexaraOrchestration/AgentRegistry.h"

namespace VexaraOrchestration {

AgentRegistry::AgentRegistry(QObject* parent)
    : QObject(parent)
{
}

QVector<AgentSnapshot> AgentRegistry::agents() const
{
    return agents_;
}

AgentSnapshot AgentRegistry::agentById(const QString& id) const
{
    for (const AgentSnapshot& agent : agents_) {
        if (agent.id == id) {
            return agent;
        }
    }
    return AgentSnapshot{};
}

bool AgentRegistry::updateAgent(const AgentSnapshot& snapshot)
{
    for (AgentSnapshot& agent : agents_) {
        if (agent.id == snapshot.id) {
            if (agent.state == snapshot.state && agent.statusLine == snapshot.statusLine
                && agent.planSummary == snapshot.planSummary
                && agent.modelId == snapshot.modelId) {
                return true;
            }
            agent = snapshot;
            emit agentsChanged();
            return true;
        }
    }
    return false;
}

void AgentRegistry::setAgents(const QVector<AgentSnapshot>& agents)
{
    agents_ = agents;
    emit agentsChanged();
}

int AgentRegistry::count() const
{
    return agents_.size();
}

QString AgentRegistry::activePlanSummary() const
{
    return activePlanSummary_;
}

QString AgentRegistry::pendingChangeSummary() const
{
    return pendingChangeSummary_;
}

void AgentRegistry::setActivePlanSummary(const QString& summary)
{
    if (activePlanSummary_ == summary) {
        return;
    }
    activePlanSummary_ = summary;
    emit agentsChanged();
}

void AgentRegistry::setPendingChangeSummary(const QString& summary)
{
    if (pendingChangeSummary_ == summary) {
        return;
    }
    pendingChangeSummary_ = summary;
    emit agentsChanged();
}

} // namespace VexaraOrchestration
