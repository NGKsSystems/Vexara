#include "VexaraOrchestration/SupervisorAgent.h"

#include "VexaraCore/AgentServiceKind.h"
#include "VexaraOrchestration/AgentRoleIds.h"
#include "VexaraOrchestration/ITaskBackend.h"
#include "VexaraOrchestration/PipelineArtifacts.h"
#include "VexaraOrchestration/PipelineStageIds.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace VexaraOrchestration {

namespace {

constexpr double kMinApprovalConfidence = 0.55;

} // namespace

SupervisorAgent::SupervisorAgent(QObject* parent)
    : QObject(parent)
{
}

void SupervisorAgent::configure(TaskBackendRegistry* backends,
                                  AgentPromptComposer* composer,
                                  PipelineQueue* queue,
                                  const QString& projectRoot)
{
    backends_ = backends;
    composer_ = composer;
    queue_ = queue;
    projectRoot_ = projectRoot;
}

bool SupervisorAgent::isRunning() const
{
    return activeTaskId_ > 0;
}

qint64 SupervisorAgent::activeTaskId() const
{
    return activeTaskId_;
}

QString SupervisorAgent::lastError() const
{
    return lastError_;
}

TaskContext SupervisorAgent::contextFromPayload(const QJsonObject& payload) const
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

bool SupervisorAgent::loadPlanArtifact(qint64 planTaskId, QJsonObject& planEnvelopeOut) const
{
    if (planTaskId <= 0) {
        return false;
    }
    return PipelineQueue::readArtifactJson(projectRoot_, planTaskId,
                                           PipelineArtifacts::filePlanJson(), planEnvelopeOut);
}

bool SupervisorAgent::isBackendConfigured(VexaraCore::AgentServiceKind kind) const
{
    if (!backends_) {
        return false;
    }

    ITaskBackend* backend = backends_->backendForService(kind);
    if (!backend) {
        return false;
    }

    switch (kind) {
    case VexaraCore::AgentServiceKind::OpenAiHttp:
    case VexaraCore::AgentServiceKind::OpenRouterHttp:
        return backends_->isRoleConfigured(AgentRole::Builder)
               && backends_->serviceForRole(AgentRole::Builder) == kind;
    case VexaraCore::AgentServiceKind::GrokCli:
        return backends_->isRoleConfigured(AgentRole::Builder)
               && backends_->serviceForRole(AgentRole::Builder) == kind;
    case VexaraCore::AgentServiceKind::AiderCli:
        return backend->isConfigured();
    default:
        return backend->isConfigured();
    }
}

QJsonArray SupervisorAgent::availableWorkerBackends() const
{
    QJsonArray catalog;

    auto appendEntry = [&](VexaraCore::AgentServiceKind kind,
                           const QString& capabilities,
                           bool preferredForCode) {
        if (!isBackendConfigured(kind)) {
            return;
        }
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), VexaraCore::agentServiceKindToString(kind));
        entry.insert(QStringLiteral("label"), VexaraCore::agentServiceKindLabel(kind));
        entry.insert(QStringLiteral("capabilities"), capabilities);
        entry.insert(QStringLiteral("preferred_for_code_changes"), preferredForCode);
        catalog.append(entry);
    };

    appendEntry(VexaraCore::AgentServiceKind::AiderCli,
                QStringLiteral("execution,file_io,git,local,ollama"),
                true);
    appendEntry(VexaraCore::AgentServiceKind::GrokCli,
                QStringLiteral("execution,file_io,cli"),
                false);
    appendEntry(VexaraCore::AgentServiceKind::OpenAiHttp,
                QStringLiteral("execution,text_only,http"),
                false);
    appendEntry(VexaraCore::AgentServiceKind::OpenRouterHttp,
                QStringLiteral("execution,text_only,http"),
                false);

    return catalog;
}

bool SupervisorAgent::execute(const PipelineTaskRecord& task)
{
    if (isRunning()) {
        lastError_ = QStringLiteral("Supervisor is already running.");
        return false;
    }
    if (!backends_ || !composer_ || !queue_) {
        lastError_ = QStringLiteral("Supervisor is not configured.");
        return false;
    }

    ITaskBackend* backend = backends_->backendForRole(AgentRole::Supervisor);
    if (!backend || !backends_->isRoleConfigured(AgentRole::Supervisor)) {
        lastError_ = QStringLiteral("Supervisor backend is not configured.");
        return false;
    }
    if (backend->isRunning()) {
        lastError_ = QStringLiteral("Supervisor backend is busy.");
        return false;
    }

    activePlanTaskId_ = PipelineArtifacts::payloadTaskRef(task.payload, QStringLiteral("plan_task_id"));
    QJsonObject planEnvelope;
    if (!loadPlanArtifact(activePlanTaskId_, planEnvelope)) {
        lastError_ = QStringLiteral("Could not read plan.json for task %1.").arg(activePlanTaskId_);
        return false;
    }

    activeTaskId_ = task.id;
    activeTaskPayload_ = task.payload;
    activePlanPayload_ = planEnvelope.value(QStringLiteral("payload")).toObject();
    activePipelineId_ = task.payload.value(QStringLiteral("pipeline_id")).toString();
    activeUserTask_ = task.payload.value(QStringLiteral("user_task")).toString();
    activeContext_ = contextFromPayload(task.payload);

    const QJsonArray backendCatalog = availableWorkerBackends();
    if (backendCatalog.isEmpty()) {
        lastError_ = QStringLiteral("No Worker backends are configured for routing.");
        activeTaskId_ = 0;
        return false;
    }

    const QString prompt = composer_->composeSupervisorPlanReviewPrompt(
        activeUserTask_, activePlanPayload_, backendCatalog, activeContext_);

    queue_->recordEvent(task.id,
                        QStringLiteral("supervisor"),
                        QStringLiteral("started"),
                        QJsonObject{{QStringLiteral("pipeline_id"), activePipelineId_},
                                    {QStringLiteral("plan_task_id"), activePlanTaskId_}});

    emit started(task.id);
    backend->executeTask(AgentRole::Supervisor, prompt, activeContext_);
    return true;
}

QString SupervisorAgent::extractJsonBody(const QString& rawResponse) const
{
    const QString trimmed = rawResponse.trimmed();
    if (trimmed.startsWith(QLatin1Char('{')) && trimmed.endsWith(QLatin1Char('}'))) {
        return trimmed;
    }

    static const QRegularExpression fencePattern(
        QStringLiteral("```(?:json)?\\s*([\\s\\S]*?)```"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = fencePattern.match(trimmed);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    const int start = trimmed.indexOf(QLatin1Char('{'));
    const int end = trimmed.lastIndexOf(QLatin1Char('}'));
    if (start >= 0 && end > start) {
        return trimmed.mid(start, end - start + 1);
    }

    return trimmed;
}

QString SupervisorAgent::normalizeBackendChoice(const QString& choice,
                                                  const QJsonArray& catalog) const
{
    const QString normalized = choice.trimmed().toLower();
    for (const QJsonValue& value : catalog) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString().toLower();
        if (id == normalized) {
            return entry.value(QStringLiteral("id")).toString();
        }
    }

    for (const QJsonValue& value : catalog) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("preferred_for_code_changes")).toBool(false)) {
            return entry.value(QStringLiteral("id")).toString();
        }
    }

    if (!catalog.isEmpty()) {
        return catalog.first().toObject().value(QStringLiteral("id")).toString();
    }

    return VexaraCore::agentServiceKindToString(VexaraCore::AgentServiceKind::GrokCli);
}

SupervisorAgent::SupervisorDecision SupervisorAgent::parseDecision(const QString& rawResponse) const
{
    SupervisorDecision decision;
    const QString jsonBody = extractJsonBody(rawResponse);
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBody.toUtf8());

    QJsonObject parsed = doc.isObject() ? doc.object() : QJsonObject();
    if (parsed.isEmpty()) {
        decision.decision = QStringLiteral("rework");
        decision.confidence = 0.0;
        decision.reasoning =
            QStringLiteral("Supervisor response was not valid JSON; requesting planner rework.");
        decision.approvedPlan = activePlanPayload_;
        decision.workerInstructions = rawResponse.trimmed();
        return decision;
    }

    decision.decision = parsed.value(QStringLiteral("decision")).toString().trimmed().toLower();
    decision.confidence = parsed.value(QStringLiteral("confidence")).toDouble(0.0);
    decision.reasoning = parsed.value(QStringLiteral("reasoning")).toString();
    decision.chosenBackend =
        normalizeBackendChoice(parsed.value(QStringLiteral("chosen_backend")).toString(),
                               availableWorkerBackends());
    decision.approvedPlan = parsed.value(QStringLiteral("approved_plan")).toObject();
    decision.planIssues = parsed.value(QStringLiteral("plan_issues")).toArray();
    decision.riskAssessment = parsed.value(QStringLiteral("risk_assessment")).toString();
    decision.workerInstructions = parsed.value(QStringLiteral("worker_instructions")).toString();

    if (decision.approvedPlan.isEmpty()) {
        decision.approvedPlan = activePlanPayload_;
    }
    if (decision.decision.isEmpty()) {
        decision.decision = decision.confidence >= kMinApprovalConfidence
                                ? QStringLiteral("approve")
                                : QStringLiteral("rework");
    }

    return decision;
}

QJsonObject SupervisorAgent::buildApprovedPlanEnvelope(qint64 taskId,
                                                       const SupervisorDecision& decision) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("pipeline_id"), activePipelineId_);
    payload.insert(QStringLiteral("user_task"), activeUserTask_);
    payload.insert(QStringLiteral("source_plan_task_id"), activePlanTaskId_);
    payload.insert(QStringLiteral("approved_plan"), decision.approvedPlan);
    payload.insert(QStringLiteral("chosen_backend"), decision.chosenBackend);
    payload.insert(QStringLiteral("confidence"), decision.confidence);
    payload.insert(QStringLiteral("reasoning"), decision.reasoning);
    payload.insert(QStringLiteral("worker_instructions"), decision.workerInstructions);
    payload.insert(QStringLiteral("plan_issues"), decision.planIssues);
    payload.insert(QStringLiteral("risk_assessment"), decision.riskAssessment);

    return PipelineArtifacts::makeEnvelope(taskId,
                                           QStringLiteral("supervisor"),
                                           QStringLiteral("completed"),
                                           payload,
                                           decision.reasoning);
}

QJsonObject SupervisorAgent::buildWorkerStagePayload(const QJsonObject& taskPayload,
                                                       qint64 supervisorTaskId,
                                                       const SupervisorDecision& decision) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("hierarchical_stage"));
    payload.insert(QStringLiteral("protocol_version"), 2);
    payload.insert(QStringLiteral("stage"), PipelineStages::workerExecution());
    payload.insert(QStringLiteral("pipeline_id"), taskPayload.value(QStringLiteral("pipeline_id")));
    payload.insert(QStringLiteral("user_task"), taskPayload.value(QStringLiteral("user_task")));
    payload.insert(QStringLiteral("context"), taskPayload.value(QStringLiteral("context")));
    payload.insert(QStringLiteral("plan_task_id"), activePlanTaskId_);
    payload.insert(QStringLiteral("supervisor_task_id"), supervisorTaskId);
    payload.insert(QStringLiteral("approved_plan_file"), PipelineArtifacts::fileApprovedPlanJson());
    payload.insert(QStringLiteral("chosen_backend"), decision.chosenBackend);
    payload.insert(QStringLiteral("worker_instructions"), decision.workerInstructions);

    const QJsonArray targetFiles = decision.approvedPlan.value(QStringLiteral("target_files")).toArray();
    if (!targetFiles.isEmpty()) {
        payload.insert(QStringLiteral("target_files"), targetFiles);
    }
    return payload;
}

QJsonObject SupervisorAgent::buildPlanningReworkPayload(const QJsonObject& taskPayload,
                                                          qint64 supervisorTaskId,
                                                          const SupervisorDecision& decision) const
{
    QString reworkTask = taskPayload.value(QStringLiteral("user_task")).toString();
    if (!decision.reasoning.trimmed().isEmpty()) {
        reworkTask += QStringLiteral("\n\nSupervisor rework feedback:\n") + decision.reasoning;
    }
  if (!decision.workerInstructions.trimmed().isEmpty()) {
        reworkTask += QStringLiteral("\n\nRefinement notes:\n") + decision.workerInstructions;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("hierarchical_stage"));
    payload.insert(QStringLiteral("protocol_version"), 2);
    payload.insert(QStringLiteral("stage"), PipelineStages::planning());
    payload.insert(QStringLiteral("pipeline_id"), taskPayload.value(QStringLiteral("pipeline_id")));
    payload.insert(QStringLiteral("user_task"), reworkTask.trimmed());
    payload.insert(QStringLiteral("context"), taskPayload.value(QStringLiteral("context")));
    payload.insert(QStringLiteral("rework_from_supervisor"), true);
    payload.insert(QStringLiteral("supervisor_task_id"), supervisorTaskId);
    payload.insert(QStringLiteral("supervisor_feedback"), decision.reasoning);
    return payload;
}

void SupervisorAgent::enqueuePlanningRework(qint64 supervisorTaskId,
                                            const SupervisorDecision& decision)
{
    const QJsonObject payload =
        buildPlanningReworkPayload(activeTaskPayload_, supervisorTaskId, decision);
    const qint64 nextTaskId =
        queue_->enqueue(payload, PipelineQueuePriority::kIntraPipelineStage);
    if (nextTaskId > 0) {
        queue_->recordEvent(nextTaskId,
                            QStringLiteral("supervisor"),
                            QStringLiteral("rework_enqueued"),
                            QJsonObject{{QStringLiteral("stage"), PipelineStages::planning()},
                                        {QStringLiteral("pipeline_id"), activePipelineId_},
                                        {QStringLiteral("confidence"), decision.confidence}});
        emit reworkRequested(supervisorTaskId, activePipelineId_, decision.reasoning);
    }
}

void SupervisorAgent::enqueueWorkerStage(qint64 supervisorTaskId, const SupervisorDecision& decision)
{
    const QJsonObject payload =
        buildWorkerStagePayload(activeTaskPayload_, supervisorTaskId, decision);
    const qint64 nextTaskId =
        queue_->enqueue(payload, PipelineQueuePriority::kIntraPipelineStage);
    if (nextTaskId > 0) {
        queue_->recordEvent(nextTaskId,
                            QStringLiteral("supervisor"),
                            QStringLiteral("handoff_enqueued"),
                            QJsonObject{{QStringLiteral("stage"), PipelineStages::workerExecution()},
                                        {QStringLiteral("pipeline_id"), activePipelineId_},
                                        {QStringLiteral("chosen_backend"), decision.chosenBackend}});
    }
}

void SupervisorAgent::handleBackendFinished(AgentRole role, bool success, const QString& summary)
{
    if (activeTaskId_ <= 0 || role != AgentRole::Supervisor) {
        return;
    }

    const qint64 taskId = activeTaskId_;

    if (!success) {
        activeTaskId_ = 0;
        lastError_ = summary;
        queue_->markFailed(taskId, summary);
        queue_->recordEvent(taskId,
                            QStringLiteral("supervisor"),
                            QStringLiteral("failed"),
                            QJsonObject{{QStringLiteral("error"), summary.left(500)}});
        emit finished(taskId, false, summary);
        return;
    }

    const SupervisorDecision decision = parseDecision(summary);

    if (decision.decision == QStringLiteral("defer")) {
        const QString deferReason = decision.reasoning.isEmpty()
                                        ? QStringLiteral("Supervisor deferred the task.")
                                        : decision.reasoning;
        queue_->markFailed(taskId, deferReason);
        queue_->recordEvent(taskId,
                            QStringLiteral("supervisor"),
                            QStringLiteral("deferred"),
                            QJsonObject{{QStringLiteral("confidence"), decision.confidence},
                                        {QStringLiteral("reasoning"), deferReason.left(500)}});
        activeTaskId_ = 0;
        emit finished(taskId, false, deferReason);
        return;
    }

    const bool needsRework = decision.decision == QStringLiteral("rework")
                             || decision.confidence < kMinApprovalConfidence;
    if (needsRework) {
        queue_->markCompleted(taskId);
        queue_->recordEvent(taskId,
                            QStringLiteral("supervisor"),
                            QStringLiteral("rework_requested"),
                            QJsonObject{{QStringLiteral("confidence"), decision.confidence},
                                        {QStringLiteral("reasoning"), decision.reasoning.left(500)}});
        enqueuePlanningRework(taskId, decision);
        activeTaskId_ = 0;
        emit finished(taskId, true,
                      QStringLiteral("Supervisor requested planner rework (confidence %1).")
                          .arg(decision.confidence, 0, 'f', 2));
        return;
    }

    const QJsonObject approvedEnvelope = buildApprovedPlanEnvelope(taskId, decision);
    if (!PipelineQueue::writeArtifactJson(projectRoot_, taskId,
                                          PipelineArtifacts::fileApprovedPlanJson(),
                                          approvedEnvelope)) {
        lastError_ = QStringLiteral("Failed to write approved_plan.json for task %1.").arg(taskId);
        queue_->markFailed(taskId, lastError_);
        activeTaskId_ = 0;
        emit finished(taskId, false, lastError_);
        return;
    }

    queue_->markCompleted(taskId);
    queue_->recordEvent(taskId,
                        QStringLiteral("supervisor"),
                        QStringLiteral("approved"),
                        QJsonObject{{QStringLiteral("chosen_backend"), decision.chosenBackend},
                                    {QStringLiteral("confidence"), decision.confidence},
                                    {QStringLiteral("approved_plan_file"),
                                     PipelineArtifacts::fileApprovedPlanJson()}});

    enqueueWorkerStage(taskId, decision);
    activeTaskId_ = 0;
    emit finished(taskId, true,
                  QStringLiteral("Supervisor approved plan; worker stage enqueued (%1).")
                      .arg(decision.chosenBackend));
}

} // namespace VexaraOrchestration
