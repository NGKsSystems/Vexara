#include "VexaraOrchestration/PlannerAgent.h"

#include "VexaraOrchestration/AgentRoleIds.h"
#include "VexaraOrchestration/PipelineArtifacts.h"
#include "VexaraOrchestration/PipelineStageIds.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace VexaraOrchestration {

PlannerAgent::PlannerAgent(QObject* parent)
    : QObject(parent)
{
}

void PlannerAgent::configure(TaskBackendRegistry* backends,
                             AgentPromptComposer* composer,
                             PipelineQueue* queue,
                             const QString& projectRoot)
{
    backends_ = backends;
    composer_ = composer;
    queue_ = queue;
    projectRoot_ = projectRoot;
}

bool PlannerAgent::isRunning() const
{
    return activeTaskId_ > 0;
}

qint64 PlannerAgent::activeTaskId() const
{
    return activeTaskId_;
}

QString PlannerAgent::lastError() const
{
    return lastError_;
}

TaskContext PlannerAgent::contextFromPayload(const QJsonObject& payload) const
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

bool PlannerAgent::execute(const PipelineTaskRecord& task)
{
    if (isRunning()) {
        lastError_ = QStringLiteral("Planner is already running.");
        return false;
    }
    if (!backends_ || !composer_ || !queue_) {
        lastError_ = QStringLiteral("Planner is not configured.");
        return false;
    }

    ITaskBackend* backend = backends_->backendForRole(AgentRole::Orchestrator);
    if (!backend || !backends_->isRoleConfigured(AgentRole::Orchestrator)) {
        lastError_ = QStringLiteral("Orchestrator backend is not configured for planning.");
        return false;
    }
    if (backend->isRunning()) {
        lastError_ = QStringLiteral("Planning backend is busy.");
        return false;
    }

    activeTaskId_ = task.id;
    activeTaskPayload_ = task.payload;
    activePipelineId_ = task.payload.value(QStringLiteral("pipeline_id")).toString();
    activeUserTask_ = task.payload.value(QStringLiteral("user_task")).toString();
    activeContext_ = contextFromPayload(task.payload);

    const QString prompt = composer_->composePlannerPrompt(activeUserTask_, activeContext_);

  queue_->recordEvent(task.id,
                        QStringLiteral("planner"),
                        QStringLiteral("started"),
                        QJsonObject{{QStringLiteral("pipeline_id"), activePipelineId_}});

    emit started(task.id);
    backend->executeTask(AgentRole::Orchestrator, prompt, activeContext_);
    return true;
}

QString PlannerAgent::extractJsonBody(const QString& rawResponse) const
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

QJsonObject PlannerAgent::parseStructuredPlan(const QString& rawResponse) const
{
    const QString jsonBody = extractJsonBody(rawResponse);
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBody.toUtf8());
    if (doc.isObject()) {
        return doc.object();
    }

    QJsonObject fallback;
    fallback.insert(QStringLiteral("summary"), rawResponse.trimmed());
    fallback.insert(QStringLiteral("subtasks"), QJsonArray());
    fallback.insert(QStringLiteral("acceptance_criteria"), QJsonArray());
    fallback.insert(QStringLiteral("risk_notes"),
                   QJsonArray{QStringLiteral("Planner response was not valid JSON; stored raw text.")});
    fallback.insert(QStringLiteral("raw_plan_text"), rawResponse.trimmed());
    return fallback;
}

QJsonObject PlannerAgent::buildPlanEnvelope(qint64 taskId,
                                            const QString& pipelineId,
                                            const QString& userTask,
                                            const QString& rawResponse) const
{
    QJsonObject structuredPlan = parseStructuredPlan(rawResponse);

    QJsonObject payload;
    payload.insert(QStringLiteral("pipeline_id"), pipelineId);
    payload.insert(QStringLiteral("user_task"), userTask);
    payload.insert(QStringLiteral("summary"), structuredPlan.value(QStringLiteral("summary")));
    payload.insert(QStringLiteral("subtasks"), structuredPlan.value(QStringLiteral("subtasks")));
    payload.insert(QStringLiteral("acceptance_criteria"),
                   structuredPlan.value(QStringLiteral("acceptance_criteria")));
    payload.insert(QStringLiteral("risk_notes"), structuredPlan.value(QStringLiteral("risk_notes")));
    if (structuredPlan.contains(QStringLiteral("raw_plan_text"))) {
        payload.insert(QStringLiteral("raw_plan_text"),
                       structuredPlan.value(QStringLiteral("raw_plan_text")));
    } else {
        payload.insert(QStringLiteral("raw_plan_text"), rawResponse.trimmed());
    }

    return PipelineArtifacts::makeEnvelope(taskId,
                                           QStringLiteral("planner"),
                                           QStringLiteral("completed"),
                                           payload,
                                           QStringLiteral("Structured plan generated for supervisor review."));
}

QJsonObject PlannerAgent::buildSupervisorStagePayload(const QJsonObject& taskPayload,
                                                      qint64 planTaskId) const
{
    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("hierarchical_stage"));
    payload.insert(QStringLiteral("protocol_version"), 2);
    payload.insert(QStringLiteral("stage"), PipelineStages::supervisorReview());
    payload.insert(QStringLiteral("pipeline_id"), taskPayload.value(QStringLiteral("pipeline_id")));
    payload.insert(QStringLiteral("user_task"), taskPayload.value(QStringLiteral("user_task")));
    payload.insert(QStringLiteral("context"), taskPayload.value(QStringLiteral("context")));
    payload.insert(QStringLiteral("plan_task_id"), planTaskId);
    payload.insert(QStringLiteral("plan_file"), PipelineArtifacts::filePlanJson());
    return payload;
}

void PlannerAgent::handleBackendFinished(AgentRole role, bool success, const QString& summary)
{
    if (activeTaskId_ <= 0 || role != AgentRole::Orchestrator) {
        return;
    }

    const qint64 taskId = activeTaskId_;

    if (!success) {
        activeTaskId_ = 0;
        lastError_ = summary;
        queue_->markFailed(taskId, summary);
        queue_->recordEvent(taskId,
                            QStringLiteral("planner"),
                            QStringLiteral("failed"),
                            QJsonObject{{QStringLiteral("error"), summary.left(500)}});
        emit finished(taskId, false, summary);
        return;
    }

    const QJsonObject planEnvelope =
        buildPlanEnvelope(taskId, activePipelineId_, activeUserTask_, summary);
    if (!PipelineQueue::writeArtifactJson(projectRoot_, taskId, PipelineArtifacts::filePlanJson(),
                                          planEnvelope)) {
        lastError_ = QStringLiteral("Failed to write plan.json for task %1.").arg(taskId);
        queue_->recordEvent(taskId,
                            QStringLiteral("planner"),
                            QStringLiteral("failed"),
                            QJsonObject{{QStringLiteral("error"), lastError_}});
        activeTaskId_ = 0;
        emit finished(taskId, false, lastError_);
        return;
    }

    queue_->recordEvent(taskId,
                        QStringLiteral("planner"),
                        QStringLiteral("completed"),
                        QJsonObject{{QStringLiteral("plan_file"), PipelineArtifacts::filePlanJson()},
                                    {QStringLiteral("pipeline_id"), activePipelineId_}});

    queue_->markCompleted(taskId);

    const QJsonObject supervisorPayload = buildSupervisorStagePayload(activeTaskPayload_, taskId);
    const qint64 nextTaskId =
        queue_->enqueue(supervisorPayload, PipelineQueuePriority::kIntraPipelineStage);
    if (nextTaskId > 0) {
        queue_->recordEvent(nextTaskId,
                            QStringLiteral("planner"),
                            QStringLiteral("handoff_enqueued"),
                            QJsonObject{{QStringLiteral("stage"), PipelineStages::supervisorReview()},
                                        {QStringLiteral("pipeline_id"), activePipelineId_},
                                        {QStringLiteral("plan_task_id"), taskId}});
    }

    activeTaskId_ = 0;
    emit finished(taskId, true, summary);
}

} // namespace VexaraOrchestration
