#include "VexaraOrchestration/DirectorAgent.h"

#include "VexaraCore/AgentServiceKind.h"
#include "VexaraOrchestration/AgentRoleIds.h"
#include "VexaraOrchestration/PipelineArtifacts.h"
#include "VexaraOrchestration/PipelineStageIds.h"

#include <QJsonDocument>
#include <QMetaObject>
#include <QTimer>
#include <QUuid>

namespace VexaraOrchestration {

namespace {

QString pipelineKindFromPayload(const QJsonObject& payload)
{
    return payload.value(QStringLiteral("kind")).toString();
}

} // namespace

DirectorAgent::DirectorAgent(QObject* parent)
    : QObject(parent)
    , planner_(this)
    , supervisor_(this)
    , worker_(this)
    , tester_(this)
    , reviewer_(this)
    , pollTimer_(new QTimer(this))
{
    pollTimer_->setInterval(500);
    connect(pollTimer_, &QTimer::timeout, this, &DirectorAgent::poll);
    connect(&planner_, &PlannerAgent::finished, this, &DirectorAgent::handlePlannerFinished);
    connect(&supervisor_, &SupervisorAgent::finished, this, &DirectorAgent::handleSupervisorFinished);
    connect(&worker_, &WorkerAgent::finished, this, &DirectorAgent::handleWorkerFinished);
    connect(&tester_, &TesterAgent::finished, this, &DirectorAgent::handleTesterFinished);
    connect(&reviewer_, &ReviewerAgent::finished, this, &DirectorAgent::handleReviewerFinished);
    connect(&reviewer_, &ReviewerAgent::pipelineApproved, this,
            [this](const QString& pipelineId, const QString& summary) {
                finishPipelineRun(pipelineId, true, summary);
            });
}

DirectorAgent::~DirectorAgent() = default;

void DirectorAgent::configure(TaskBackendRegistry* backends,
                                AgentPromptComposer* composer,
                                AgentRegistry* registry,
                                PipelineQueue* queue,
                                const QString& projectRoot,
                                IWorkerEditorHost* editorHost,
                                ITesterCommandHost* commandHost,
                                const VexaraCore::VerificationSettings& verification)
{
    backends_ = backends;
    composer_ = composer;
    registry_ = registry;
    queue_ = queue;
    projectRoot_ = projectRoot;
    planner_.configure(backends, composer, queue, projectRoot);
    supervisor_.configure(backends, composer, queue, projectRoot);
    worker_.configure(backends, composer, queue, editorHost, projectRoot);
    tester_.configure(backends, composer, queue, commandHost, verification, projectRoot);
    reviewer_.configure(backends, composer, queue, projectRoot);
}

void DirectorAgent::start()
{
    if (!pollTimer_->isActive()) {
        pollTimer_->start();
        emit systemStateChanged(QStringLiteral("Director started."));
    }
}

void DirectorAgent::stop()
{
    if (pollTimer_->isActive()) {
        pollTimer_->stop();
        emit systemStateChanged(QStringLiteral("Director stopped."));
    }
}

bool DirectorAgent::isActive() const
{
    return pollTimer_->isActive();
}

bool DirectorAgent::isDispatching() const
{
    return activeTaskId_ > 0 || planner_.isRunning() || supervisor_.isRunning()
           || worker_.isRunning() || tester_.isRunning() || reviewer_.isRunning();
}

bool DirectorAgent::isPlannerActive() const
{
    return planner_.isRunning();
}

bool DirectorAgent::isSupervisorActive() const
{
    return supervisor_.isRunning();
}

bool DirectorAgent::isWorkerActive() const
{
    return worker_.isRunning();
}

bool DirectorAgent::isTesterActive() const
{
    return tester_.isRunning();
}

bool DirectorAgent::isReviewerActive() const
{
    return reviewer_.isRunning();
}

qint64 DirectorAgent::activeTaskId() const
{
    return activeTaskId_;
}

QString DirectorAgent::activeStage() const
{
    return activeStage_;
}

QString DirectorAgent::currentStageLabel() const
{
    auto labelForStage = [](const QString& stage) -> QString {
        if (stage == PipelineStages::planning()) {
            return QStringLiteral("Planner (writing plan)");
        }
        if (stage == PipelineStages::supervisorReview()) {
            return QStringLiteral("Supervisor review");
        }
        if (stage == PipelineStages::workerExecution()) {
            return QStringLiteral("Builder (editing files)");
        }
        if (stage == PipelineStages::testing()) {
            return QStringLiteral("Testing");
        }
        if (stage == PipelineStages::review()) {
            return QStringLiteral("Review");
        }
        return stage;
    };

    if (!activeStage_.isEmpty()) {
        return labelForStage(activeStage_);
    }
    if (planner_.isRunning()) {
        return QStringLiteral("Planner (writing plan)");
    }
    if (supervisor_.isRunning()) {
        return QStringLiteral("Supervisor review");
    }
    if (worker_.isRunning()) {
        return QStringLiteral("Builder (editing files)");
    }
    if (tester_.isRunning()) {
        return QStringLiteral("Testing");
    }
    if (reviewer_.isRunning()) {
        return QStringLiteral("Review");
    }
    return QString();
}

QString DirectorAgent::lastError() const
{
    return lastError_;
}

int DirectorAgent::pendingTaskCount() const
{
    return queue_ ? queue_->pendingCount() : 0;
}

int DirectorAgent::pendingRootPipelineCount() const
{
    return queue_ ? queue_->pendingRootPipelineCount() : 0;
}

int DirectorAgent::deferredTaskCount() const
{
    return queue_ ? queue_->deferredCount() : 0;
}

int DirectorAgent::clearDeferredTasks()
{
    if (!queue_ || !queue_->isOpen()) {
        lastError_ = QStringLiteral("Pipeline queue is not available.");
        emit systemStateChanged(lastError_);
        return 0;
    }

    const int cleared = queue_->clearAllDeferredTasks();
    if (cleared > 0) {
        emit systemStateChanged(
            QStringLiteral("Cleared %1 deferred pipeline task(s).").arg(cleared));
    } else {
        emit systemStateChanged(QStringLiteral("No deferred pipeline tasks to clear."));
    }
    return cleared;
}

int DirectorAgent::cancelPendingRootPipelines(const QString& reason)
{
    if (!queue_ || !queue_->isOpen()) {
        lastError_ = QStringLiteral("Pipeline queue is not available.");
        emit systemStateChanged(lastError_);
        return 0;
    }

    const int cancelled = queue_->cancelPendingRootPlanningTasks(reason);
    if (cancelled > 0) {
        emit systemStateChanged(
            QStringLiteral("Cancelled %1 queued pipeline task(s).").arg(cancelled));
    } else {
        emit systemStateChanged(QStringLiteral("No queued pipeline tasks to cancel."));
    }
    return cancelled;
}

void DirectorAgent::abortActivePipelineWork(const QString& reason)
{
    const QString cancelReason = reason.trimmed().isEmpty()
                                     ? QStringLiteral("Cancelled by user.")
                                     : reason.trimmed();

    if (backends_) {
        backends_->cancelRunningTasks();
    }

    const qint64 taskId = activeTaskId_;
    if (taskId > 0 && queue_) {
        queue_->markFailed(taskId, cancelReason);
        queue_->recordEvent(taskId,
                            QStringLiteral("director"),
                            QStringLiteral("cancelled"),
                            QJsonObject{{QStringLiteral("reason"), cancelReason}});
    }

    activeTaskId_ = 0;
    activeStage_.clear();
    activeRole_ = AgentRole::Orchestrator;

    patchAgentForRole(AgentRole::Orchestrator, AgentState::Idle, QStringLiteral("Cancelled"));
    patchAgentForRole(AgentRole::Supervisor, AgentState::Idle, QStringLiteral("Standby"));
    patchAgentForRole(AgentRole::Builder, AgentState::Idle, QStringLiteral("Standby"));

    emit systemStateChanged(
        QStringLiteral("Active pipeline work stopped: %1").arg(cancelReason));
}

void DirectorAgent::resumeQueue()
{
    queuePausedForPlanning_ = false;
    queuePauseReason_.clear();
    consecutivePlanningFailures_ = 0;
    emit systemStateChanged(QStringLiteral("Pipeline queue resumed."));
    scheduleTryDispatchNext();
}

bool DirectorAgent::isQueuePaused() const
{
    return queuePausedForPlanning_;
}

QString DirectorAgent::queuePauseReason() const
{
    return queuePauseReason_;
}

bool DirectorAgent::isOpenClawPlanningReady(QString* detail) const
{
    if (!backends_ || !backends_->isRoleConfigured(AgentRole::Orchestrator)) {
        if (detail) {
            *detail = QStringLiteral("Orchestrator backend is not configured for planning.");
        }
        return false;
    }
    if (backends_->serviceForRole(AgentRole::Orchestrator)
        != VexaraCore::AgentServiceKind::OpenClawCli) {
        return true;
    }
    return backends_->openClawSettings().isLocalOllamaReady(detail);
}

bool DirectorAgent::shouldPauseAfterPlanningFailure(const QString& summary) const
{
    const QString normalized = summary.trimmed().toLower();
    const bool infrastructureFailure =
        normalized.contains(QStringLiteral("timed out"))
        || normalized.contains(QStringLiteral("ollama"))
        || normalized.contains(QStringLiteral("reachable"))
        || normalized.contains(QStringLiteral("configured"));
    if (infrastructureFailure) {
        return true;
    }
    return consecutivePlanningFailures_ >= 1;
}

void DirectorAgent::pauseQueueForPlanning(const QString& reason)
{
    queuePausedForPlanning_ = true;
    queuePauseReason_ = reason.trimmed();
    const QString message =
        QStringLiteral("Pipeline queue paused: %1. Fix Ollama/OpenClaw, then click Resume queue.")
            .arg(queuePauseReason_);
    emit systemStateChanged(message);
}

void DirectorAgent::forwardBackendFinished(AgentRole role, bool success, const QString& summary)
{
    if (reviewer_.isRunning()) {
        reviewer_.handleBackendFinished(role, success, summary);
        return;
    }
    if (tester_.isRunning()) {
        tester_.handleBackendFinished(role, success, summary);
        return;
    }
    if (worker_.isRunning()) {
        worker_.handleBackendFinished(role, success, summary);
        return;
    }
    if (supervisor_.isRunning()) {
        supervisor_.handleBackendFinished(role, success, summary);
        return;
    }
    if (planner_.isRunning()) {
        planner_.handleBackendFinished(role, success, summary);
        return;
    }
    handleBackendFinished(role, success, summary);
}

qint64 DirectorAgent::enqueuePipelineStage(const QJsonObject& payload, int priority)
{
    if (!queue_ || !queue_->isOpen()) {
        lastError_ = QStringLiteral("Pipeline queue is not available.");
        return 0;
    }
    return queue_->enqueue(payload, priority);
}

qint64 DirectorAgent::enqueuePipelineStart(const QString& userTask,
                                             const VexaraCore::GrokTaskContext& context,
                                             int priority)
{
    if (!backends_) {
        lastError_ = QStringLiteral("Director backends are not configured.");
        return 0;
    }

    const QString pipelineId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject contextJson;
    contextJson.insert(QStringLiteral("project_path"), context.projectPath);
    contextJson.insert(QStringLiteral("detected_project_type"), context.detectedProjectType);
    contextJson.insert(QStringLiteral("current_file_path"), context.currentFilePath);
    contextJson.insert(QStringLiteral("selected_text"), context.selectedText);

    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("hierarchical_stage"));
    payload.insert(QStringLiteral("protocol_version"), 2);
    payload.insert(QStringLiteral("stage"), PipelineStages::planning());
    payload.insert(QStringLiteral("pipeline_id"), pipelineId);
    payload.insert(QStringLiteral("user_task"), userTask.trimmed());
    payload.insert(QStringLiteral("context"), contextJson);

    const int rootPriority = priority != 0 ? priority : PipelineQueuePriority::kRootPipeline;
    const qint64 taskId = enqueuePipelineStage(payload, rootPriority);
    if (taskId > 0) {
        queue_->recordEvent(taskId,
                            QStringLiteral("director"),
                            QStringLiteral("enqueued"),
                            QJsonObject{{QStringLiteral("pipeline_id"), pipelineId},
                                        {QStringLiteral("stage"), PipelineStages::planning()}});
        const int waitingRoots = pendingRootPipelineCount();
        emit systemStateChanged(
            QStringLiteral("Queued pipeline %1 (planning task %2, %3 root pipeline(s) waiting).")
                .arg(pipelineId)
                .arg(taskId)
                .arg(waitingRoots));
        scheduleTryDispatchNext();
    }
    return taskId;
}

bool DirectorAgent::canPoll() const
{
    if (!pollTimer_->isActive() || !queue_ || !queue_->isOpen() || !backends_) {
        return false;
    }
    if (queuePausedForPlanning_) {
        return false;
    }
    if (activeTaskId_ > 0 || planner_.isRunning() || supervisor_.isRunning() || worker_.isRunning()
        || tester_.isRunning() || reviewer_.isRunning() || backends_->anyBackendRunning()) {
        return false;
    }
    return true;
}

void DirectorAgent::poll()
{
    if (!canPoll()) {
        return;
    }
    tryDispatchNext();
}

void DirectorAgent::tryDispatchNext()
{
    PipelineTaskRecord task;
    if (!queue_->claimNextReady(task)) {
        return;
    }
    if (!dispatchTask(task)) {
        const QString errorSummary =
            lastError_.isEmpty() ? QStringLiteral("Dispatch failed.") : lastError_;
        queue_->markFailed(task.id, errorSummary);
        emit systemStateChanged(
            QStringLiteral("Dispatch failed for task %1: %2").arg(task.id).arg(errorSummary));
        const QString pipelineId = pipelineIdForTask(task.id);
        if (!pipelineId.isEmpty() && stageFromPayload(task.payload) == PipelineStages::planning()) {
            finishPipelineRun(pipelineId, false, errorSummary);
        } else {
            scheduleTryDispatchNext();
        }
    }
}

void DirectorAgent::scheduleTryDispatchNext()
{
    QMetaObject::invokeMethod(
        this,
        [this]() {
            if (canPoll()) {
                tryDispatchNext();
            }
        },
        Qt::QueuedConnection);
}

void DirectorAgent::finishPipelineRun(const QString& pipelineId,
                                      bool success,
                                      const QString& summary)
{
    if (!pipelineId.isEmpty()) {
        emit pipelineCompleted(pipelineId, success, summary);
        emit systemStateChanged(
            QStringLiteral("Pipeline %1 %2.")
                .arg(pipelineId, success ? QStringLiteral("completed") : QStringLiteral("failed")));
    }
    if (!queuePausedForPlanning_) {
        scheduleTryDispatchNext();
    }
}

QString DirectorAgent::pipelineIdForTask(qint64 taskId) const
{
    if (!queue_ || taskId <= 0) {
        return QString();
    }
    PipelineTaskRecord task;
    if (!queue_->getTask(taskId, task)) {
        return QString();
    }
    return task.payload.value(QStringLiteral("pipeline_id")).toString();
}

bool DirectorAgent::isHierarchicalPayload(const QJsonObject& payload) const
{
    return payload.value(QStringLiteral("protocol_version")).toInt() >= 2
           || payload.value(QStringLiteral("kind")).toString() == QStringLiteral("hierarchical_stage");
}

QString DirectorAgent::stageFromPayload(const QJsonObject& payload) const
{
    return payload.value(QStringLiteral("stage")).toString().trimmed().toLower();
}

bool DirectorAgent::dispatchTask(const PipelineTaskRecord& task)
{
    const QString kind = pipelineKindFromPayload(task.payload);
    if (kind == QStringLiteral("hierarchical_stage") || isHierarchicalPayload(task.payload)) {
        return dispatchHierarchicalStage(task);
    }
    if (kind == QStringLiteral("pipeline_stage")) {
        return dispatchLegacyPipelineStage(task);
    }

    lastError_ = QStringLiteral("Unsupported queue task kind: %1").arg(kind);
    return false;
}

bool DirectorAgent::writeTaskArtifact(const PipelineTaskRecord& task, const QString& status) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("pipeline_id"), task.payload.value(QStringLiteral("pipeline_id")));
    payload.insert(QStringLiteral("stage"), stageFromPayload(task.payload));
    payload.insert(QStringLiteral("user_task"), task.payload.value(QStringLiteral("user_task")));
    payload.insert(QStringLiteral("context"), task.payload.value(QStringLiteral("context")));
    payload.insert(QStringLiteral("queue_payload"), task.payload);

    const QJsonObject envelope = PipelineArtifacts::makeEnvelope(
        task.id,
        QStringLiteral("director"),
        status,
        payload,
        QStringLiteral("Task hand-off from Director to short-lived agent."));

    return PipelineQueue::writeArtifactJson(projectRoot_, task.id, PipelineArtifacts::fileTaskJson(),
                                            envelope);
}

bool DirectorAgent::dispatchHierarchicalStage(const PipelineTaskRecord& task)
{
    const QString stage = stageFromPayload(task.payload);
    if (stage == PipelineStages::planning()) {
        return dispatchPlanningStage(task);
    }
    if (stage == PipelineStages::supervisorReview()) {
        return dispatchSupervisorReviewStage(task);
    }
    if (stage == PipelineStages::workerExecution()) {
        return dispatchWorkerStage(task);
    }
    if (stage == PipelineStages::testing()) {
        return dispatchTestingStage(task);
    }
    if (stage == PipelineStages::review()) {
        return dispatchReviewStage(task);
    }

    lastError_ = QStringLiteral("Unsupported hierarchical stage: %1").arg(stage);
    return false;
}

bool DirectorAgent::dispatchPlanningStage(const PipelineTaskRecord& task)
{
    if (!backends_->isRoleConfigured(AgentRole::Orchestrator)) {
        lastError_ = QStringLiteral("Orchestrator backend is not configured for planning.");
        return false;
    }

    QString readinessDetail;
    if (!isOpenClawPlanningReady(&readinessDetail)) {
        lastError_ = readinessDetail;
        pauseQueueForPlanning(readinessDetail);
        return false;
    }

    activeTaskId_ = task.id;
    activeStage_ = PipelineStages::planning();

    if (!writeTaskArtifact(task, QStringLiteral("running"))) {
        lastError_ = QStringLiteral("Failed to write task.json for task %1.").arg(task.id);
        activeTaskId_ = 0;
        return false;
    }

    queue_->recordEvent(task.id,
                        QStringLiteral("director"),
                        QStringLiteral("dispatched"),
                        QJsonObject{{QStringLiteral("stage"), PipelineStages::planning()},
                                    {QStringLiteral("pipeline_id"),
                                     task.payload.value(QStringLiteral("pipeline_id"))}});

    patchAgentForRole(AgentRole::Orchestrator, AgentState::Running, QStringLiteral("Planning"));
    if (!planner_.execute(task)) {
        lastError_ = planner_.lastError();
        activeTaskId_ = 0;
        return false;
    }

    emit taskDispatched(task.id, AgentRole::Orchestrator, activeStage_);
    return true;
}

bool DirectorAgent::dispatchSupervisorReviewStage(const PipelineTaskRecord& task)
{
    if (!backends_->isRoleConfigured(AgentRole::Supervisor)) {
        lastError_ = QStringLiteral("Supervisor backend is not configured.");
        return false;
    }

    const qint64 planTaskId =
        PipelineArtifacts::payloadTaskRef(task.payload, QStringLiteral("plan_task_id"));
    if (planTaskId <= 0) {
        lastError_ = QStringLiteral(
            "Supervisor review task %1 is missing a valid plan_task_id (got %2).")
                         .arg(task.id)
                         .arg(planTaskId);
        return false;
    }

    activeTaskId_ = task.id;
    activeStage_ = PipelineStages::supervisorReview();

    if (!writeTaskArtifact(task, QStringLiteral("running"))) {
        lastError_ = QStringLiteral("Failed to write task.json for task %1.").arg(task.id);
        activeTaskId_ = 0;
        return false;
    }

    queue_->recordEvent(task.id,
                        QStringLiteral("director"),
                        QStringLiteral("dispatched"),
                        QJsonObject{{QStringLiteral("stage"), PipelineStages::supervisorReview()},
                                    {QStringLiteral("pipeline_id"),
                                     task.payload.value(QStringLiteral("pipeline_id"))},
                                    {QStringLiteral("plan_task_id"), planTaskId}});

    patchAgentForRole(AgentRole::Supervisor, AgentState::Running, QStringLiteral("Reviewing plan"));
    if (!supervisor_.execute(task)) {
        lastError_ = supervisor_.lastError();
        activeTaskId_ = 0;
        activeStage_.clear();
        return false;
    }

    emit taskDispatched(task.id, AgentRole::Supervisor, activeStage_);
    return true;
}

bool DirectorAgent::dispatchWorkerStage(const PipelineTaskRecord& task)
{
    activeTaskId_ = task.id;
    activeStage_ = PipelineStages::workerExecution();

    if (!writeTaskArtifact(task, QStringLiteral("running"))) {
        lastError_ = QStringLiteral("Failed to write task.json for task %1.").arg(task.id);
        activeTaskId_ = 0;
        return false;
    }

    queue_->recordEvent(task.id,
                        QStringLiteral("director"),
                        QStringLiteral("dispatched"),
                        QJsonObject{{QStringLiteral("stage"), PipelineStages::workerExecution()},
                                    {QStringLiteral("pipeline_id"),
                                     task.payload.value(QStringLiteral("pipeline_id"))},
                                    {QStringLiteral("chosen_backend"),
                                     task.payload.value(QStringLiteral("chosen_backend"))}});

    patchAgentForRole(AgentRole::Builder, AgentState::Running, QStringLiteral("Worker executing"));
    if (!worker_.execute(task)) {
        lastError_ = worker_.lastError();
        activeTaskId_ = 0;
        activeStage_.clear();
        return false;
    }

    emit taskDispatched(task.id, AgentRole::Builder, activeStage_);
    return true;
}

bool DirectorAgent::dispatchTestingStage(const PipelineTaskRecord& task)
{
    activeTaskId_ = task.id;
    activeStage_ = PipelineStages::testing();

    if (!writeTaskArtifact(task, QStringLiteral("pending"))) {
        lastError_ = QStringLiteral("Failed to write task.json for task %1.").arg(task.id);
        activeTaskId_ = 0;
        activeStage_.clear();
        return false;
    }

    queue_->recordEvent(task.id,
                        QStringLiteral("director"),
                        QStringLiteral("dispatch_tester"),
                        QJsonObject{{QStringLiteral("stage"), PipelineStages::testing()},
                                    {QStringLiteral("worker_task_id"),
                                     task.payload.value(QStringLiteral("worker_task_id"))}});

    patchAgentForRole(AgentRole::Supervisor, AgentState::Running, QStringLiteral("Running tests"));
    if (!tester_.execute(task)) {
        lastError_ = tester_.lastError();
        activeTaskId_ = 0;
        activeStage_.clear();
        return false;
    }

    emit taskDispatched(task.id, AgentRole::Supervisor, activeStage_);
    return true;
}

bool DirectorAgent::dispatchReviewStage(const PipelineTaskRecord& task)
{
    activeTaskId_ = task.id;
    activeStage_ = PipelineStages::review();

    if (!writeTaskArtifact(task, QStringLiteral("pending"))) {
        lastError_ = QStringLiteral("Failed to write task.json for task %1.").arg(task.id);
        activeTaskId_ = 0;
        activeStage_.clear();
        return false;
    }

    queue_->recordEvent(task.id,
                        QStringLiteral("director"),
                        QStringLiteral("dispatch_reviewer"),
                        QJsonObject{{QStringLiteral("stage"), PipelineStages::review()},
                                    {QStringLiteral("testing_task_id"),
                                     task.payload.value(QStringLiteral("testing_task_id"))}});

    patchAgentForRole(AgentRole::Supervisor, AgentState::Running, QStringLiteral("Final review"));
    if (!reviewer_.execute(task)) {
        lastError_ = reviewer_.lastError();
        activeTaskId_ = 0;
        activeStage_.clear();
        return false;
    }

    emit taskDispatched(task.id, AgentRole::Supervisor, activeStage_);
    return true;
}

void DirectorAgent::handlePlannerFinished(qint64 taskId, bool success, const QString& summary)
{
    if (taskId != activeTaskId_) {
        return;
    }

    activeTaskId_ = 0;
    activeStage_.clear();

    if (!success) {
        ++consecutivePlanningFailures_;
        patchAgentForRole(AgentRole::Orchestrator, AgentState::Blocked, summary.left(120));
        emit taskCompleted(taskId, false, summary);
        if (shouldPauseAfterPlanningFailure(summary)) {
            pauseQueueForPlanning(summary);
        } else {
            emit systemStateChanged(
                QStringLiteral("Planning task %1 failed.").arg(taskId));
        }
        const QString pipelineId = pipelineIdForTask(taskId);
        if (!pipelineId.isEmpty()) {
            finishPipelineRun(pipelineId, false, summary);
        } else if (!queuePausedForPlanning_) {
            scheduleTryDispatchNext();
        }
        return;
    }

    consecutivePlanningFailures_ = 0;
    patchAgentForRole(AgentRole::Orchestrator, AgentState::Idle, QStringLiteral("Plan complete"));
    emit taskCompleted(taskId, true, summary);
    emit systemStateChanged(
        QStringLiteral("Planning task %1 complete; supervisor stage enqueued.").arg(taskId));
    scheduleTryDispatchNext();
}

void DirectorAgent::handleSupervisorFinished(qint64 taskId, bool success, const QString& summary)
{
    if (taskId != activeTaskId_) {
        return;
    }

    activeTaskId_ = 0;
    activeStage_.clear();

    if (!success) {
        patchAgentForRole(AgentRole::Supervisor, AgentState::Blocked, summary.left(120));
        emit taskCompleted(taskId, false, summary);
        emit systemStateChanged(
            QStringLiteral("Supervisor task %1 failed; perpetual motion continues.").arg(taskId));
        return;
    }

    patchAgentForRole(AgentRole::Supervisor, AgentState::Idle, QStringLiteral("Review complete"));
    emit taskCompleted(taskId, true, summary);
    emit systemStateChanged(summary);
    scheduleTryDispatchNext();
}

void DirectorAgent::handleWorkerFinished(qint64 taskId, bool success, const QString& summary)
{
    if (taskId != activeTaskId_) {
        return;
    }

    activeTaskId_ = 0;
    activeStage_.clear();

    if (!success) {
        patchAgentForRole(AgentRole::Builder, AgentState::Blocked, summary.left(120));
        emit taskCompleted(taskId, false, summary);
        emit systemStateChanged(
            QStringLiteral("Worker task %1 failed; perpetual motion continues.").arg(taskId));
        return;
    }

    patchAgentForRole(AgentRole::Builder, AgentState::WaitingReview, QStringLiteral("Awaiting review"));
    emit taskCompleted(taskId, true, summary);
    emit systemStateChanged(
        QStringLiteral("Worker task %1 complete; testing stage enqueued.").arg(taskId));
    scheduleTryDispatchNext();
}

void DirectorAgent::handleTesterFinished(qint64 taskId, bool success, const QString& summary)
{
    if (taskId != activeTaskId_) {
        return;
    }

    activeTaskId_ = 0;
    activeStage_.clear();

    if (!success) {
        patchAgentForRole(AgentRole::Supervisor, AgentState::Blocked, summary.left(120));
        emit taskCompleted(taskId, false, summary);
        emit systemStateChanged(
            QStringLiteral("Testing task %1 failed; perpetual motion continues.").arg(taskId));
        return;
    }

    patchAgentForRole(AgentRole::Supervisor, AgentState::Idle, QStringLiteral("Tests passed"));
    emit taskCompleted(taskId, true, summary);
    emit systemStateChanged(
        QStringLiteral("Testing task %1 complete; review stage enqueued.").arg(taskId));
    scheduleTryDispatchNext();
}

void DirectorAgent::handleReviewerFinished(qint64 taskId, bool success, const QString& summary)
{
    if (taskId != activeTaskId_) {
        return;
    }

    activeTaskId_ = 0;
    activeStage_.clear();

    if (!success) {
        patchAgentForRole(AgentRole::Supervisor, AgentState::Blocked, summary.left(120));
        emit taskCompleted(taskId, false, summary);
        emit systemStateChanged(
            QStringLiteral("Review task %1 failed; perpetual motion continues.").arg(taskId));
        const QString pipelineId = pipelineIdForTask(taskId);
        if (!pipelineId.isEmpty()) {
            finishPipelineRun(pipelineId, false, summary);
        } else {
            scheduleTryDispatchNext();
        }
        return;
    }

    patchAgentForRole(AgentRole::Supervisor, AgentState::Idle, QStringLiteral("Review complete"));
    emit taskCompleted(taskId, true, summary);
    emit systemStateChanged(summary);
    scheduleTryDispatchNext();
}

TaskContext DirectorAgent::taskContextFromPayload(const QJsonObject& payload) const
{
    TaskContext context;
    const QJsonObject contextJson = payload.value(QStringLiteral("context")).toObject();
    context.projectPath = contextJson.value(QStringLiteral("project_path")).toString(projectRoot_);
    context.detectedProjectType =
        contextJson.value(QStringLiteral("detected_project_type")).toString();
    context.currentFilePath = contextJson.value(QStringLiteral("current_file_path")).toString();
    context.selectedText = contextJson.value(QStringLiteral("selected_text")).toString();
    if (context.projectPath.isEmpty()) {
        context.projectPath = projectRoot_;
    }
    return context;
}

AgentRole DirectorAgent::roleFromPayload(const QJsonObject& payload) const
{
    const QString role = payload.value(QStringLiteral("role")).toString().trimmed().toLower();
    if (role == QStringLiteral("orchestrator")) {
        return AgentRole::Orchestrator;
    }
    if (role == QStringLiteral("supervisor")) {
        return AgentRole::Supervisor;
    }
    return AgentRole::Builder;
}

QString DirectorAgent::composePromptForPayload(const QJsonObject& payload) const
{
    if (!composer_) {
        return payload.value(QStringLiteral("user_task")).toString();
    }

    const AgentRole role = roleFromPayload(payload);
    const TaskContext context = taskContextFromPayload(payload);
    const QString userTask = payload.value(QStringLiteral("user_task")).toString();
    const QString orchestratorPlan = payload.value(QStringLiteral("orchestrator_plan")).toString();
    const QString builderResult = payload.value(QStringLiteral("builder_result")).toString();

    switch (role) {
    case AgentRole::Orchestrator:
        return composer_->composeUserPrompt(AgentRole::Orchestrator, userTask, context);
    case AgentRole::Supervisor:
        return composer_->composeSupervisorReviewPrompt(userTask, builderResult, context);
    case AgentRole::Builder:
        return composer_->composeBuilderPromptWithPlan(userTask, orchestratorPlan, context);
    }
    return userTask;
}

void DirectorAgent::patchAgentForRole(AgentRole role, AgentState state, const QString& statusLine)
{
    if (!registry_) {
        return;
    }
    AgentSnapshot agent = registry_->agentById(agentIdForRole(role));
    if (agent.id.isEmpty()) {
        return;
    }
    agent.state = state;
    agent.statusLine = statusLine;
    registry_->updateAgent(agent);
}

bool DirectorAgent::dispatchLegacyPipelineStage(const PipelineTaskRecord& task)
{
    const AgentRole role = roleFromPayload(task.payload);
    ITaskBackend* backend = backends_->backendForRole(role);
    if (!backend) {
        lastError_ = QStringLiteral("No backend assigned for %1.").arg(roleDisplayName(role));
        return false;
    }
    if (!backends_->isRoleConfigured(role)) {
        if (role == AgentRole::Orchestrator || role == AgentRole::Supervisor) {
            queue_->markCompleted(task.id);
            queue_->recordEvent(task.id,
                                QStringLiteral("director"),
                                QStringLiteral("skipped"),
                                QJsonObject{{QStringLiteral("role"), roleDisplayName(role)}});
            enqueueLegacyFollowUpStage(role, QString(), task);
            return true;
        }
        lastError_ = QStringLiteral("%1 backend is not configured.").arg(roleDisplayName(role));
        return false;
    }

    activeTaskId_ = task.id;
    activeRole_ = role;
    activeStage_.clear();
    activePipeline_.pipelineId = task.payload.value(QStringLiteral("pipeline_id")).toString();
    activePipeline_.userTask = task.payload.value(QStringLiteral("user_task")).toString();
    activePipeline_.context = taskContextFromPayload(task.payload);
    activePipeline_.orchestratorPlan =
        task.payload.value(QStringLiteral("orchestrator_plan")).toString();
    activePipeline_.builderResult = task.payload.value(QStringLiteral("builder_result")).toString();
    activePipeline_.runOrchestrator =
        task.payload.value(QStringLiteral("run_orchestrator")).toBool(false);
    activePipeline_.runSupervisor =
        task.payload.value(QStringLiteral("run_supervisor")).toBool(false);

    const QString prompt = composePromptForPayload(task.payload);
    PipelineQueue::writeArtifactJson(
        projectRoot_,
        task.id,
        QStringLiteral("dispatch.json"),
        PipelineArtifacts::makeEnvelope(task.id,
                                        QStringLiteral("director"),
                                        QStringLiteral("running"),
                                        QJsonObject{{QStringLiteral("role"), roleDisplayName(role)},
                                                     {QStringLiteral("prompt"), prompt},
                                                     {QStringLiteral("payload"), task.payload}},
                                        QStringLiteral("Legacy pipeline dispatch.")));

    queue_->recordEvent(task.id,
                        roleDisplayName(role),
                        QStringLiteral("dispatched"),
                        QJsonObject{{QStringLiteral("pipeline_id"), activePipeline_.pipelineId}});

    patchAgentForRole(role, AgentState::Running, QStringLiteral("Director dispatch"));
    backend->executeTask(role, prompt, activePipeline_.context);
    emit taskDispatched(task.id, role, activeStage_);
    return true;
}

void DirectorAgent::handleBackendFinished(AgentRole role, bool success, const QString& summary)
{
    if (activeTaskId_ <= 0 || role != activeRole_ || !activeStage_.isEmpty()) {
        return;
    }

    const qint64 finishedTaskId = activeTaskId_;
    PipelineTaskRecord task;
    if (!queue_->getTask(finishedTaskId, task)) {
        activeTaskId_ = 0;
        return;
    }

    PipelineQueue::writeArtifactJson(
        projectRoot_,
        finishedTaskId,
        QStringLiteral("result.json"),
        PipelineArtifacts::makeEnvelope(finishedTaskId,
                                        roleDisplayName(role),
                                        success ? QStringLiteral("completed")
                                                : QStringLiteral("failed"),
                                        QJsonObject{{QStringLiteral("summary"), summary},
                                                    {QStringLiteral("role"), roleDisplayName(role)}},
                                        QString()));

    queue_->recordEvent(finishedTaskId,
                        roleDisplayName(role),
                        success ? QStringLiteral("completed") : QStringLiteral("failed"),
                        QJsonObject{{QStringLiteral("summary"), summary.left(500)}});

    if (!success) {
        queue_->markFailed(finishedTaskId, summary);
        patchAgentForRole(role, AgentState::Blocked, summary.left(120));
        emit taskCompleted(finishedTaskId, false, summary);
        emit systemStateChanged(
            QStringLiteral("Task %1 failed; perpetual motion continues.").arg(finishedTaskId));
        activeTaskId_ = 0;
        scheduleTryDispatchNext();
        return;
    }

    queue_->markCompleted(finishedTaskId);
    patchAgentForRole(role, AgentState::Idle, QStringLiteral("Stage complete"));
    emit taskCompleted(finishedTaskId, true, summary);
    enqueueLegacyFollowUpStage(role, summary, task);
    activeTaskId_ = 0;
    scheduleTryDispatchNext();
}

void DirectorAgent::enqueueLegacyFollowUpStage(AgentRole completedRole,
                                               const QString& summary,
                                               const PipelineTaskRecord& task)
{
    ActivePipelineContext pipeline;
    pipeline.pipelineId = task.payload.value(QStringLiteral("pipeline_id")).toString();
    pipeline.userTask = task.payload.value(QStringLiteral("user_task")).toString();
    pipeline.context = taskContextFromPayload(task.payload);
    pipeline.orchestratorPlan = task.payload.value(QStringLiteral("orchestrator_plan")).toString();
    pipeline.builderResult = task.payload.value(QStringLiteral("builder_result")).toString();
    pipeline.runOrchestrator = task.payload.value(QStringLiteral("run_orchestrator")).toBool(false);
    pipeline.runSupervisor = task.payload.value(QStringLiteral("run_supervisor")).toBool(false);

    if (completedRole == AgentRole::Orchestrator) {
        pipeline.orchestratorPlan = summary;
    } else if (completedRole == AgentRole::Builder) {
        pipeline.builderResult = summary;
    }

    AgentRole nextRole = AgentRole::Builder;
    bool pipelineDone = false;

    if (completedRole == AgentRole::Orchestrator) {
        nextRole = AgentRole::Builder;
    } else if (completedRole == AgentRole::Builder) {
        if (pipeline.runSupervisor &&
            backends_ &&
            TaskPipelineHooks::shouldRunSupervisor(*backends_)) {
            nextRole = AgentRole::Supervisor;
        } else {
            pipelineDone = true;
        }
    } else if (completedRole == AgentRole::Supervisor) {
        pipelineDone = true;
    }

    if (pipelineDone) {
        finishPipelineRun(pipeline.pipelineId, true, summary);
        return;
    }

    const QJsonObject nextPayload = buildLegacyStagePayload(pipeline, nextRole);
    const qint64 nextTaskId =
        enqueuePipelineStage(nextPayload, PipelineQueuePriority::kIntraPipelineStage);
    if (nextTaskId > 0) {
        queue_->recordEvent(nextTaskId,
                            QStringLiteral("director"),
                            QStringLiteral("stage_enqueued"),
                            QJsonObject{{QStringLiteral("pipeline_id"), pipeline.pipelineId},
                                        {QStringLiteral("role"), roleDisplayName(nextRole)}});
        emit systemStateChanged(
            QStringLiteral("Queued %1 stage for pipeline %2.")
                .arg(roleDisplayName(nextRole), pipeline.pipelineId));
    }
}

QJsonObject DirectorAgent::buildLegacyStagePayload(const ActivePipelineContext& pipeline,
                                                   AgentRole role) const
{
    QJsonObject contextJson;
    contextJson.insert(QStringLiteral("project_path"), pipeline.context.projectPath);
    contextJson.insert(QStringLiteral("detected_project_type"),
                       pipeline.context.detectedProjectType);
    contextJson.insert(QStringLiteral("current_file_path"), pipeline.context.currentFilePath);
    contextJson.insert(QStringLiteral("selected_text"), pipeline.context.selectedText);

    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("pipeline_stage"));
    payload.insert(QStringLiteral("pipeline_id"), pipeline.pipelineId);
    payload.insert(QStringLiteral("user_task"), pipeline.userTask);
    payload.insert(QStringLiteral("role"), roleDisplayName(role).toLower());
    payload.insert(QStringLiteral("context"), contextJson);
    payload.insert(QStringLiteral("run_orchestrator"), pipeline.runOrchestrator);
    payload.insert(QStringLiteral("run_supervisor"), pipeline.runSupervisor);
    payload.insert(QStringLiteral("orchestrator_plan"), pipeline.orchestratorPlan);
    payload.insert(QStringLiteral("builder_result"), pipeline.builderResult);
    return payload;
}

} // namespace VexaraOrchestration
