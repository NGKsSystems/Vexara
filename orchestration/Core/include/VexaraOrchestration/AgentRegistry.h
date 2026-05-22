#pragma once

#include "VexaraOrchestration/AgentSnapshot.h"

#include <QObject>
#include <QVector>

namespace VexaraOrchestration {

class AgentRegistry : public QObject {
    Q_OBJECT

public:
    explicit AgentRegistry(QObject* parent = nullptr);

    QVector<AgentSnapshot> agents() const;
    AgentSnapshot agentById(const QString& id) const;
    bool updateAgent(const AgentSnapshot& snapshot);
    void setAgents(const QVector<AgentSnapshot>& agents);
    int count() const;

    QString activePlanSummary() const;
    QString pendingChangeSummary() const;
    void setActivePlanSummary(const QString& summary);
    void setPendingChangeSummary(const QString& summary);

signals:
    void agentsChanged();

private:
    QVector<AgentSnapshot> agents_;
    QString activePlanSummary_;
    QString pendingChangeSummary_;
};

} // namespace VexaraOrchestration
