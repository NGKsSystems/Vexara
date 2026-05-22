#include "VexaraOrchestration/Orchestrator.h"

namespace VexaraOrchestration {

Orchestrator::Orchestrator(QObject* parent)
    : QObject(parent)
    , registry_(this)
    , grokBridge_(this)
    , verificationRunner_(this)
{
    connect(&grokBridge_, &GrokBuildBridge::taskStarted, this, [this](const QString& prompt) {
        registry_.setActivePlanSummary(prompt);
        patchAgent(QStringLiteral("orchestrator-1"),
                   AgentState::Running,
                   QStringLiteral("Coordinating Grok Build"));
        patchAgent(QStringLiteral("builder-1"),
                   AgentState::Running,
                   QStringLiteral("Building via Grok"));
        patchAgent(QStringLiteral("supervisor-1"),
                   AgentState::Idle,
                   QStringLiteral("Waiting for builder"));
        emit taskStateChanged(QStringLiteral("Task started"));
    });

    connect(&grokBridge_, &GrokBuildBridge::outputChunk, this, [this](const QString& chunk) {
        AgentSnapshot builder = registry_.agentById(QStringLiteral("builder-1"));
        builder.statusLine = chunk.trimmed().left(120);
        if (builder.statusLine.isEmpty()) {
            builder.statusLine = QStringLiteral("Building...");
        }
        registry_.updateAgent(builder);
    });

    connect(&grokBridge_, &GrokBuildBridge::taskFinished, this, &Orchestrator::onGrokTaskFinished);
    connect(&verificationRunner_, &VerificationRunner::verificationStarted, this, [this]() {
        patchAgent(QStringLiteral("supervisor-1"),
                   AgentState::Running,
                   QStringLiteral("Running verification"));
        emit verificationStateChanged(QStringLiteral("Verification started"));
    });
    connect(&verificationRunner_, &VerificationRunner::verificationFinished, this,
            &Orchestrator::onVerificationFinished);
}

const AgentRegistry& Orchestrator::registry() const
{
    return registry_;
}

AgentRegistry& Orchestrator::registry()
{
    return registry_;
}

void Orchestrator::configure(const VexaraCore::GlobalSettings& settings)
{
    settings_ = settings;
    settings_.models.ensureDefaults();
    grokBridge_.configure(settings_.grokBuild);
    verificationRunner_.configure(settings_.verification);
    applyModelAssignments(settings_.models);
}

void Orchestrator::setProjectRoot(const QString& path)
{
    projectRoot_ = path;
}

QString Orchestrator::projectRoot() const
{
    return projectRoot_;
}

void Orchestrator::bootstrapDefaultAgents()
{
    QVector<AgentSnapshot> agents;
    agents.push_back(AgentSnapshot{
        QStringLiteral("orchestrator-1"),
        QStringLiteral("Orchestrator"),
        AgentRole::Orchestrator,
        AgentState::Idle,
        QStringLiteral("Ready"),
        QString(),
        QString(),
    });
    agents.push_back(AgentSnapshot{
        QStringLiteral("builder-1"),
        QStringLiteral("Builder"),
        AgentRole::Builder,
        AgentState::Idle,
        QStringLiteral("Standby"),
        QString(),
        QString(),
    });
    agents.push_back(AgentSnapshot{
        QStringLiteral("supervisor-1"),
        QStringLiteral("Supervisor"),
        AgentRole::Supervisor,
        AgentState::Idle,
        QStringLiteral("Standby"),
        QString(),
        QString(),
    });
    registry_.setAgents(agents);
    registry_.setActivePlanSummary(QString());
    registry_.setPendingChangeSummary(QString());
    pendingReview_ = false;
    applyModelAssignments(settings_.models);
}

void Orchestrator::submitTask(const QString& prompt)
{
    const QString trimmed = prompt.trimmed();
    if (trimmed.isEmpty()) {
        emit taskStateChanged(QStringLiteral("Enter a task prompt first."));
        return;
    }
    if (grokBridge_.isRunning()) {
        emit taskStateChanged(QStringLiteral("A task is already running."));
        return;
    }

    pendingReview_ = false;
    registry_.setPendingChangeSummary(QString());

    if (!grokBridge_.isConfigured()) {
        registry_.setActivePlanSummary(trimmed);
        patchAgent(QStringLiteral("orchestrator-1"),
                   AgentState::Blocked,
                   QStringLiteral("Set grok_build.command in vexara.json"));
        patchAgent(QStringLiteral("builder-1"), AgentState::Blocked, QStringLiteral("Grok Build not configured"));
        patchAgent(QStringLiteral("supervisor-1"), AgentState::Idle, QStringLiteral("Standby"));
        emit taskStateChanged(
            QStringLiteral("Configure grok_build.command in Settings to run agent tasks."));
        return;
    }

    grokBridge_.runTask(trimmed, projectRoot_);
}

void Orchestrator::approvePendingChanges()
{
    if (!pendingReview_) {
        emit taskStateChanged(QStringLiteral("No pending changes to approve."));
        return;
    }
    pendingReview_ = false;
    registry_.setPendingChangeSummary(QString());
    patchAgent(QStringLiteral("orchestrator-1"), AgentState::Idle, QStringLiteral("Ready"));
    patchAgent(QStringLiteral("builder-1"), AgentState::Idle, QStringLiteral("Standby"));
    patchAgent(QStringLiteral("supervisor-1"), AgentState::Idle, QStringLiteral("Approved"));
    emit taskStateChanged(QStringLiteral("Changes approved"));
}

void Orchestrator::rejectPendingChanges()
{
    if (!pendingReview_) {
        emit taskStateChanged(QStringLiteral("No pending changes to reject."));
        return;
    }
    pendingReview_ = false;
    registry_.setPendingChangeSummary(QString());
    patchAgent(QStringLiteral("builder-1"), AgentState::Blocked, QStringLiteral("Changes rejected"));
    patchAgent(QStringLiteral("supervisor-1"), AgentState::Idle, QStringLiteral("Rejected"));
    patchAgent(QStringLiteral("orchestrator-1"), AgentState::Idle, QStringLiteral("Ready"));
    emit taskStateChanged(QStringLiteral("Changes rejected"));
}

void Orchestrator::runVerification()
{
    if (verificationRunner_.isRunning()) {
        emit verificationStateChanged(QStringLiteral("Verification already running."));
        return;
    }
    verificationRunner_.run(projectRoot_);
}

bool Orchestrator::hasPendingReview() const
{
    return pendingReview_;
}

void Orchestrator::patchAgent(const QString& id,
                              AgentState state,
                              const QString& statusLine,
                              const QString& planSummary)
{
    AgentSnapshot agent = registry_.agentById(id);
    if (agent.id.isEmpty()) {
        return;
    }
    agent.state = state;
    agent.statusLine = statusLine;
    if (!planSummary.isEmpty()) {
        agent.planSummary = planSummary;
    }
    registry_.updateAgent(agent);
}

void Orchestrator::applyModelAssignments(const VexaraCore::ModelSettings& models)
{
    const QVector<AgentSnapshot> agents = registry_.agents();
    for (const AgentSnapshot& agent : agents) {
        AgentSnapshot updated = agent;
        updated.modelId = models.modelIdForAgent(agent.id);
        registry_.updateAgent(updated);
    }
}

void Orchestrator::onGrokTaskFinished(bool success, const QString& summary)
{
    pendingReview_ = success;
    registry_.setPendingChangeSummary(summary);

    patchAgent(QStringLiteral("builder-1"),
               success ? AgentState::WaitingReview : AgentState::Blocked,
               success ? QStringLiteral("Awaiting review") : QStringLiteral("Build failed"));
    patchAgent(QStringLiteral("supervisor-1"),
               success ? AgentState::Running : AgentState::Idle,
               success ? QStringLiteral("Review output") : QStringLiteral("Standby"));
    patchAgent(QStringLiteral("orchestrator-1"),
               AgentState::Idle,
               success ? QStringLiteral("Awaiting approval") : QStringLiteral("Task failed"));

    emit taskStateChanged(summary);
}

void Orchestrator::onVerificationFinished(bool success, const QString& summary)
{
    patchAgent(QStringLiteral("supervisor-1"),
               success ? AgentState::Idle : AgentState::Blocked,
               success ? QStringLiteral("Verification passed") : QStringLiteral("Verification failed"));
    emit verificationStateChanged(summary);
}

} // namespace VexaraOrchestration
