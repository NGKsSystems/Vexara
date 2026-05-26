#include "VexaraOrchestration/DirectorAgent.h"
#include "VexaraOrchestration/Orchestrator.h"

#include "VexaraCore/AgentServiceKind.h"
#include "VexaraCore/PipelinePaths.h"
#include "VexaraOrchestration/AgentRoleIds.h"
#include "VexaraOrchestration/TaskPipeline.h"

#include <QDir>

namespace VexaraOrchestration {

namespace {

QString formatProgressDurationMs(qint64 milliseconds)
{
    if (milliseconds < 0) {
        milliseconds = 0;
    }
    const qint64 totalSeconds = milliseconds / 1000;
    if (totalSeconds < 60) {
        return QStringLiteral("%1s").arg(totalSeconds);
    }
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    if (minutes < 60) {
        return QStringLiteral("%1m %2s").arg(minutes).arg(seconds);
    }
    const qint64 hours = minutes / 60;
    return QStringLiteral("%1h %2m").arg(hours).arg(minutes % 60);
}

bool parseStageLabel(const QString& stageLabel, QString* agentLabelOut, QString* actionVerbOut)
{
    if (!agentLabelOut || !actionVerbOut) {
        return false;
    }

    const QString stage = stageLabel.trimmed().toLower();
    if (stage == QStringLiteral("planning")
        || stage.contains(QStringLiteral("planner"))) {
        *agentLabelOut = QStringLiteral("Planner");
        *actionVerbOut = QStringLiteral("Planning");
        return true;
    }
    if (stage == QStringLiteral("supervisor_review")
        || stage.contains(QStringLiteral("supervisor"))) {
        *agentLabelOut = QStringLiteral("Supervisor");
        *actionVerbOut = QStringLiteral("Reviewing");
        return true;
    }
    if (stage == QStringLiteral("worker") || stage.contains(QStringLiteral("builder"))) {
        *agentLabelOut = QStringLiteral("Builder");
        *actionVerbOut = QStringLiteral("Building");
        return true;
    }
    if (stage == QStringLiteral("testing")) {
        *agentLabelOut = QStringLiteral("Testing");
        *actionVerbOut = QStringLiteral("Running verify");
        return true;
    }
    if (stage == QStringLiteral("review")) {
        *agentLabelOut = QStringLiteral("Review");
        *actionVerbOut = QStringLiteral("Final review");
        return true;
    }

    if (!stage.isEmpty()) {
        *agentLabelOut = stageLabel.trimmed();
        *actionVerbOut = QStringLiteral("Working");
        return true;
    }
    return false;
}

} // namespace

Orchestrator::Orchestrator(QObject* parent)
    : QObject(parent)
    , registry_(this)
    , taskBackends_(this)
    , verificationRunner_(this)
    , director_(this)
    , planRefreshTimer_(this)
    , taskStateEmitTimer_(this)
{
    planRefreshTimer_.setSingleShot(true);
    planRefreshTimer_.setInterval(200);
    connect(&planRefreshTimer_, &QTimer::timeout, this, &Orchestrator::refreshPlanFromTranscript);

    taskStateEmitTimer_.setSingleShot(true);
    taskStateEmitTimer_.setInterval(250);
    connect(&taskStateEmitTimer_, &QTimer::timeout, this, [this]() {
        if (!pendingTaskStateSummary_.isEmpty()) {
            emit taskStateChanged(pendingTaskStateSummary_);
        }
    });
    connect(&taskBackends_, &TaskBackendRegistry::taskStarted, this, &Orchestrator::onTaskStarted);
    connect(&taskBackends_, &TaskBackendRegistry::outputChunk, this, &Orchestrator::onTaskOutputChunk);
    connect(&taskBackends_, &TaskBackendRegistry::taskFinished, this, &Orchestrator::onTaskFinished);

    connect(&verificationRunner_, &VerificationRunner::verificationStarted, this, [this]() {
        patchAgent(QStringLiteral("supervisor-1"),
                   AgentState::Running,
                   QStringLiteral("Running verification"));
        emit verificationStateChanged(QStringLiteral("Verification started"));
    });
    connect(&verificationRunner_, &VerificationRunner::verificationFinished, this,
            &Orchestrator::onVerificationFinished);

    connect(&chatClient_, &AgentChatClient::requestStarted, this, [this](const QString& agentName) {
        patchAgent(QStringLiteral("builder-1"),
                   AgentState::Running,
                   QStringLiteral("Chatting with %1").arg(agentName));
        emit chatHistoryChanged();
    });
    connect(&chatClient_, &AgentChatClient::requestFinished, this, &Orchestrator::onChatFinished);

    connect(&director_, &DirectorAgent::systemStateChanged, this,
            &Orchestrator::onDirectorSystemStateChanged);
    connect(&director_, &DirectorAgent::taskDispatched, this,
            &Orchestrator::onDirectorTaskDispatched);
    connect(&director_, &DirectorAgent::taskCompleted, this,
            &Orchestrator::onDirectorTaskCompleted);
    connect(&director_, &DirectorAgent::pipelineCompleted, this,
            &Orchestrator::onDirectorPipelineCompleted);
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
    settings_.agentExecution.ensureDefaults();
    settings_.models.syncAssignmentsFromAgentExecution(settings_.agentExecution);
    promptComposer_.configure(settings_.grokBuild, settings_.agentExecution.openclaw);
    taskBackends_.configure(settings_, promptComposer_);
    verificationRunner_.configure(settings_.verification);
    applyModelAssignments(settings_.models);
    director_.configure(&taskBackends_, &promptComposer_, &registry_, &pipelineQueue_, projectRoot_,
                        workerEditorHost_, testerCommandHost_, settings_.verification);
}

void Orchestrator::setWorkerEditorHost(IWorkerEditorHost* editorHost)
{
    workerEditorHost_ = editorHost;
    director_.configure(&taskBackends_, &promptComposer_, &registry_, &pipelineQueue_, projectRoot_,
                        workerEditorHost_, testerCommandHost_, settings_.verification);
}

void Orchestrator::setTesterCommandHost(ITesterCommandHost* commandHost)
{
    testerCommandHost_ = commandHost;
    director_.configure(&taskBackends_, &promptComposer_, &registry_, &pipelineQueue_, projectRoot_,
                        workerEditorHost_, testerCommandHost_, settings_.verification);
}

void Orchestrator::setProjectRoot(const QString& path)
{
    projectRoot_ = path;
    openPipelineQueueForProject(projectRoot_);
    director_.configure(&taskBackends_, &promptComposer_, &registry_, &pipelineQueue_, projectRoot_,
                        workerEditorHost_, testerCommandHost_, settings_.verification);
}

void Orchestrator::openPipelineQueueForProject(const QString& projectRoot)
{
    closePipelineQueue();
    if (projectRoot.trimmed().isEmpty()) {
        return;
    }

    QDir().mkpath(VexaraCore::PipelinePaths::pipelineRoot(projectRoot));
    QDir().mkpath(VexaraCore::PipelinePaths::artifactsRoot(projectRoot));

    const QString dbPath = VexaraCore::PipelinePaths::queueDatabasePath(projectRoot);
    if (!pipelineQueue_.open(dbPath)) {
        emit taskStateChanged(QStringLiteral("Pipeline queue unavailable: %1")
                                  .arg(pipelineQueue_.lastError()));
        return;
    }

    const int recovered = pipelineQueue_.reconcileStaleRunningTasks();
    if (recovered > 0) {
        emit taskStateChanged(
            QStringLiteral("Recovered %1 interrupted pipeline task(s); queue will resume.")
                .arg(recovered));
    }

    QString restoredTask;
    if (pipelineQueue_.latestRootPlanningUserTask(restoredTask)) {
        lastPipelineUserTask_ = restoredTask;
    }

    director_.start();
}

void Orchestrator::closePipelineQueue()
{
    director_.stop();
    pipelineQueue_.close();
}

QString Orchestrator::projectRoot() const
{
    return projectRoot_;
}

VexaraCore::AgentServiceKind Orchestrator::serviceForRole(AgentRole role) const
{
    return taskBackends_.serviceForRole(role);
}

QString Orchestrator::effectiveModelDisplayForRole(AgentRole role) const
{
    const QString agentId = agentIdForRole(role);
    const VexaraCore::AgentServiceKind service = serviceForRole(role);

    switch (service) {
    case VexaraCore::AgentServiceKind::OpenClawCli: {
        const QString model = settings_.agentExecution.openclaw.model.trimmed();
        return model.isEmpty() ? QStringLiteral("Not set — choose in Settings → Agent Roles")
                               : model;
    }
    case VexaraCore::AgentServiceKind::AiderCli: {
        const QString model = settings_.agentExecution.aider.model.trimmed();
        return model.isEmpty() ? QStringLiteral("Not set — choose in Settings → Agent Roles")
                               : model;
    }
    case VexaraCore::AgentServiceKind::GrokCli: {
        const VexaraCore::ModelProfile profile =
            settings_.models.profileById(settings_.models.modelIdForAgent(agentId));
        if (!profile.id.isEmpty() && !profile.modelName.isEmpty()) {
            return QStringLiteral("%1 (%2)").arg(profile.displayName, profile.modelName);
        }
        return QStringLiteral("Grok CLI");
    }
    case VexaraCore::AgentServiceKind::OpenAiHttp:
    case VexaraCore::AgentServiceKind::OpenRouterHttp: {
        const VexaraCore::ModelProfile profile =
            settings_.models.profileById(settings_.models.modelIdForAgent(agentId));
        if (profile.id.isEmpty()) {
            return QStringLiteral("Not set — choose in Settings → Agent Roles");
        }
        return QStringLiteral("%1 — %2").arg(profile.displayName, profile.modelName);
    }
    case VexaraCore::AgentServiceKind::None:
        break;
    }
    return QStringLiteral("—");
}

bool Orchestrator::isRoleBackendConfigured(AgentRole role) const
{
    return taskBackends_.isRoleConfigured(role);
}

bool Orchestrator::usesOpenClawForPlannerRoles() const
{
    return serviceForRole(AgentRole::Orchestrator) == VexaraCore::AgentServiceKind::OpenClawCli
           || serviceForRole(AgentRole::Supervisor) == VexaraCore::AgentServiceKind::OpenClawCli;
}

QString Orchestrator::openClawPlannerStatus() const
{
    VexaraCore::OpenClawSettings openClaw = settings_.agentExecution.openclaw;
    openClaw.ensureDefaults();
    if (!openClaw.isConfigured()) {
        return QStringLiteral("OpenClaw executable not configured — open Settings → OpenClaw CLI.");
    }
    if (!openClaw.usesLocalOllama()) {
        return QStringLiteral("OpenClaw: cloud mode (%1).").arg(openClaw.model.trimmed());
    }
    QString detail;
    if (openClaw.isLocalOllamaReady(&detail)) {
        return detail;
    }
    return detail + QStringLiteral(" Click \"Configure for Ollama\".");
}

bool Orchestrator::configureOpenClawForLocalOllama(QString* message)
{
    settings_.agentExecution.openclaw.ensureDefaults();
    settings_.agentExecution.openclaw.localOllamaMode = true;
    if (settings_.agentExecution.openclaw.model.trimmed().isEmpty()
        || !settings_.agentExecution.openclaw.model.trimmed().startsWith(
            QStringLiteral("ollama/"), Qt::CaseInsensitive)) {
        settings_.agentExecution.openclaw.model = QStringLiteral("ollama/qwen2.5-coder:14b");
    }

    const VexaraCore::OpenClawSettings::LocalSetupResult result =
        settings_.agentExecution.openclaw.applyLocalOllamaConfiguration();

    taskBackends_.configure(settings_, promptComposer_);
    promptComposer_.configure(settings_.grokBuild, settings_.agentExecution.openclaw);

    if (message) {
        *message = result.message;
    }
    if (result.success) {
        director_.resumeQueue();
        emit taskStateChanged(result.message);
    }
    return result.success;
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

void Orchestrator::submitTask(const QString& userTask, const VexaraCore::GrokTaskContext& context)
{
    const QString trimmed = userTask.trimmed();
    if (trimmed.isEmpty()) {
        emit taskStateChanged(QStringLiteral("Enter a task prompt first."));
        return;
    }
    if (isTaskRunning()) {
        emit taskStateChanged(QStringLiteral("A task is already running."));
        return;
    }

    legacyPipelineActive_ = true;
    pendingReview_ = false;
    registry_.setPendingChangeSummary(QString());

    TaskContext resolved = context;
    if (resolved.projectPath.isEmpty()) {
        resolved.projectPath = projectRoot_;
    }
    if (resolved.detectedProjectType.isEmpty()) {
        resolved.detectedProjectType = VexaraCore::detectProjectType(resolved.projectPath);
    }

    pipelineState_ = TaskPipelineState{};
    pipelineState_.userTask = trimmed;
    pipelineState_.context = resolved;
    pipelineState_.runOrchestrator = TaskPipelineHooks::shouldRunOrchestrator(taskBackends_);
    pipelineState_.runSupervisor = TaskPipelineHooks::shouldRunSupervisor(taskBackends_);

    if (!taskBackends_.isRoleConfigured(AgentRole::Builder)) {
        const QString preview =
            promptComposer_.composeUserPrompt(AgentRole::Builder, trimmed, resolved);
        registry_.setActivePlanSummary(preview);
        patchAgent(QStringLiteral("orchestrator-1"), AgentState::Blocked, QStringLiteral("Builder backend not ready"));
        patchAgent(QStringLiteral("builder-1"), AgentState::Blocked, QStringLiteral("Not configured"));
        patchAgent(QStringLiteral("supervisor-1"), AgentState::Idle, QStringLiteral("Standby"));
        emit taskStateChanged(notConfiguredMessage(AgentRole::Builder));
        legacyPipelineActive_ = false;
        return;
    }

    if (pipelineState_.runOrchestrator) {
        runPipelineStage(AgentRole::Orchestrator);
    } else {
        runPipelineStage(AgentRole::Builder);
    }
}

void Orchestrator::runPipelineStage(AgentRole role)
{
    ITaskBackend* backend = taskBackends_.backendForRole(role);
    if (!backend) {
        if (role == AgentRole::Orchestrator) {
            runPipelineStage(AgentRole::Builder);
            return;
        }
        if (role == AgentRole::Supervisor) {
            finishPipeline(!pipelineState_.builderResult.isEmpty(),
                           pipelineState_.builderResult);
            return;
        }
        finishPipeline(false, QStringLiteral("No backend assigned for %1.").arg(roleDisplayName(role)));
        return;
    }

    if (!taskBackends_.isRoleConfigured(role)) {
        if (role == AgentRole::Orchestrator) {
            pipelineState_.orchestratorPlan.clear();
            runPipelineStage(AgentRole::Builder);
            return;
        }
        if (role == AgentRole::Supervisor) {
            finishPipeline(true, pipelineState_.builderResult);
            return;
        }
        finishPipeline(false, notConfiguredMessage(role));
        return;
    }

    QString composedPrompt;
    switch (role) {
    case AgentRole::Orchestrator:
        composedPrompt =
            promptComposer_.composeUserPrompt(AgentRole::Orchestrator, pipelineState_.userTask,
                                              pipelineState_.context);
        break;
    case AgentRole::Builder:
        composedPrompt = promptComposer_.composeBuilderPromptWithPlan(
            pipelineState_.userTask, pipelineState_.orchestratorPlan, pipelineState_.context);
        break;
    case AgentRole::Supervisor:
        composedPrompt = promptComposer_.composeSupervisorReviewPrompt(
            pipelineState_.userTask, pipelineState_.builderResult, pipelineState_.context);
        break;
    }

    pipelineState_.activeStage = role == AgentRole::Orchestrator   ? PipelineStage::Orchestrator
                                 : role == AgentRole::Builder     ? PipelineStage::Builder
                                 : role == AgentRole::Supervisor   ? PipelineStage::Supervisor
                                                                  : PipelineStage::Idle;
    activeTaskRole_ = role;
    backend->executeTask(role, composedPrompt, pipelineState_.context);
}

void Orchestrator::handleOrchestratorFinished(bool success, const QString& summary)
{
    if (success) {
        pipelineState_.orchestratorPlan = summary;
    } else {
        pipelineState_.orchestratorPlan.clear();
        appendStageOutput(AgentRole::Orchestrator,
                          QStringLiteral("\n(Orchestrator failed; continuing without plan.)\n"));
        patchAgent(QStringLiteral("orchestrator-1"), AgentState::Blocked, summary.left(120));
    }
    runPipelineStage(AgentRole::Builder);
}

void Orchestrator::handleBuilderFinished(bool success, const QString& summary)
{
    if (!success) {
        finishPipeline(false, summary);
        return;
    }

    pipelineState_.builderResult = summary;
    if (pipelineState_.runSupervisor) {
        runPipelineStage(AgentRole::Supervisor);
    } else {
        finishPipeline(true, summary);
    }
}

void Orchestrator::handleSupervisorFinished(bool success, const QString& summary)
{
    pipelineState_.supervisorVerdict = summary;
    QString pending = summary;
    if (!pipelineState_.builderResult.isEmpty()) {
        pending = QStringLiteral("## Supervisor review\n%1\n\n## Builder output\n%2")
                      .arg(summary, pipelineState_.builderResult);
    }
    const bool canReview = success || !pipelineState_.builderResult.trimmed().isEmpty();
    finishPipeline(canReview, pending);
}

void Orchestrator::finishPipeline(bool success, const QString& summary)
{
    legacyPipelineActive_ = false;
    pipelineState_.activeStage = PipelineStage::Done;
    pendingReview_ = success;

    registry_.setPendingChangeSummary(summary);

    patchAgent(QStringLiteral("builder-1"),
               success ? AgentState::WaitingReview : AgentState::Blocked,
               success ? QStringLiteral("Awaiting review") : QStringLiteral("Task failed"));
    patchAgent(QStringLiteral("supervisor-1"),
               success ? AgentState::Idle : AgentState::Blocked,
               success ? QStringLiteral("Review complete") : QStringLiteral("Review failed"));
    patchAgent(QStringLiteral("orchestrator-1"),
               AgentState::Idle,
               success ? QStringLiteral("Pipeline complete") : QStringLiteral("Pipeline failed"));

    emit taskStateChanged(summary.isEmpty() ? QStringLiteral("Task finished.") : summary);
}

void Orchestrator::appendStageToTranscript(AgentRole role, const QString& prompt)
{
    const QString header = QStringLiteral("## %1\n").arg(roleDisplayName(role));
    if (!pipelineState_.planTranscript.isEmpty()) {
        pipelineState_.planTranscript += QStringLiteral("\n\n");
    }
    pipelineState_.planTranscript += header + prompt;
    taskLiveOutput_.clear();
    refreshPlanFromTranscript();
}

void Orchestrator::appendStageOutput(AgentRole role, const QString& chunk)
{
    if (chunk.isEmpty()) {
        return;
    }
    taskLiveOutput_.append(chunk);
    if (taskLiveOutput_.length() > 120000) {
        taskLiveOutput_ = taskLiveOutput_.right(120000);
    }
    schedulePlanRefresh();
}

void Orchestrator::schedulePlanRefresh()
{
    planRefreshTimer_.start();
}

void Orchestrator::emitTaskStateThrottled(const QString& summary)
{
    pendingTaskStateSummary_ = summary;
    taskStateEmitTimer_.start();
}

void Orchestrator::refreshPlanFromTranscript()
{
    QString plan = pipelineState_.planTranscript;
    if (!taskLiveOutput_.isEmpty()) {
        if (hierarchicalPlanSessionActive_) {
            const QString stage = director_.currentStageLabel();
            plan += QStringLiteral("\n\n--- %1 live output ---\n")
                        .arg(stage.isEmpty() ? QStringLiteral("Stage") : stage);
        } else {
            plan += outputHeaderForRole(activeTaskRole_);
        }
        plan += taskLiveOutput_;
    }
    registry_.setActivePlanSummary(plan);
}

QString Orchestrator::outputHeaderForRole(AgentRole role) const
{
    const bool httpBackend = isHttpService(taskBackends_.serviceForRole(role));
    const QString label = httpBackend ? QStringLiteral("response") : QStringLiteral("output");
    return QStringLiteral("\n\n--- %1 %2 ---\n").arg(roleDisplayName(role), label);
}

void Orchestrator::updateAgentStatusesForStage(AgentRole role, bool starting)
{
    if (starting) {
        if (role == AgentRole::Orchestrator) {
            patchAgent(QStringLiteral("orchestrator-1"), AgentState::Running, QStringLiteral("Planning"));
            patchAgent(QStringLiteral("builder-1"), AgentState::Idle, QStringLiteral("Waiting"));
            patchAgent(QStringLiteral("supervisor-1"), AgentState::Idle, QStringLiteral("Standby"));
        } else if (role == AgentRole::Builder) {
            patchAgent(QStringLiteral("orchestrator-1"),
                       pipelineState_.runOrchestrator ? AgentState::Idle : AgentState::Running,
                       pipelineState_.runOrchestrator ? QStringLiteral("Plan ready")
                                                      : QStringLiteral("Coordinating"));
            patchAgent(QStringLiteral("builder-1"), AgentState::Running, QStringLiteral("Building"));
            patchAgent(QStringLiteral("supervisor-1"), AgentState::Idle, QStringLiteral("Waiting"));
        } else if (role == AgentRole::Supervisor) {
            patchAgent(QStringLiteral("orchestrator-1"), AgentState::Idle, QStringLiteral("Done"));
            patchAgent(QStringLiteral("builder-1"), AgentState::Idle, QStringLiteral("Done"));
            patchAgent(QStringLiteral("supervisor-1"), AgentState::Running, QStringLiteral("Reviewing"));
        }
        return;
    }
}

void Orchestrator::sendChat(const QString& message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        emit taskStateChanged(QStringLiteral("Enter a chat message first."));
        return;
    }
    if (chatClient_.isBusy()) {
        emit taskStateChanged(QStringLiteral("Wait for the current chat reply."));
        return;
    }
    if (isTaskRunning()) {
        emit taskStateChanged(QStringLiteral("A task is already running."));
        return;
    }

    const QString modelId = settings_.models.modelIdForAgent(QStringLiteral("builder-1"));
    const VexaraCore::ModelProfile profile = settings_.models.profileById(modelId);
    const AgentSnapshot builder = registry_.agentById(QStringLiteral("builder-1"));
    const QString speaker = builder.displayName.isEmpty() ? QStringLiteral("Builder") : builder.displayName;

    const QVector<ChatMessage> priorHistory = chatHistory_;
    appendChatMessage(QStringLiteral("user"), QStringLiteral("You"), trimmed);
    chatClient_.sendMessage(profile, speaker, priorHistory, trimmed, chatSystemContext());
}

void Orchestrator::approvePendingChanges()
{
    if (!pendingReview_) {
        emit taskStateChanged(QStringLiteral("No pending changes to keep."));
        return;
    }
    pendingReview_ = false;
    registry_.setPendingChangeSummary(QString());
    patchAgent(QStringLiteral("orchestrator-1"), AgentState::Idle, QStringLiteral("Ready"));
    patchAgent(QStringLiteral("builder-1"), AgentState::Idle, QStringLiteral("Standby"));
    patchAgent(QStringLiteral("supervisor-1"), AgentState::Idle, QStringLiteral("Approved"));
    emit taskStateChanged(QStringLiteral("Changes kept"));
}

void Orchestrator::rejectPendingChanges()
{
    if (!pendingReview_) {
        emit taskStateChanged(QStringLiteral("No pending changes to delete."));
        return;
    }
    pendingReview_ = false;
    registry_.setPendingChangeSummary(QString());
    patchAgent(QStringLiteral("builder-1"), AgentState::Blocked, QStringLiteral("Changes discarded"));
    patchAgent(QStringLiteral("supervisor-1"), AgentState::Idle, QStringLiteral("Discarded"));
    patchAgent(QStringLiteral("orchestrator-1"), AgentState::Idle, QStringLiteral("Ready"));
    emit taskStateChanged(QStringLiteral("Changes discarded"));
}

void Orchestrator::keepPendingChanges()
{
    approvePendingChanges();
}

void Orchestrator::deletePendingOrQueuedWork()
{
    if (pendingReview_) {
        rejectPendingChanges();
        return;
    }

    if (isHierarchicalPipelineRunning() || taskBackends_.anyBackendRunning()) {
        director_.abortActivePipelineWork(QStringLiteral("Cancelled by user."));
        appendHierarchicalPlanLine(QStringLiteral("[Director] Active pipeline work cancelled."));
    }

    const int cancelled = cancelPendingPipelineTasks();
    if (cancelled == 0 && !isHierarchicalPipelineRunning() && !taskBackends_.anyBackendRunning()) {
        emit taskStateChanged(QStringLiteral("Nothing to delete — no pending review or queued pipelines."));
    } else if (cancelled > 0) {
        emit taskStateChanged(
            QStringLiteral("Stopped active work and removed %1 queued pipeline(s).").arg(cancelled));
    } else {
        emit taskStateChanged(QStringLiteral("Active pipeline work stopped."));
    }
}

void Orchestrator::restartPipelineWork()
{
    if ((isHierarchicalPipelineRunning() || taskBackends_.anyBackendRunning()) && !pendingReview_) {
        director_.abortActivePipelineWork(QStringLiteral("Restarting pipeline."));
        appendHierarchicalPlanLine(QStringLiteral("[Director] Stopped active work for restart."));
    }

    if (pendingReview_) {
        pendingReview_ = false;
        registry_.setPendingChangeSummary(QString());
        patchAgent(QStringLiteral("builder-1"), AgentState::Idle, QStringLiteral("Standby"));
        patchAgent(QStringLiteral("supervisor-1"), AgentState::Idle, QStringLiteral("Standby"));
        patchAgent(QStringLiteral("orchestrator-1"), AgentState::Idle, QStringLiteral("Ready"));
        appendHierarchicalPlanLine(QStringLiteral("[Director] Review cleared — restarting pipeline."));
        onPipelinePromptReceived();
    }

    if (isPipelineQueuePaused()) {
        resumePipelineQueue();
        appendHierarchicalPlanLine(QStringLiteral("[Director] Queue resumed for restart."));
        appendQueueStatusLine();
    }

    const QString trimmed = lastPipelineUserTask_.trimmed();
    if (trimmed.isEmpty()) {
        if (isPipelineQueuePaused()) {
            emit taskStateChanged(QStringLiteral("Queue resumed. Enter a task and run again."));
        } else {
            emit taskStateChanged(
                QStringLiteral("Nothing to restart — run a pipeline task first, or use Resume queue if paused."));
        }
        return;
    }

    enqueuePipelineTask(trimmed, lastPipelineContext_);
    emit taskStateChanged(QStringLiteral("Pipeline restarted."));
}

QString Orchestrator::lastPipelineUserTask() const
{
    return lastPipelineUserTask_;
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

bool Orchestrator::isGrokBuildConfigured() const
{
    return settings_.grokBuild.isConfigured();
}

QString Orchestrator::grokBuildCommand() const
{
    return settings_.grokBuild.command;
}

bool Orchestrator::isTaskRunning() const
{
    return legacyPipelineActive_ || taskBackends_.anyBackendRunning() || director_.isDispatching();
}

bool Orchestrator::isDirectorActive() const
{
    return director_.isActive();
}

bool Orchestrator::isHierarchicalPipelineRunning() const
{
    return director_.isDispatching() || director_.activeTaskId() > 0;
}

bool Orchestrator::isHierarchicalPipelineReady() const
{
    if (projectRoot_.trimmed().isEmpty() || !pipelineQueue_.isOpen()) {
        return false;
    }
    return isRoleBackendConfigured(AgentRole::Orchestrator)
           && isRoleBackendConfigured(AgentRole::Supervisor)
           && isRoleBackendConfigured(AgentRole::Builder);
}

QString Orchestrator::activePipelineStage() const
{
    return director_.currentStageLabel();
}

int Orchestrator::pendingQueueTaskCount() const
{
    return director_.pendingTaskCount();
}

int Orchestrator::pendingRootPipelineCount() const
{
    return director_.pendingRootPipelineCount();
}

int Orchestrator::deferredQueueTaskCount() const
{
    return director_.deferredTaskCount();
}

int Orchestrator::clearDeferredPipelineTasks()
{
    const int cleared = director_.clearDeferredTasks();
    if (cleared > 0) {
        appendHierarchicalPlanLine(
            QStringLiteral("[Director] Cleared %1 deferred pipeline task(s).").arg(cleared));
        appendQueueStatusLine();
    }
    emit taskStateChanged(cleared > 0
                              ? QStringLiteral("Cleared %1 deferred pipeline task(s).").arg(cleared)
                              : QStringLiteral("No deferred pipeline tasks to clear."));
    return cleared;
}

int Orchestrator::cancelPendingPipelineTasks()
{
    const int cancelled = director_.cancelPendingRootPipelines();
    if (cancelled > 0) {
        appendHierarchicalPlanLine(
            QStringLiteral("[Director] Cancelled %1 queued pipeline task(s).").arg(cancelled));
        appendQueueStatusLine();
        emit taskStateChanged(
            QStringLiteral("Cancelled %1 queued pipeline task(s).").arg(cancelled));
    }
    return cancelled;
}

void Orchestrator::resumePipelineQueue()
{
    director_.resumeQueue();
}

bool Orchestrator::isPipelineQueuePaused() const
{
    return director_.isQueuePaused();
}

QString Orchestrator::pipelineQueuePauseReason() const
{
    return director_.queuePauseReason();
}

qint64 Orchestrator::enqueuePipelineTask(const QString& userTask,
                                         const VexaraCore::GrokTaskContext& context)
{
    const QString trimmed = userTask.trimmed();
    if (trimmed.isEmpty()) {
        emit taskStateChanged(QStringLiteral("Enter a task prompt first."));
        return 0;
    }
    if (!pipelineQueue_.isOpen()) {
        emit taskStateChanged(QStringLiteral("Open a project folder to use the pipeline queue."));
        return 0;
    }

    VexaraCore::GrokTaskContext resolved = context;
    if (resolved.projectPath.isEmpty()) {
        resolved.projectPath = projectRoot_;
    }
    if (resolved.detectedProjectType.isEmpty()) {
        resolved.detectedProjectType = VexaraCore::detectProjectType(resolved.projectPath);
    }

    lastPipelineUserTask_ = trimmed;
    lastPipelineContext_ = resolved;

    const qint64 taskId = director_.enqueuePipelineStart(trimmed, resolved);
    if (taskId <= 0) {
        emit taskStateChanged(director_.lastError());
    } else {
        if (!hierarchicalPlanSessionActive_) {
            hierarchicalPlanSessionActive_ = true;
            taskLiveOutput_.clear();
            appendDirectorPipelineTaskHeader(trimmed);
        } else {
            onPipelinePromptReceived();
            appendHierarchicalPlanLine(QString());
            appendHierarchicalPlanLine(QStringLiteral("=== QUEUED PIPELINE ==="));
            appendHierarchicalPlanLine(trimmed);
        }
        appendHierarchicalPlanLine(
            QStringLiteral("[Director] Pipeline queued (planning task %1, %2 root pipeline(s) waiting).")
                .arg(taskId)
                .arg(director_.pendingRootPipelineCount()));
        appendQueueStatusLine();
        patchAgent(QStringLiteral("orchestrator-1"),
                   AgentState::Idle,
                   QStringLiteral("Pipeline queued (%1 root task(s) waiting)")
                       .arg(director_.pendingRootPipelineCount()));
        emit taskStateChanged(
            QStringLiteral("Hierarchical pipeline queued (task %1).").arg(taskId));
    }
    return taskId;
}

void Orchestrator::enqueueThreeTestPipelineTasks(const VexaraCore::GrokTaskContext& context)
{
    if (!pipelineQueue_.isOpen()) {
        emit taskStateChanged(QStringLiteral("Open a project folder to use the pipeline queue."));
        return;
    }

    VexaraCore::GrokTaskContext resolved = context;
    if (resolved.projectPath.isEmpty()) {
        resolved.projectPath = projectRoot_;
    }
    if (resolved.detectedProjectType.isEmpty()) {
        resolved.detectedProjectType = VexaraCore::detectProjectType(resolved.projectPath);
    }

    const QString targetFile = QStringLiteral("tools/pipeline_queue_test_target.cpp");
    resolved.currentFilePath = targetFile;

    for (int line = 1; line <= 3; ++line) {
        const QString task =
            QStringLiteral("Add a new line to %1 containing only this comment: // QUEUE_TEST_LINE_%2. "
                           "Do not modify any other file.")
                .arg(targetFile)
                .arg(line);
        enqueuePipelineTask(task, resolved);
    }

    emit taskStateChanged(QStringLiteral("Enqueued 3 test pipeline tasks (sequential queue)."));
}

bool Orchestrator::isChatBusy() const
{
    return chatClient_.isBusy();
}

QVector<ChatMessage> Orchestrator::chatHistory() const
{
    return chatHistory_;
}

QString Orchestrator::formattedChatTranscript() const
{
    QStringList lines;
    for (const ChatMessage& entry : chatHistory_) {
        lines.append(QStringLiteral("%1: %2").arg(entry.speaker, entry.content));
    }
    return lines.join(QStringLiteral("\n\n"));
}

QString Orchestrator::notConfiguredMessage(AgentRole role) const
{
    const VexaraCore::AgentServiceKind service = taskBackends_.serviceForRole(role);
    switch (service) {
    case VexaraCore::AgentServiceKind::OpenAiHttp:
        return QStringLiteral("Assign %1 to an OpenAI profile and add an API key in Settings.")
            .arg(agentIdForRole(role));
    case VexaraCore::AgentServiceKind::OpenRouterHttp:
        return QStringLiteral("Assign %1 to an OpenRouter profile and add OPENROUTER_API_KEY.")
            .arg(agentIdForRole(role));
    case VexaraCore::AgentServiceKind::OpenClawCli:
        return QStringLiteral(
                   "Configure OpenClaw in Settings → OpenClaw CLI (executable + Ollama model). "
                   "Use \"Configure for Ollama\" in Agents if you see OpenAI key errors.")
            .arg(roleDisplayName(role));
    case VexaraCore::AgentServiceKind::GrokCli:
        return QStringLiteral("Configure grok_build.command in Settings for the Builder role.");
    case VexaraCore::AgentServiceKind::AiderCli:
        return QStringLiteral("Configure agent_execution.aider.command and model in Settings.");
    case VexaraCore::AgentServiceKind::None:
        return QStringLiteral("Assign a backend to the %1 role in Settings → Agent Roles.")
            .arg(roleDisplayName(role));
    }
    return QStringLiteral("Backend is not configured.");
}

bool Orchestrator::isHttpService(VexaraCore::AgentServiceKind service) const
{
    return service == VexaraCore::AgentServiceKind::OpenAiHttp
           || service == VexaraCore::AgentServiceKind::OpenRouterHttp;
}

void Orchestrator::appendChatMessage(const QString& role,
                                     const QString& speaker,
                                     const QString& content)
{
    ChatMessage entry;
    entry.role = role;
    entry.speaker = speaker;
    entry.content = content;
    chatHistory_.append(entry);
    emit chatHistoryChanged();
}

QString Orchestrator::chatSystemContext() const
{
    TaskContext context;
    context.projectPath = projectRoot_;
    return promptComposer_.systemContextFor(AgentRole::Builder, context);
}

void Orchestrator::onChatFinished(bool success, const QString& reply, const QString& errorSummary)
{
    const AgentSnapshot builder = registry_.agentById(QStringLiteral("builder-1"));
    const QString speaker = builder.displayName.isEmpty() ? QStringLiteral("Builder") : builder.displayName;

    if (success) {
        appendChatMessage(QStringLiteral("assistant"), speaker, reply);
        patchAgent(QStringLiteral("builder-1"), AgentState::Idle, QStringLiteral("Ready"));
        emit taskStateChanged(QStringLiteral("Chat reply received."));
    } else {
        appendChatMessage(QStringLiteral("assistant"), QStringLiteral("System"), errorSummary);
        patchAgent(QStringLiteral("builder-1"), AgentState::Blocked, errorSummary.left(120));
        emit taskStateChanged(errorSummary);
    }
}

void Orchestrator::onTaskStarted(AgentRole role, const QString& prompt)
{
    if (isDirectorPipelineEngaged()) {
        activeTaskRole_ = role;
        taskLiveOutput_.clear();
        appendHierarchicalPlanLine(QStringLiteral("--- Agent prompt ---"));
        appendHierarchicalPlanLine(prompt);
        updateAgentStatusesForStage(role, true);
        return;
    }

    activeTaskRole_ = role;
    taskLiveOutput_.clear();
    appendStageToTranscript(role, prompt);
    updateAgentStatusesForStage(role, true);

    const QString serviceLabel =
        VexaraCore::agentServiceKindLabel(taskBackends_.serviceForRole(role));
    emit taskStateChanged(QStringLiteral("%1 stage started (%2)")
                              .arg(roleDisplayName(role), serviceLabel));
}

void Orchestrator::onTaskOutputChunk(AgentRole role, const QString& chunk)
{
    if (isDirectorPipelineEngaged()) {
        appendStageOutput(role, chunk);
        return;
    }

    if (role != activeTaskRole_) {
        return;
    }
    appendStageOutput(role, chunk);
}

void Orchestrator::onTaskFinished(AgentRole role, bool success, const QString& summary)
{
    if (isDirectorPipelineEngaged()) {
        director_.forwardBackendFinished(role, success, summary);
        return;
    }

    if (role != activeTaskRole_) {
        return;
    }

    if (!summary.isEmpty()) {
        appendStageOutput(role, QStringLiteral("\n") + summary);
    }

    switch (pipelineState_.activeStage) {
    case PipelineStage::Orchestrator:
        handleOrchestratorFinished(success, summary);
        break;
    case PipelineStage::Builder:
        handleBuilderFinished(success, summary);
        break;
    case PipelineStage::Supervisor:
        handleSupervisorFinished(success, summary);
        break;
    default:
        finishPipeline(success, summary);
        break;
    }
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
    Q_UNUSED(models);
    const QVector<AgentSnapshot> agents = registry_.agents();
    for (const AgentSnapshot& agent : agents) {
        AgentSnapshot updated = agent;
        AgentRole role = AgentRole::Builder;
        if (agent.id == QStringLiteral("orchestrator-1")) {
            role = AgentRole::Orchestrator;
        } else if (agent.id == QStringLiteral("supervisor-1")) {
            role = AgentRole::Supervisor;
        }
        updated.modelId = effectiveModelDisplayForRole(role);
        registry_.updateAgent(updated);
    }
}

void Orchestrator::onVerificationFinished(bool success, const QString& summary)
{
    patchAgent(QStringLiteral("supervisor-1"),
               success ? AgentState::Idle : AgentState::Blocked,
               success ? QStringLiteral("Verification passed") : QStringLiteral("Verification failed"));
    emit verificationStateChanged(summary);
}

bool Orchestrator::isDirectorPipelineEngaged() const
{
    if (!hierarchicalPlanSessionActive_) {
        return false;
    }
    return director_.isPlannerActive() || director_.isSupervisorActive() || director_.isWorkerActive()
           || director_.isTesterActive() || director_.isReviewerActive()
           || director_.activeTaskId() > 0;
}

void Orchestrator::appendDirectorPipelineTaskHeader(const QString& userTask)
{
    pipelineState_.planTranscript =
        QStringLiteral("=== HIERARCHICAL PIPELINE ===\n") + userTask.trimmed();
    taskLiveOutput_.clear();
    onPipelinePromptReceived();
    refreshPlanFromTranscript();
}

QString Orchestrator::formattedPipelineProgress() const
{
    return cachedPipelineProgressText_;
}

void Orchestrator::resetPipelineProgress()
{
    pipelineProgressLines_.clear();
    runningPipelineProgressIndex_ = -1;
    lastDispatchedStageLabel_.clear();
    cachedPipelineProgressText_.clear();
}

void Orchestrator::publishPipelineProgress()
{
    cachedPipelineProgressText_ = formatPipelineProgressText();
    emit pipelineProgressChanged();
}

void Orchestrator::onPipelinePromptReceived()
{
    resetPipelineProgress();
    PipelineProgressLine line;
    line.kind = PipelineProgressLine::Kind::Milestone;
    line.text = QStringLiteral("Prompt received");
    line.state = QStringLiteral("done");
    pipelineProgressLines_.append(line);
    publishPipelineProgress();
}

void Orchestrator::onPipelineStageStarted(const QString& stageLabel)
{
    if (runningPipelineProgressIndex_ >= 0
        && runningPipelineProgressIndex_ < pipelineProgressLines_.size()) {
        PipelineProgressLine& running = pipelineProgressLines_[runningPipelineProgressIndex_];
        running.state = QStringLiteral("done");
        running.endedUtc = QDateTime::currentDateTimeUtc();
        runningPipelineProgressIndex_ = -1;
    }

    QString agentLabel;
    QString actionVerb;
    if (!parseStageLabel(stageLabel, &agentLabel, &actionVerb)) {
        publishPipelineProgress();
        return;
    }

    if (!pipelineProgressLines_.isEmpty()) {
        PipelineProgressLine& last = pipelineProgressLines_.last();
        if (last.kind == PipelineProgressLine::Kind::Stage && last.agentLabel == agentLabel
            && last.actionVerb == actionVerb) {
            last.state = QStringLiteral("running");
            last.startedUtc = QDateTime::currentDateTimeUtc();
            last.endedUtc = QDateTime();
            runningPipelineProgressIndex_ = pipelineProgressLines_.size() - 1;
            publishPipelineProgress();
            return;
        }
    }

    PipelineProgressLine line;
    line.kind = PipelineProgressLine::Kind::Stage;
    line.agentLabel = agentLabel;
    line.actionVerb = actionVerb;
    line.state = QStringLiteral("running");
    line.startedUtc = QDateTime::currentDateTimeUtc();
    pipelineProgressLines_.append(line);
    runningPipelineProgressIndex_ = pipelineProgressLines_.size() - 1;
    publishPipelineProgress();
}

void Orchestrator::onPipelineStageFinished(bool success)
{
    if (runningPipelineProgressIndex_ < 0
        || runningPipelineProgressIndex_ >= pipelineProgressLines_.size()) {
        publishPipelineProgress();
        return;
    }

    PipelineProgressLine& running = pipelineProgressLines_[runningPipelineProgressIndex_];
    running.state = success ? QStringLiteral("done") : QStringLiteral("failed");
    running.endedUtc = QDateTime::currentDateTimeUtc();
    runningPipelineProgressIndex_ = -1;

    if (success) {
        const QString handoff = handoffAfterStage(lastDispatchedStageLabel_);
        if (!handoff.isEmpty()) {
            PipelineProgressLine hop;
            hop.kind = PipelineProgressLine::Kind::Handoff;
            hop.text = handoff;
            hop.state = QStringLiteral("done");
            pipelineProgressLines_.append(hop);
        }
    }

    publishPipelineProgress();
}

QString Orchestrator::handoffAfterStage(const QString& stageLabel) const
{
    const QString stage = stageLabel.trimmed().toLower();
    if (stage == QStringLiteral("planning") || stage.contains(QStringLiteral("planner"))) {
        return QStringLiteral("Sent to Supervisor");
    }
    if (stage == QStringLiteral("supervisor_review") || stage.contains(QStringLiteral("supervisor"))) {
        return QStringLiteral("Sent to Builder");
    }
    if (stage == QStringLiteral("worker") || stage.contains(QStringLiteral("builder"))) {
        return QStringLiteral("Sent to Testing");
    }
    if (stage == QStringLiteral("testing")) {
        return QStringLiteral("Sent to Review");
    }
    return QString();
}

QString Orchestrator::formatPipelineProgressText() const
{
    if (pipelineProgressLines_.isEmpty()) {
        return QString();
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList rows;

    for (int index = 0; index < pipelineProgressLines_.size(); ++index) {
        const PipelineProgressLine& line = pipelineProgressLines_.at(index);
        if (line.kind == PipelineProgressLine::Kind::Milestone
            || line.kind == PipelineProgressLine::Kind::Handoff) {
            rows.append(line.text);
            continue;
        }

        const bool isRunning =
            line.state == QStringLiteral("running")
            || (index == runningPipelineProgressIndex_ && !line.endedUtc.isValid());
        const qint64 elapsedMs =
            isRunning ? line.startedUtc.msecsTo(now)
                      : (line.endedUtc.isValid() ? line.startedUtc.msecsTo(line.endedUtc) : 0);
        const QString duration = formatProgressDurationMs(elapsedMs);

        QString row = QStringLiteral("%1 — %2").arg(line.agentLabel, line.actionVerb);
        if (!duration.isEmpty()) {
            row += QStringLiteral("   %1").arg(duration);
        }
        if (isRunning) {
            row += QStringLiteral("  (running)");
        } else if (line.state == QStringLiteral("failed")) {
            row += QStringLiteral("  ✗");
        } else if (line.state == QStringLiteral("done")) {
            row += QStringLiteral("  ✓");
        }
        rows.append(row);
    }

    return rows.join(QLatin1Char('\n'));
}

void Orchestrator::appendHierarchicalPlanLine(const QString& line)
{
    if (line.isEmpty()) {
        if (!pipelineState_.planTranscript.isEmpty()) {
            pipelineState_.planTranscript += QLatin1Char('\n');
            schedulePlanRefresh();
        }
        return;
    }

    if (!pipelineState_.planTranscript.isEmpty()) {
        pipelineState_.planTranscript += QLatin1Char('\n');
    }
    pipelineState_.planTranscript += line;
    schedulePlanRefresh();
}

void Orchestrator::appendQueueStatusLine()
{
    appendHierarchicalPlanLine(
        QStringLiteral("[Queue] Pending: %1 | Root pipelines waiting: %2 | Deferred: %3")
            .arg(director_.pendingTaskCount())
            .arg(director_.pendingRootPipelineCount())
            .arg(director_.deferredTaskCount()));
}

void Orchestrator::onDirectorTaskDispatched(qint64 taskId,
                                            AgentRole role,
                                            const QString& pipelineStageId)
{
    Q_UNUSED(taskId)
    Q_UNUSED(role)
    if (!hierarchicalPlanSessionActive_) {
        return;
    }

    const QString stage = pipelineStageId.trimmed();
    lastDispatchedStageLabel_ = stage;
    onPipelineStageStarted(stage);
    appendHierarchicalPlanLine(QString());
    appendHierarchicalPlanLine(
        QStringLiteral("=== %1 (task %2) ===")
            .arg(stage.isEmpty() ? QStringLiteral("Stage") : stage.toUpper(),
                 QString::number(taskId)));
    appendHierarchicalPlanLine(QStringLiteral("Status: Running..."));

    const VexaraCore::AgentServiceKind service = taskBackends_.serviceForRole(role);
    if (service != VexaraCore::AgentServiceKind::None) {
        appendHierarchicalPlanLine(
            QStringLiteral("Backend: %1").arg(VexaraCore::agentServiceKindLabel(service)));
    }

    if (stage == QStringLiteral("testing")) {
        appendHierarchicalPlanLine(
            QStringLiteral("Note: Verification command output streams to the Terminal panel."));
    }

    taskLiveOutput_.clear();
}

void Orchestrator::onDirectorTaskCompleted(qint64 taskId, bool success, const QString& summary)
{
    Q_UNUSED(taskId)
    if (!hierarchicalPlanSessionActive_) {
        return;
    }

    onPipelineStageFinished(success);

    taskLiveOutput_.clear();
    appendHierarchicalPlanLine(
        success ? QStringLiteral("Result: SUCCESS")
                : QStringLiteral("Result: FAILED (will retry or defer per queue rules)"));

    const QString trimmedSummary = summary.trimmed();
    if (!trimmedSummary.isEmpty()) {
        appendHierarchicalPlanLine(trimmedSummary.left(3000));
    }

    appendQueueStatusLine();
}

void Orchestrator::onDirectorSystemStateChanged(const QString& summary)
{
    if (hierarchicalPlanSessionActive_ && !summary.trimmed().isEmpty()) {
        appendHierarchicalPlanLine(QStringLiteral("[Director] %1").arg(summary.trimmed()));
    }
    emitTaskStateThrottled(summary);
}

void Orchestrator::onDirectorPipelineCompleted(const QString& pipelineId,
                                               bool success,
                                               const QString& summary)
{
    if (hierarchicalPlanSessionActive_) {
        taskLiveOutput_.clear();
        appendHierarchicalPlanLine(QString());
        appendHierarchicalPlanLine(success ? QStringLiteral("=== PIPELINE COMPLETE ===")
                                           : QStringLiteral("=== PIPELINE FAILED ==="));
        appendHierarchicalPlanLine(
            QStringLiteral("Pipeline ID: %1").arg(pipelineId));
        if (!summary.trimmed().isEmpty()) {
            appendHierarchicalPlanLine(summary.trimmed().left(3000));
        }
        appendQueueStatusLine();
    }

    if (runningPipelineProgressIndex_ >= 0) {
        onPipelineStageFinished(success);
    }

    PipelineProgressLine milestone;
    milestone.kind = PipelineProgressLine::Kind::Milestone;
    milestone.state = QStringLiteral("done");
    if (success) {
        milestone.text = QStringLiteral("Pipeline complete — review changes, then Keep or Delete");
    } else {
        milestone.text = QStringLiteral("Pipeline failed");
    }
    pipelineProgressLines_.append(milestone);
    publishPipelineProgress();

    if (success) {
        QString reviewSummary =
            QStringLiteral("Pipeline finished successfully.\n\n");
        const QString taskLine = lastPipelineUserTask_.trimmed();
        if (!taskLine.isEmpty()) {
            reviewSummary += QStringLiteral("Task: %1\n\n").arg(taskLine);
        }
        if (!summary.trimmed().isEmpty()) {
            reviewSummary += summary.trimmed().left(2000);
        }
        reviewSummary +=
            QStringLiteral("\n\nOpen the changed file(s) in the editor, then click Keep to accept or "
                            "Delete to discard.");
        registry_.setPendingChangeSummary(reviewSummary);
        pendingReview_ = true;
        patchAgent(QStringLiteral("builder-1"), AgentState::WaitingReview,
                   QStringLiteral("Awaiting review"));
        patchAgent(QStringLiteral("supervisor-1"), AgentState::Idle,
                   QStringLiteral("Review complete"));
        patchAgent(QStringLiteral("orchestrator-1"), AgentState::Idle,
                   QStringLiteral("Pipeline complete"));
    } else {
        patchAgent(QStringLiteral("orchestrator-1"),
                   AgentState::Blocked,
                   summary.trimmed().isEmpty() ? QStringLiteral("Pipeline failed")
                                               : summary.trimmed().left(120));
    }

    if (director_.pendingRootPipelineCount() == 0 && !director_.isDispatching()) {
        hierarchicalPlanSessionActive_ = false;
    } else {
        appendHierarchicalPlanLine(
            QStringLiteral("[Director] Starting next queued pipeline (%1 root pipeline(s) waiting)...")
                .arg(director_.pendingRootPipelineCount()));
        appendQueueStatusLine();
    }
    emit taskStateChanged(QStringLiteral("Queued pipeline %1 finished.").arg(pipelineId));
}

} // namespace VexaraOrchestration
