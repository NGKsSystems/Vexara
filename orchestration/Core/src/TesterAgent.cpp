#include "VexaraOrchestration/TesterAgent.h"

#include "VexaraOrchestration/AgentRoleIds.h"
#include "VexaraOrchestration/ITaskBackend.h"
#include "VexaraOrchestration/PipelineArtifacts.h"
#include "VexaraOrchestration/PipelineStageIds.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>

namespace VexaraOrchestration {

namespace {

constexpr int kMaxCapturedOutputChars = 120000;

QString truncateOutput(const QString& text)
{
    if (text.length() <= kMaxCapturedOutputChars) {
        return text;
    }
    return text.left(kMaxCapturedOutputChars / 2) + QStringLiteral("\n...[truncated]...\n")
           + text.right(kMaxCapturedOutputChars / 2);
}

void appendUniqueCommand(QStringList& commands, const QString& command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (!commands.contains(trimmed)) {
        commands.append(trimmed);
    }
}

} // namespace

TesterAgent::TesterAgent(QObject* parent)
    : QObject(parent)
    , commandProcess_(new QProcess(this))
    , commandTimeoutTimer_(new QTimer(this))
{
    commandTimeoutTimer_->setSingleShot(true);

    connect(commandProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString chunk = QString::fromLocal8Bit(commandProcess_->readAllStandardOutput());
        if (chunk.isEmpty()) {
            return;
        }
        activeCommandOutput_.append(chunk);
        if (commandHost_) {
            commandHost_->appendTerminalOutput(chunk);
        }
    });
    connect(commandProcess_, &QProcess::readyReadStandardError, this, [this]() {
        const QString chunk = QString::fromLocal8Bit(commandProcess_->readAllStandardError());
        if (chunk.isEmpty()) {
            return;
        }
        activeCommandOutput_.append(chunk);
        if (commandHost_) {
            commandHost_->appendTerminalOutput(chunk);
        }
    });
    connect(commandProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (commandTimeoutTimer_->isActive()) {
                    commandTimeoutTimer_->stop();
                }
                finishCurrentCommand(exitCode, status != QProcess::NormalExit);
            });
    connect(commandTimeoutTimer_, &QTimer::timeout, this, [this]() {
        if (commandProcess_->state() != QProcess::NotRunning) {
            commandProcess_->kill();
            finishCurrentCommand(-1, true);
        }
    });
}

TesterAgent::~TesterAgent() = default;

void TesterAgent::configure(TaskBackendRegistry* backends,
                            AgentPromptComposer* composer,
                            PipelineQueue* queue,
                            ITesterCommandHost* commandHost,
                            const VexaraCore::VerificationSettings& verification,
                            const QString& projectRoot)
{
    backends_ = backends;
    composer_ = composer;
    queue_ = queue;
    commandHost_ = commandHost;
    verification_ = verification;
    projectRoot_ = projectRoot;
}

bool TesterAgent::isRunning() const
{
    return activeTaskId_ > 0;
}

qint64 TesterAgent::activeTaskId() const
{
    return activeTaskId_;
}

QString TesterAgent::lastError() const
{
    return lastError_;
}

TaskContext TesterAgent::contextFromPayload(const QJsonObject& payload) const
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

bool TesterAgent::loadApprovedPlan(qint64 supervisorTaskId, QJsonObject& envelopeOut) const
{
    if (supervisorTaskId <= 0) {
        return false;
    }
    return PipelineQueue::readArtifactJson(projectRoot_, supervisorTaskId,
                                           PipelineArtifacts::fileApprovedPlanJson(), envelopeOut);
}

bool TesterAgent::loadWorkerResult(qint64 workerTaskId, QString& markdownOut) const
{
    if (workerTaskId <= 0) {
        return false;
    }
    return PipelineQueue::readArtifactText(projectRoot_, workerTaskId,
                                           PipelineArtifacts::fileWorkerResultMd(), markdownOut);
}

QStringList TesterAgent::buildCommandQueue() const
{
    QStringList commands;

    const QString defaultCommand =
        commandHost_ ? commandHost_->verificationCommand() : verification_.command;
    appendUniqueCommand(commands, defaultCommand);

    const QJsonObject approvedPlan =
        activeApprovedPayload_.value(QStringLiteral("approved_plan")).toObject();
    const QJsonArray explicitCommands =
        approvedPlan.value(QStringLiteral("verification_commands")).toArray();
    for (const QJsonValue& value : explicitCommands) {
        appendUniqueCommand(commands, value.toString());
    }

    const QJsonArray verificationSteps = approvedPlan.value(QStringLiteral("verification_steps")).toArray();
    for (const QJsonValue& value : verificationSteps) {
        appendUniqueCommand(commands, value.toString());
    }

    const QJsonArray subtasks = approvedPlan.value(QStringLiteral("subtasks")).toArray();
    for (const QJsonValue& value : subtasks) {
        const QJsonObject subtask = value.toObject();
        appendUniqueCommand(commands, subtask.value(QStringLiteral("verification_command")).toString());
        const QJsonArray subtaskCommands =
            subtask.value(QStringLiteral("verification_commands")).toArray();
        for (const QJsonValue& commandValue : subtaskCommands) {
            appendUniqueCommand(commands, commandValue.toString());
        }
    }

    return commands;
}

bool TesterAgent::execute(const PipelineTaskRecord& task)
{
    if (isRunning()) {
        lastError_ = QStringLiteral("Tester is already running.");
        return false;
    }
    if (!backends_ || !composer_ || !queue_) {
        lastError_ = QStringLiteral("Tester is not configured.");
        return false;
    }
    if (!backends_->isRoleConfigured(AgentRole::Supervisor)) {
        lastError_ = QStringLiteral("Supervisor backend is required for test evaluation.");
        return false;
    }

    activeSupervisorTaskId_ =
        PipelineArtifacts::payloadTaskRef(task.payload, QStringLiteral("supervisor_task_id"));
    activeWorkerTaskId_ =
        PipelineArtifacts::payloadTaskRef(task.payload, QStringLiteral("worker_task_id"));

    QJsonObject approvedEnvelope;
    if (!loadApprovedPlan(activeSupervisorTaskId_, approvedEnvelope)) {
        lastError_ = QStringLiteral("Could not read approved_plan.json for supervisor task %1.")
                         .arg(activeSupervisorTaskId_);
        return false;
    }

    workerResultMarkdown_.clear();
    if (!loadWorkerResult(activeWorkerTaskId_, workerResultMarkdown_)) {
        workerResultMarkdown_ = QStringLiteral("(worker_result.md unavailable)");
    }

    activeTaskId_ = task.id;
    activeTaskPayload_ = task.payload;
    activeApprovedPayload_ = approvedEnvelope.value(QStringLiteral("payload")).toObject();
    activePipelineId_ = task.payload.value(QStringLiteral("pipeline_id")).toString();
    activeUserTask_ = task.payload.value(QStringLiteral("user_task")).toString();
    activeContext_ = contextFromPayload(task.payload);
    commandRuns_.clear();
    commandIndex_ = 0;
    phase_ = Phase::RunningCommands;

    commandQueue_ = buildCommandQueue();
    if (commandQueue_.isEmpty()) {
        lastError_ = QStringLiteral("No verification commands configured for testing.");
        activeTaskId_ = 0;
        phase_ = Phase::Idle;
        return false;
    }

    queue_->recordEvent(task.id,
                        QStringLiteral("tester"),
                        QStringLiteral("started"),
                        QJsonObject{{QStringLiteral("pipeline_id"), activePipelineId_},
                                    {QStringLiteral("command_count"), commandQueue_.size()}});

    emit started(task.id);
    beginCommandPhase();
    return true;
}

void TesterAgent::beginCommandPhase()
{
    if (commandHost_) {
        commandHost_->appendTerminalOutput(
            QStringLiteral("\n[Vexara Tester] Running verification commands...\n"));
    }
    runNextCommand();
}

void TesterAgent::runNextCommand()
{
    if (commandIndex_ >= commandQueue_.size()) {
        beginEvaluationPhase();
        return;
    }

    const QString command = commandQueue_.at(commandIndex_);
    const QString workingDirectory = activeContext_.projectPath.trimmed().isEmpty()
                                         ? projectRoot_
                                         : activeContext_.projectPath;

    if (workingDirectory.isEmpty() || !QFileInfo(workingDirectory).isDir()) {
        lastError_ = QStringLiteral("Open a project folder before running tests.");
        completeTask(false, lastError_);
        return;
    }

    activeCommandOutput_.clear();
    if (commandHost_) {
        commandHost_->appendTerminalOutput(
            QStringLiteral("\n[Vexara Tester] $ %1\n").arg(command));
    }

    commandProcess_->setProgram(QStringLiteral("cmd.exe"));
    commandProcess_->setArguments({QStringLiteral("/C"), command});
    commandProcess_->setWorkingDirectory(workingDirectory);
    commandProcess_->start();

    if (!commandProcess_->waitForStarted(5000)) {
        CommandRunResult result;
        result.command = command;
        result.exitCode = -1;
        result.success = false;
        result.output = QStringLiteral("Failed to start command.");
        commandRuns_.append(result);
        ++commandIndex_;
        runNextCommand();
        return;
    }

    commandTimeoutTimer_->start(verification_.timeoutMs);
}

void TesterAgent::finishCurrentCommand(int exitCode, bool crashed)
{
    if (phase_ != Phase::RunningCommands || commandIndex_ >= commandQueue_.size()) {
        return;
    }

    CommandRunResult result;
    result.command = commandQueue_.at(commandIndex_);
    result.exitCode = exitCode;
    result.success = !crashed && exitCode == 0;
    result.output = truncateOutput(activeCommandOutput_.trimmed());
    if (result.output.isEmpty()) {
        result.output = result.success
                            ? QStringLiteral("Command completed successfully.")
                            : QStringLiteral("Command failed (exit %1).").arg(exitCode);
    }
    commandRuns_.append(result);
    ++commandIndex_;
    runNextCommand();
}

void TesterAgent::beginEvaluationPhase()
{
    phase_ = Phase::RunningEvaluation;

    ITaskBackend* backend = backends_->backendForRole(AgentRole::Supervisor);
    if (!backend || backend->isRunning()) {
        lastError_ = QStringLiteral("Supervisor backend is unavailable for test evaluation.");
        completeTask(false, lastError_);
        return;
    }

    QJsonArray commandResults;
    for (const CommandRunResult& run : commandRuns_) {
        QJsonObject entry;
        entry.insert(QStringLiteral("command"), run.command);
        entry.insert(QStringLiteral("exit_code"), run.exitCode);
        entry.insert(QStringLiteral("success"), run.success);
        entry.insert(QStringLiteral("output"), run.output);
        commandResults.append(entry);
    }

    const QString prompt = composer_->composeTesterEvaluationPrompt(
        activeUserTask_, activeApprovedPayload_, workerResultMarkdown_, commandResults,
        activeContext_);

    queue_->recordEvent(activeTaskId_,
                        QStringLiteral("tester"),
                        QStringLiteral("evaluation_started"),
                        QJsonObject{{QStringLiteral("pipeline_id"), activePipelineId_},
                                    {QStringLiteral("commands_run"), commandResults.size()}});

    backend->executeTask(AgentRole::Supervisor, prompt, activeContext_);
}

QString TesterAgent::extractJsonBody(const QString& rawResponse) const
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

TesterAgent::TesterDecision TesterAgent::parseDecision(const QString& rawResponse) const
{
    TesterDecision decision;
    decision.overallVerdict = QStringLiteral("fail");
    decision.summary = rawResponse.trimmed().left(2000);

    const QJsonDocument doc = QJsonDocument::fromJson(extractJsonBody(rawResponse).toUtf8());
    if (!doc.isObject()) {
        decision.issues = QJsonArray{QStringLiteral("Tester evaluation did not return valid JSON.")};
        return decision;
    }

    const QJsonObject object = doc.object();
    decision.overallVerdict =
        object.value(QStringLiteral("overall_verdict")).toString(decision.overallVerdict).trimmed().toLower();
    decision.summary = object.value(QStringLiteral("summary")).toString(decision.summary);
    decision.subtaskResults = object.value(QStringLiteral("subtask_results")).toArray();
    decision.issues = object.value(QStringLiteral("issues")).toArray();
    decision.reasoning = object.value(QStringLiteral("reasoning")).toString();

    const bool anyCommandFailed =
        std::any_of(commandRuns_.cbegin(), commandRuns_.cend(),
                    [](const CommandRunResult& run) { return !run.success; });

    if (decision.overallVerdict != QStringLiteral("pass")
        && decision.overallVerdict != QStringLiteral("fail")) {
        decision.overallVerdict = anyCommandFailed ? QStringLiteral("fail") : QStringLiteral("pass");
    }

    if (anyCommandFailed) {
        decision.overallVerdict = QStringLiteral("fail");
        if (!decision.summary.contains(QStringLiteral("verification command"), Qt::CaseInsensitive)) {
            decision.summary =
                QStringLiteral("A verification command failed — see command_runs in test_results.")
                + (decision.summary.isEmpty() ? QString() : QStringLiteral(" ") + decision.summary);
        }
    }

    return decision;
}

QJsonObject TesterAgent::buildTestResultsPayload(const TesterDecision& decision) const
{
    QJsonArray commandResults;
    for (const CommandRunResult& run : commandRuns_) {
        QJsonObject entry;
        entry.insert(QStringLiteral("command"), run.command);
        entry.insert(QStringLiteral("exit_code"), run.exitCode);
        entry.insert(QStringLiteral("passed"), run.success);
        entry.insert(QStringLiteral("output"), run.output);
        commandResults.append(entry);
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("overall_verdict"), decision.overallVerdict);
    payload.insert(QStringLiteral("summary"), decision.summary);
    payload.insert(QStringLiteral("subtask_results"), decision.subtaskResults);
    payload.insert(QStringLiteral("issues"), decision.issues);
    payload.insert(QStringLiteral("command_runs"), commandResults);
    payload.insert(QStringLiteral("worker_task_id"), activeWorkerTaskId_);
    payload.insert(QStringLiteral("supervisor_task_id"), activeSupervisorTaskId_);
    payload.insert(QStringLiteral("pipeline_id"), activePipelineId_);
    return payload;
}

bool TesterAgent::writeTestResultsArtifact(qint64 taskId, const TesterDecision& decision) const
{
    const QJsonObject envelope =
        PipelineArtifacts::makeEnvelope(taskId,
                                        QStringLiteral("tester"),
                                        decision.overallVerdict == QStringLiteral("pass")
                                            ? QStringLiteral("passed")
                                            : QStringLiteral("failed"),
                                        buildTestResultsPayload(decision),
                                        decision.reasoning.trimmed().isEmpty() ? decision.summary
                                                                               : decision.reasoning);
    return PipelineQueue::writeArtifactJson(projectRoot_, taskId,
                                            PipelineArtifacts::fileTestResultsJson(), envelope);
}

void TesterAgent::enqueueReviewStage(qint64 testingTaskId)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("hierarchical_stage"));
    payload.insert(QStringLiteral("protocol_version"), 2);
    payload.insert(QStringLiteral("stage"), PipelineStages::review());
    payload.insert(QStringLiteral("pipeline_id"), activePipelineId_);
    payload.insert(QStringLiteral("user_task"), activeUserTask_);
    payload.insert(QStringLiteral("context"), activeTaskPayload_.value(QStringLiteral("context")));
    payload.insert(QStringLiteral("testing_task_id"), testingTaskId);
    payload.insert(QStringLiteral("worker_task_id"), activeWorkerTaskId_);
    payload.insert(QStringLiteral("supervisor_task_id"), activeSupervisorTaskId_);

    const qint64 nextTaskId =
        queue_->enqueue(payload, PipelineQueuePriority::kIntraPipelineStage);
    if (nextTaskId > 0) {
        queue_->recordEvent(nextTaskId,
                            QStringLiteral("tester"),
                            QStringLiteral("handoff_enqueued"),
                            QJsonObject{{QStringLiteral("stage"), PipelineStages::review()},
                                        {QStringLiteral("pipeline_id"), activePipelineId_}});
    }
}

void TesterAgent::completeTask(bool success, const QString& summary)
{
    const qint64 taskId = activeTaskId_;
    activeTaskId_ = 0;
    phase_ = Phase::Idle;
    commandQueue_.clear();
    commandIndex_ = 0;

    if (success) {
        queue_->markCompleted(taskId);
        queue_->recordEvent(taskId,
                            QStringLiteral("tester"),
                            QStringLiteral("completed"),
                            QJsonObject{{QStringLiteral("pipeline_id"), activePipelineId_}});
    } else {
        lastError_ = summary;
        queue_->markFailed(taskId, summary);
        queue_->recordEvent(taskId,
                            QStringLiteral("tester"),
                            QStringLiteral("failed"),
                            QJsonObject{{QStringLiteral("error"), summary.left(500)}});
    }

    emit finished(taskId, success, summary);
}

void TesterAgent::handleBackendFinished(AgentRole role, bool success, const QString& summary)
{
    if (activeTaskId_ <= 0 || phase_ != Phase::RunningEvaluation || role != AgentRole::Supervisor) {
        return;
    }

    const qint64 taskId = activeTaskId_;
    Q_UNUSED(taskId)

    if (!success) {
        completeTask(false, summary);
        return;
    }

    const TesterDecision decision = parseDecision(summary);
    if (!writeTestResultsArtifact(activeTaskId_, decision)) {
        completeTask(false, QStringLiteral("Failed to write test_results.json."));
        return;
    }

    const bool passed = decision.overallVerdict == QStringLiteral("pass");
    if (passed) {
        enqueueReviewStage(taskId);
        completeTask(true, decision.summary.trimmed().isEmpty()
                               ? QStringLiteral("Testing passed.")
                               : decision.summary);
        return;
    }

    QString failureSummary = decision.summary.trimmed();
    if (failureSummary.isEmpty()) {
        failureSummary = QStringLiteral("Testing failed.");
    }
  if (!decision.issues.isEmpty()) {
        failureSummary += QStringLiteral("\nIssues:\n");
        for (const QJsonValue& issue : decision.issues) {
            failureSummary += QStringLiteral("- ") + issue.toString() + QStringLiteral("\n");
        }
    }
    completeTask(false, failureSummary.trimmed());
}

} // namespace VexaraOrchestration
