#include "VexaraOrchestration/ReviewerAgent.h"

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

ReviewerAgent::ReviewerAgent(QObject* parent)
    : QObject(parent)
{
}

void ReviewerAgent::configure(TaskBackendRegistry* backends,
                              AgentPromptComposer* composer,
                              PipelineQueue* queue,
                              const QString& projectRoot)
{
    backends_ = backends;
    composer_ = composer;
    queue_ = queue;
    projectRoot_ = projectRoot;
}

bool ReviewerAgent::isRunning() const
{
    return activeTaskId_ > 0;
}

qint64 ReviewerAgent::activeTaskId() const
{
    return activeTaskId_;
}

QString ReviewerAgent::lastError() const
{
    return lastError_;
}

TaskContext ReviewerAgent::contextFromPayload(const QJsonObject& payload) const
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

bool ReviewerAgent::loadApprovedPlan(qint64 supervisorTaskId, QJsonObject& envelopeOut) const
{
    if (supervisorTaskId <= 0) {
        return false;
    }
    return PipelineQueue::readArtifactJson(projectRoot_, supervisorTaskId,
                                           PipelineArtifacts::fileApprovedPlanJson(), envelopeOut);
}

bool ReviewerAgent::loadWorkerResult(qint64 workerTaskId, QString& markdownOut) const
{
    if (workerTaskId <= 0) {
        return false;
    }
    return PipelineQueue::readArtifactText(projectRoot_, workerTaskId,
                                           PipelineArtifacts::fileWorkerResultMd(), markdownOut);
}

bool ReviewerAgent::loadTestResults(qint64 testingTaskId, QJsonObject& envelopeOut) const
{
    if (testingTaskId <= 0) {
        return false;
    }
    return PipelineQueue::readArtifactJson(projectRoot_, testingTaskId,
                                           PipelineArtifacts::fileTestResultsJson(), envelopeOut);
}

bool ReviewerAgent::execute(const PipelineTaskRecord& task)
{
    if (isRunning()) {
        lastError_ = QStringLiteral("Reviewer is already running.");
        return false;
    }
    if (!backends_ || !composer_ || !queue_) {
        lastError_ = QStringLiteral("Reviewer is not configured.");
        return false;
    }
    if (!backends_->isRoleConfigured(AgentRole::Supervisor)) {
        lastError_ = QStringLiteral("Supervisor backend is required for final review.");
        return false;
    }

    activeSupervisorTaskId_ =
        PipelineArtifacts::payloadTaskRef(task.payload, QStringLiteral("supervisor_task_id"));
    activeWorkerTaskId_ =
        PipelineArtifacts::payloadTaskRef(task.payload, QStringLiteral("worker_task_id"));
    activeTestingTaskId_ =
        PipelineArtifacts::payloadTaskRef(task.payload, QStringLiteral("testing_task_id"));

    QJsonObject approvedEnvelope;
    if (!loadApprovedPlan(activeSupervisorTaskId_, approvedEnvelope)) {
        lastError_ = QStringLiteral("Could not read approved_plan.json for supervisor task %1.")
                         .arg(activeSupervisorTaskId_);
        return false;
    }

    activeWorkerResultMarkdown_.clear();
    if (!loadWorkerResult(activeWorkerTaskId_, activeWorkerResultMarkdown_)) {
        activeWorkerResultMarkdown_ = QStringLiteral("(worker_result.md unavailable)");
    }

    QJsonObject testEnvelope;
    if (!loadTestResults(activeTestingTaskId_, testEnvelope)) {
        lastError_ = QStringLiteral("Could not read test_results.json for testing task %1.")
                         .arg(activeTestingTaskId_);
        return false;
    }

    ITaskBackend* backend = backends_->backendForRole(AgentRole::Supervisor);
    if (!backend || backend->isRunning()) {
        lastError_ = QStringLiteral("Supervisor backend is unavailable for review.");
        return false;
    }

    activeTaskId_ = task.id;
    activeTaskPayload_ = task.payload;
    activeApprovedPayload_ = approvedEnvelope.value(QStringLiteral("payload")).toObject();
    activeTestResultsPayload_ = testEnvelope.value(QStringLiteral("payload")).toObject();
    activePipelineId_ = task.payload.value(QStringLiteral("pipeline_id")).toString();
    activeUserTask_ = task.payload.value(QStringLiteral("user_task")).toString();
    activeContext_ = contextFromPayload(task.payload);

    const QString prompt = composer_->composeReviewerPrompt(
        activeUserTask_, activeApprovedPayload_, activeWorkerResultMarkdown_,
        activeTestResultsPayload_, activeContext_);

    queue_->recordEvent(task.id,
                        QStringLiteral("reviewer"),
                        QStringLiteral("started"),
                        QJsonObject{{QStringLiteral("pipeline_id"), activePipelineId_},
                                    {QStringLiteral("testing_task_id"), activeTestingTaskId_}});

    emit started(task.id);
    backend->executeTask(AgentRole::Supervisor, prompt, activeContext_);
    return true;
}

QString ReviewerAgent::extractJsonBody(const QString& rawResponse) const
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

QString ReviewerAgent::normalizeReworkStage(const QString& stage) const
{
    const QString normalized = stage.trimmed().toLower();
    if (normalized == QStringLiteral("planner") || normalized == QStringLiteral("planning")) {
        return PipelineStages::planning();
    }
    if (normalized == QStringLiteral("supervisor") || normalized == QStringLiteral("supervisor_review")) {
        return PipelineStages::supervisorReview();
    }
    if (normalized == QStringLiteral("worker")) {
        return PipelineStages::workerExecution();
    }
    if (normalized == QStringLiteral("tester") || normalized == QStringLiteral("testing")) {
        return PipelineStages::testing();
    }
    return normalized;
}

ReviewerAgent::ReviewDecision ReviewerAgent::parseDecision(const QString& rawResponse) const
{
    ReviewDecision decision;
    decision.decision = QStringLiteral("rework");
    decision.reworkStage = PipelineStages::workerExecution();
    decision.summary = rawResponse.trimmed().left(2000);

    const QJsonDocument doc = QJsonDocument::fromJson(extractJsonBody(rawResponse).toUtf8());
    if (!doc.isObject()) {
        decision.reasoning = QStringLiteral("Reviewer response was not valid JSON.");
        decision.issues = QJsonArray{decision.reasoning};
        return decision;
    }

    const QJsonObject object = doc.object();
    decision.decision = object.value(QStringLiteral("decision")).toString(decision.decision).trimmed().toLower();
    decision.confidence = object.value(QStringLiteral("confidence")).toDouble(decision.confidence);
    decision.reasoning = object.value(QStringLiteral("reasoning")).toString();
    decision.reworkStage =
        normalizeReworkStage(object.value(QStringLiteral("rework_stage")).toString(decision.reworkStage));
    decision.summary = object.value(QStringLiteral("summary")).toString(decision.summary);
    decision.issues = object.value(QStringLiteral("issues")).toArray();

    if (decision.decision != QStringLiteral("approve") && decision.decision != QStringLiteral("rework")
        && decision.decision != QStringLiteral("escalate")) {
        decision.decision = QStringLiteral("rework");
    }

    return decision;
}

QJsonObject ReviewerAgent::buildReviewDecisionPayload(const ReviewDecision& decision) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("decision"), decision.decision);
    payload.insert(QStringLiteral("confidence"), decision.confidence);
    payload.insert(QStringLiteral("reasoning"), decision.reasoning);
    payload.insert(QStringLiteral("rework_stage"), decision.reworkStage);
    payload.insert(QStringLiteral("summary"), decision.summary);
    payload.insert(QStringLiteral("issues"), decision.issues);
    payload.insert(QStringLiteral("pipeline_id"), activePipelineId_);
    payload.insert(QStringLiteral("testing_task_id"), activeTestingTaskId_);
    payload.insert(QStringLiteral("worker_task_id"), activeWorkerTaskId_);
    payload.insert(QStringLiteral("supervisor_task_id"), activeSupervisorTaskId_);
    return payload;
}

bool ReviewerAgent::writeReviewDecisionArtifact(qint64 taskId, const ReviewDecision& decision) const
{
    const QString status = decision.decision == QStringLiteral("approve")
                               ? QStringLiteral("approved")
                               : decision.decision == QStringLiteral("escalate")
                                     ? QStringLiteral("escalated")
                                     : QStringLiteral("rework");
    const QJsonObject envelope =
        PipelineArtifacts::makeEnvelope(taskId,
                                        QStringLiteral("reviewer"),
                                        status,
                                        buildReviewDecisionPayload(decision),
                                        decision.reasoning.trimmed().isEmpty() ? decision.summary
                                                                               : decision.reasoning);
    return PipelineQueue::writeArtifactJson(projectRoot_, taskId,
                                            PipelineArtifacts::fileReviewDecisionJson(), envelope);
}

QJsonObject ReviewerAgent::buildPlanningReworkPayload(qint64 reviewerTaskId,
                                                      const ReviewDecision& decision) const
{
    QString reworkTask = activeUserTask_;
    if (!decision.reasoning.trimmed().isEmpty()) {
        reworkTask += QStringLiteral("\n\nReviewer rework feedback:\n") + decision.reasoning;
    }
    if (!decision.issues.isEmpty()) {
        reworkTask += QStringLiteral("\n\nIssues to address:\n");
        for (const QJsonValue& issue : decision.issues) {
            reworkTask += QStringLiteral("- ") + issue.toString() + QStringLiteral("\n");
        }
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("hierarchical_stage"));
    payload.insert(QStringLiteral("protocol_version"), 2);
    payload.insert(QStringLiteral("stage"), PipelineStages::planning());
    payload.insert(QStringLiteral("pipeline_id"), activePipelineId_);
    payload.insert(QStringLiteral("user_task"), reworkTask.trimmed());
    payload.insert(QStringLiteral("context"), activeTaskPayload_.value(QStringLiteral("context")));
    payload.insert(QStringLiteral("rework_from_reviewer"), true);
    payload.insert(QStringLiteral("reviewer_task_id"), reviewerTaskId);
    payload.insert(QStringLiteral("reviewer_feedback"), decision.reasoning);
    return payload;
}

QJsonObject ReviewerAgent::buildSupervisorReworkPayload(qint64 reviewerTaskId,
                                                        const ReviewDecision& decision) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("hierarchical_stage"));
    payload.insert(QStringLiteral("protocol_version"), 2);
    payload.insert(QStringLiteral("stage"), PipelineStages::supervisorReview());
    payload.insert(QStringLiteral("pipeline_id"), activePipelineId_);
    payload.insert(QStringLiteral("user_task"), activeUserTask_);
    payload.insert(QStringLiteral("context"), activeTaskPayload_.value(QStringLiteral("context")));
    payload.insert(QStringLiteral("rework_from_reviewer"), true);
    payload.insert(QStringLiteral("reviewer_task_id"), reviewerTaskId);
    payload.insert(QStringLiteral("reviewer_feedback"), decision.reasoning);
    payload.insert(QStringLiteral("plan_task_id"),
                   PipelineArtifacts::jsonToTaskId(
                       activeApprovedPayload_.value(QStringLiteral("source_plan_task_id"))));
    return payload;
}

QJsonObject ReviewerAgent::buildWorkerReworkPayload(qint64 reviewerTaskId,
                                                    const ReviewDecision& decision) const
{
    QString workerInstructions =
        activeApprovedPayload_.value(QStringLiteral("worker_instructions")).toString();
    if (!decision.reasoning.trimmed().isEmpty()) {
        workerInstructions += QStringLiteral("\n\nReviewer rework feedback:\n") + decision.reasoning;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("hierarchical_stage"));
    payload.insert(QStringLiteral("protocol_version"), 2);
    payload.insert(QStringLiteral("stage"), PipelineStages::workerExecution());
    payload.insert(QStringLiteral("pipeline_id"), activePipelineId_);
    payload.insert(QStringLiteral("user_task"), activeUserTask_);
    payload.insert(QStringLiteral("context"), activeTaskPayload_.value(QStringLiteral("context")));
    payload.insert(QStringLiteral("supervisor_task_id"), activeSupervisorTaskId_);
    payload.insert(QStringLiteral("chosen_backend"), activeApprovedPayload_.value(QStringLiteral("chosen_backend")));
    payload.insert(QStringLiteral("worker_instructions"), workerInstructions.trimmed());
    payload.insert(QStringLiteral("rework_from_reviewer"), true);
    payload.insert(QStringLiteral("reviewer_task_id"), reviewerTaskId);
    payload.insert(QStringLiteral("reviewer_feedback"), decision.reasoning);
    return payload;
}

QJsonObject ReviewerAgent::buildTesterReworkPayload(qint64 reviewerTaskId,
                                                    const ReviewDecision& decision) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("hierarchical_stage"));
    payload.insert(QStringLiteral("protocol_version"), 2);
    payload.insert(QStringLiteral("stage"), PipelineStages::testing());
    payload.insert(QStringLiteral("pipeline_id"), activePipelineId_);
    payload.insert(QStringLiteral("user_task"), activeUserTask_);
    payload.insert(QStringLiteral("context"), activeTaskPayload_.value(QStringLiteral("context")));
    payload.insert(QStringLiteral("worker_task_id"), activeWorkerTaskId_);
    payload.insert(QStringLiteral("supervisor_task_id"), activeSupervisorTaskId_);
    payload.insert(QStringLiteral("rework_from_reviewer"), true);
    payload.insert(QStringLiteral("reviewer_task_id"), reviewerTaskId);
    payload.insert(QStringLiteral("reviewer_feedback"), decision.reasoning);
    return payload;
}

QJsonObject ReviewerAgent::buildEscalationPayload(qint64 reviewerTaskId,
                                                  const ReviewDecision& decision) const
{
    QString escalatedTask = activeUserTask_;
    escalatedTask += QStringLiteral("\n\nReviewer escalation — Director should re-orchestrate:\n");
    if (!decision.reasoning.trimmed().isEmpty()) {
        escalatedTask += decision.reasoning;
    }
    if (!decision.issues.isEmpty()) {
        escalatedTask += QStringLiteral("\n\nEscalation issues:\n");
        for (const QJsonValue& issue : decision.issues) {
            escalatedTask += QStringLiteral("- ") + issue.toString() + QStringLiteral("\n");
        }
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("hierarchical_stage"));
    payload.insert(QStringLiteral("protocol_version"), 2);
    payload.insert(QStringLiteral("stage"), PipelineStages::planning());
    payload.insert(QStringLiteral("pipeline_id"), activePipelineId_);
    payload.insert(QStringLiteral("user_task"), escalatedTask.trimmed());
    payload.insert(QStringLiteral("context"), activeTaskPayload_.value(QStringLiteral("context")));
    payload.insert(QStringLiteral("escalated_from_reviewer"), true);
    payload.insert(QStringLiteral("reviewer_task_id"), reviewerTaskId);
    payload.insert(QStringLiteral("reviewer_feedback"), decision.reasoning);
    return payload;
}

void ReviewerAgent::enqueueReworkStage(qint64 reviewerTaskId, const ReviewDecision& decision)
{
    QJsonObject payload;
    QString targetStage = decision.reworkStage;

    if (decision.decision == QStringLiteral("escalate")) {
        payload = buildEscalationPayload(reviewerTaskId, decision);
        targetStage = PipelineStages::planning();
    } else if (targetStage == PipelineStages::planning()) {
        payload = buildPlanningReworkPayload(reviewerTaskId, decision);
    } else if (targetStage == PipelineStages::supervisorReview()) {
        payload = buildSupervisorReworkPayload(reviewerTaskId, decision);
    } else if (targetStage == PipelineStages::testing()) {
        payload = buildTesterReworkPayload(reviewerTaskId, decision);
    } else {
        payload = buildWorkerReworkPayload(reviewerTaskId, decision);
        targetStage = PipelineStages::workerExecution();
    }

    const qint64 nextTaskId =
        queue_->enqueue(payload, PipelineQueuePriority::kIntraPipelineStage);
    if (nextTaskId > 0) {
        queue_->recordEvent(nextTaskId,
                            QStringLiteral("reviewer"),
                            QStringLiteral("rework_enqueued"),
                            QJsonObject{{QStringLiteral("stage"), targetStage},
                                        {QStringLiteral("pipeline_id"), activePipelineId_},
                                        {QStringLiteral("decision"), decision.decision}});
    }
}

void ReviewerAgent::handleBackendFinished(AgentRole role, bool success, const QString& summary)
{
    if (activeTaskId_ <= 0 || role != AgentRole::Supervisor) {
        return;
    }

    const qint64 taskId = activeTaskId_;
    const QString pipelineId = activePipelineId_;

    if (!success) {
        activeTaskId_ = 0;
        lastError_ = summary;
        queue_->markFailed(taskId, summary);
        queue_->recordEvent(taskId,
                            QStringLiteral("reviewer"),
                            QStringLiteral("failed"),
                            QJsonObject{{QStringLiteral("error"), summary.left(500)}});
        emit finished(taskId, false, summary);
        return;
    }

    ReviewDecision decision = parseDecision(summary);

    if (decision.decision == QStringLiteral("approve")
        && decision.confidence < kMinApprovalConfidence) {
        decision.decision = QStringLiteral("rework");
        if (decision.reworkStage.isEmpty()) {
            decision.reworkStage = PipelineStages::workerExecution();
        }
        if (decision.reasoning.isEmpty()) {
            decision.reasoning =
                QStringLiteral("Approval confidence below threshold (%1).")
                    .arg(decision.confidence, 0, 'f', 2);
        }
    }

    if (!writeReviewDecisionArtifact(taskId, decision)) {
        lastError_ = QStringLiteral("Failed to write review_decision.json for task %1.").arg(taskId);
        queue_->markFailed(taskId, lastError_);
        activeTaskId_ = 0;
        emit finished(taskId, false, lastError_);
        return;
    }

    queue_->markCompleted(taskId);

    if (decision.decision == QStringLiteral("approve")) {
        const QString completionSummary = decision.summary.trimmed().isEmpty()
                                              ? QStringLiteral("Pipeline approved by reviewer.")
                                              : decision.summary;
        queue_->recordEvent(taskId,
                            QStringLiteral("reviewer"),
                            QStringLiteral("approved"),
                            QJsonObject{{QStringLiteral("pipeline_id"), pipelineId},
                                        {QStringLiteral("confidence"), decision.confidence}});
        activeTaskId_ = 0;
        emit pipelineApproved(pipelineId, completionSummary);
        emit finished(taskId, true, completionSummary);
        return;
    }

    enqueueReworkStage(taskId, decision);
    queue_->recordEvent(taskId,
                        QStringLiteral("reviewer"),
                        decision.decision == QStringLiteral("escalate")
                            ? QStringLiteral("escalated")
                            : QStringLiteral("rework_requested"),
                        QJsonObject{{QStringLiteral("pipeline_id"), pipelineId},
                                    {QStringLiteral("rework_stage"), decision.reworkStage},
                                    {QStringLiteral("confidence"), decision.confidence}});

    const QString outcomeSummary =
        decision.decision == QStringLiteral("escalate")
            ? QStringLiteral("Reviewer escalated pipeline %1 to Director (planning restart).")
                  .arg(pipelineId)
            : QStringLiteral("Reviewer requested %1 rework for pipeline %2.")
                  .arg(decision.reworkStage, pipelineId);
    activeTaskId_ = 0;
    emit finished(taskId, true, outcomeSummary);
}

} // namespace VexaraOrchestration
