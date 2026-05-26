#include "VexaraOrchestration/WorkerAgent.h"

#include "VexaraCore/AgentServiceKind.h"
#include "VexaraOrchestration/AgentRoleIds.h"
#include "VexaraOrchestration/HttpModelTaskBackend.h"
#include "VexaraOrchestration/ITaskBackend.h"
#include "VexaraOrchestration/PipelineArtifacts.h"
#include "VexaraOrchestration/PipelineStageIds.h"
#include "VexaraOrchestration/WorkerGitDiff.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace VexaraOrchestration {

namespace {

void appendUniqueTargetFile(QStringList& files, const QString& candidate, const QString& projectRoot)
{
    QString path = candidate.trimmed();
    if (path.isEmpty() || projectRoot.trimmed().isEmpty()) {
        return;
    }

    const QDir projectDir(projectRoot);
    const QFileInfo directInfo(projectDir.absoluteFilePath(path));
    if (directInfo.isFile()) {
        path = projectDir.relativeFilePath(directInfo.absoluteFilePath());
    } else if (QFileInfo(path).isFile()) {
        path = projectDir.relativeFilePath(QFileInfo(path).absoluteFilePath());
    } else {
        const QString fileName = QFileInfo(path).fileName();
        if (fileName.isEmpty()) {
            return;
        }

        QStringList matches;
        QDirIterator iterator(projectRoot,
                              QStringList{fileName},
                              QDir::Files,
                              QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (iterator.hasNext()) {
            const QString absolutePath = iterator.next();
            const QString relativePath = projectDir.relativeFilePath(absolutePath);
            if (relativePath.startsWith(QStringLiteral(".."))
                || relativePath.startsWith(QStringLiteral(".vexara/"), Qt::CaseInsensitive)
                || relativePath.contains(QStringLiteral("/build_graph/"), Qt::CaseInsensitive)
                || relativePath.contains(QStringLiteral("/_proof/"), Qt::CaseInsensitive)) {
                continue;
            }
            matches.append(relativePath);
        }

        if (matches.isEmpty()) {
            return;
        }

        path = matches.first();
        for (const QString& match : matches) {
            if (match.contains(QStringLiteral("/src/"), Qt::CaseInsensitive)
                || match.startsWith(QStringLiteral("apps/"), Qt::CaseInsensitive)) {
                path = match;
                break;
            }
        }
    }

    if (path.startsWith(QStringLiteral(".."))) {
        return;
    }

    for (const QString& existing : files) {
        if (existing.compare(path, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    files.append(path);
}

void collectTargetFilesFromArray(QStringList& files,
                                 const QJsonArray& array,
                                 const QString& projectRoot)
{
    for (const QJsonValue& value : array) {
        appendUniqueTargetFile(files, value.toString(), projectRoot);
    }
}

bool isProtectedTargetFile(const QString& relativePath)
{
    const QString normalized = relativePath.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toLower();
    if (normalized.isEmpty()) {
        return true;
    }
    if (normalized.endsWith(QStringLiteral("vexara.json"))
        || normalized.endsWith(QStringLiteral("build_plan.json"))) {
        return true;
    }
    if (normalized.contains(QStringLiteral("cmakelists.txt"))) {
        return true;
    }
    if (normalized.contains(QStringLiteral("settingsdialog"))
        || normalized.contains(QStringLiteral("aiderbridge"))
        || normalized.contains(QStringLiteral("aidercli"))
        || normalized.contains(QStringLiteral("aidersettings"))
        || normalized.contains(QStringLiteral("verificationsettings"))
        || normalized.contains(QStringLiteral("globalsettings"))) {
        return true;
    }
    if (normalized.contains(QStringLiteral("/docs/")) || normalized.endsWith(QStringLiteral(".md"))) {
        return true;
    }
    if (normalized.endsWith(QStringLiteral(".bat")) || normalized.endsWith(QStringLiteral(".ps1"))
        || normalized.endsWith(QStringLiteral(".json"))) {
        return true;
    }
    return false;
}

QStringList filterEditableTargetFiles(const QStringList& candidates, const QString& projectRoot)
{
    QStringList filtered;
    filtered.reserve(candidates.size());
    for (const QString& candidate : candidates) {
        if (isProtectedTargetFile(candidate)) {
            continue;
        }
        appendUniqueTargetFile(filtered, candidate, projectRoot);
    }
    return filtered;
}

QString extractPrimaryFileFromUserTask(const QString& userTask)
{
    const QString trimmed = userTask.trimmed();

    static const QRegularExpression explicitEditPattern(
        QStringLiteral(
            R"((?:edit|modify|update|change|fix|patch|implement|add|insert|write)\s+(?:\w+\s+){0,6}["'`]?([A-Za-z0-9_./\\-]+\.(?:cpp|h|hpp|c|cc|cxx|cs|py|js|ts|tsx|jsx)))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch editMatch = explicitEditPattern.match(trimmed);
    if (editMatch.hasMatch()) {
        return editMatch.captured(1);
    }

    static const QRegularExpression ofFilePattern(
        QStringLiteral(
            R"((?:top of|to|in|into|file)\s+["'`]?([A-Za-z0-9_./\\-]+\.(?:cpp|h|hpp|c|cc|cxx|cs|py|js|ts|tsx|jsx)))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch ofMatch = ofFilePattern.match(trimmed);
    if (ofMatch.hasMatch()) {
        return ofMatch.captured(1);
    }

    static const QRegularExpression anyFilePattern(
        QStringLiteral(R"(\b([A-Za-z0-9_./\\-]+\.(?:cpp|h|hpp|c|cc|cxx))\b)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch anyMatch = anyFilePattern.match(trimmed);
    if (anyMatch.hasMatch()) {
        return anyMatch.captured(1);
    }

    return QString();
}

QStringList resolveWorkerTargetFiles(const QJsonObject& approvedPayload,
                                     const QString& userTask,
                                     const QString& workerInstructions,
                                     const TaskContext& context)
{
    Q_UNUSED(workerInstructions);

    QStringList files;
    const QString projectRoot = context.projectPath.trimmed();
    if (projectRoot.isEmpty()) {
        return files;
    }

    collectTargetFilesFromArray(files, approvedPayload.value(QStringLiteral("target_files")).toArray(),
                                projectRoot);

    const QJsonObject approvedPlan =
        approvedPayload.value(QStringLiteral("approved_plan")).toObject();
    collectTargetFilesFromArray(files, approvedPlan.value(QStringLiteral("target_files")).toArray(),
                                projectRoot);

    const QJsonArray subtasks = approvedPlan.value(QStringLiteral("subtasks")).toArray();
    for (const QJsonValue& subtaskValue : subtasks) {
        collectTargetFilesFromArray(
            files, subtaskValue.toObject().value(QStringLiteral("target_files")).toArray(),
            projectRoot);
    }

    files = filterEditableTargetFiles(files, projectRoot);

    if (files.isEmpty()) {
        appendUniqueTargetFile(files, context.currentFilePath, projectRoot);
    }
    if (files.isEmpty()) {
        appendUniqueTargetFile(files, extractPrimaryFileFromUserTask(userTask), projectRoot);
    }

    files = filterEditableTargetFiles(files, projectRoot);

    constexpr int kMaxAiderTargetFiles = 2;
    if (files.size() > kMaxAiderTargetFiles) {
        files = files.mid(0, kMaxAiderTargetFiles);
    }

    return files;
}

} // namespace

WorkerAgent::WorkerAgent(QObject* parent)
    : QObject(parent)
{
}

void WorkerAgent::configure(TaskBackendRegistry* backends,
                            AgentPromptComposer* composer,
                            PipelineQueue* queue,
                            IWorkerEditorHost* editorHost,
                            const QString& projectRoot)
{
    backends_ = backends;
    composer_ = composer;
    queue_ = queue;
    editorHost_ = editorHost;
    projectRoot_ = projectRoot;
}

bool WorkerAgent::isRunning() const
{
    return activeTaskId_ > 0;
}

qint64 WorkerAgent::activeTaskId() const
{
    return activeTaskId_;
}

QString WorkerAgent::lastError() const
{
    return lastError_;
}

TaskContext WorkerAgent::contextFromPayload(const QJsonObject& payload) const
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

bool WorkerAgent::loadApprovedPlan(qint64 supervisorTaskId, QJsonObject& envelopeOut) const
{
    if (supervisorTaskId <= 0) {
        return false;
    }
    return PipelineQueue::readArtifactJson(projectRoot_, supervisorTaskId,
                                           PipelineArtifacts::fileApprovedPlanJson(), envelopeOut);
}

VexaraCore::AgentServiceKind WorkerAgent::backendKindFromString(const QString& value) const
{
    return VexaraCore::agentServiceKindFromString(value);
}

bool WorkerAgent::isBackendReady(VexaraCore::AgentServiceKind kind) const
{
    if (!backends_) {
        return false;
    }

    ITaskBackend* backend = backends_->backendForService(kind);
    if (!backend) {
        return false;
    }

    if (kind == VexaraCore::AgentServiceKind::OpenAiHttp
        || kind == VexaraCore::AgentServiceKind::OpenRouterHttp) {
        return backends_->isRoleConfigured(AgentRole::Builder)
               && backends_->serviceForRole(AgentRole::Builder) == kind;
    }

    if (kind == VexaraCore::AgentServiceKind::GrokCli
        || kind == VexaraCore::AgentServiceKind::AiderCli) {
        return backend->isConfigured();
    }

    return backend->isConfigured();
}

bool WorkerAgent::execute(const PipelineTaskRecord& task)
{
    if (isRunning()) {
        lastError_ = QStringLiteral("Worker is already running.");
        return false;
    }
    if (!backends_ || !composer_ || !queue_) {
        lastError_ = QStringLiteral("Worker is not configured.");
        return false;
    }

    const qint64 supervisorTaskId =
        PipelineArtifacts::payloadTaskRef(task.payload, QStringLiteral("supervisor_task_id"));
    QJsonObject approvedEnvelope;
    if (!loadApprovedPlan(supervisorTaskId, approvedEnvelope)) {
        lastError_ =
            QStringLiteral("Could not read approved_plan.json for supervisor task %1.")
                .arg(supervisorTaskId);
        return false;
    }

    activeChosenBackend_ = task.payload.value(QStringLiteral("chosen_backend")).toString();
    if (activeChosenBackend_.trimmed().isEmpty()) {
        activeChosenBackend_ =
            approvedEnvelope.value(QStringLiteral("payload"))
                .toObject()
                .value(QStringLiteral("chosen_backend"))
                .toString();
    }

    activeBackendKind_ = backendKindFromString(activeChosenBackend_);
    if (!isBackendReady(activeBackendKind_)) {
        lastError_ = QStringLiteral("Worker backend '%1' is not configured.")
                         .arg(activeChosenBackend_);
        return false;
    }

    ITaskBackend* backend = backends_->backendForService(activeBackendKind_);
    if (!backend || backend->isRunning()) {
        lastError_ = QStringLiteral("Worker backend is unavailable.");
        return false;
    }

    activeTaskId_ = task.id;
    activeTaskPayload_ = task.payload;
    activeApprovedPayload_ = approvedEnvelope.value(QStringLiteral("payload")).toObject();
    activePipelineId_ = task.payload.value(QStringLiteral("pipeline_id")).toString();
    activeUserTask_ = task.payload.value(QStringLiteral("user_task")).toString();
    activeWorkerInstructions_ = task.payload.value(QStringLiteral("worker_instructions")).toString();
    if (activeWorkerInstructions_.trimmed().isEmpty()) {
        activeWorkerInstructions_ =
            activeApprovedPayload_.value(QStringLiteral("worker_instructions")).toString();
    }
    activeContext_ = contextFromPayload(task.payload);
    activeContext_.targetFiles = resolveWorkerTargetFiles(
        activeApprovedPayload_, activeUserTask_, activeWorkerInstructions_, activeContext_);

    const bool structuredEdits =
        activeBackendKind_ == VexaraCore::AgentServiceKind::OpenAiHttp
        || activeBackendKind_ == VexaraCore::AgentServiceKind::OpenRouterHttp;

    baselineDiff_ = WorkerGitDiff::captureDiff(activeContext_.projectPath);

    const QString prompt = composer_->composeWorkerPrompt(
        activeUserTask_, activeApprovedPayload_, activeWorkerInstructions_, activeContext_,
        structuredEdits, activeBackendKind_);

    queue_->recordEvent(task.id,
                        QStringLiteral("worker"),
                        QStringLiteral("started"),
                        QJsonObject{{QStringLiteral("pipeline_id"), activePipelineId_},
                                    {QStringLiteral("chosen_backend"), activeChosenBackend_}});

    emit started(task.id);
    backend->executeTask(AgentRole::Builder, prompt, activeContext_);
    return true;
}

QString WorkerAgent::extractJsonBody(const QString& rawResponse) const
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

int WorkerAgent::applyStructuredEdits(const QJsonObject& responseObject)
{
    if (!editorHost_) {
        return 0;
    }

    const QJsonArray edits = responseObject.value(QStringLiteral("edits")).toArray();
    int applied = 0;
    for (const QJsonValue& value : edits) {
        const QJsonObject edit = value.toObject();
        const QString path = edit.value(QStringLiteral("path")).toString();
        const int line = edit.value(QStringLiteral("line")).toInt(1);
        const int column = edit.value(QStringLiteral("column")).toInt(1);
        const int replaceLength = edit.value(QStringLiteral("replace_length")).toInt(0);
        const QString newText = edit.value(QStringLiteral("new_text")).toString();
        if (path.trimmed().isEmpty()) {
            continue;
        }

        if (editorHost_->applyReplacement(path, line, column, replaceLength, newText)) {
            ++applied;
        }
    }
    return applied;
}

int WorkerAgent::syncEditorWithDiskChanges(const QStringList& changedFiles)
{
    if (!editorHost_) {
        return 0;
    }

    int synced = 0;
    for (const QString& relativePath : changedFiles) {
        const QString resolved = editorHost_->resolvePath(relativePath);
        if (resolved.isEmpty()) {
            continue;
        }
        if (editorHost_->reloadFromDisk(resolved)) {
            editorHost_->openFileAt(resolved, 1, 1);
            ++synced;
        }
    }
    return synced;
}

WorkerAgent::WorkerExecutionResult WorkerAgent::finalizeResult(
    const QString& rawSummary,
    const QJsonObject& responseObject) const
{
    WorkerExecutionResult result;
    result.summary = responseObject.value(QStringLiteral("summary")).toString(rawSummary.trimmed());
    result.diffText = WorkerGitDiff::captureDiff(activeContext_.projectPath);
    if (result.diffText.isEmpty() && !baselineDiff_.isEmpty()) {
        result.diffText = WorkerGitDiff::captureDiff(activeContext_.projectPath);
    }

    result.touchedFiles = WorkerGitDiff::changedFiles(activeContext_.projectPath);
    return result;
}

bool WorkerAgent::writeWorkerArtifacts(qint64 taskId, const WorkerExecutionResult& result) const
{
    QJsonObject resultPayload;
    resultPayload.insert(QStringLiteral("pipeline_id"), activePipelineId_);
    resultPayload.insert(QStringLiteral("user_task"), activeUserTask_);
    resultPayload.insert(QStringLiteral("chosen_backend"), activeChosenBackend_);
    resultPayload.insert(QStringLiteral("summary"), result.summary);
    resultPayload.insert(QStringLiteral("edits_applied"), result.editsApplied);
    resultPayload.insert(QStringLiteral("files_touched"), QJsonArray::fromStringList(result.touchedFiles));

    const QJsonObject resultEnvelope =
        PipelineArtifacts::makeEnvelope(taskId,
                                        QStringLiteral("worker"),
                                        QStringLiteral("completed"),
                                        resultPayload,
                                        QStringLiteral("Worker execution finished."));

    QString markdown = QStringLiteral("# Worker Result\n\n");
    markdown += QStringLiteral("**Backend:** %1\n\n").arg(activeChosenBackend_);
    markdown += QStringLiteral("**Summary:** %1\n\n").arg(result.summary);
    markdown += QStringLiteral("**Edits applied:** %1\n\n").arg(result.editsApplied);
    if (!result.touchedFiles.isEmpty()) {
        markdown += QStringLiteral("**Files touched:**\n");
        for (const QString& file : result.touchedFiles) {
            markdown += QStringLiteral("- %1\n").arg(file);
        }
    }
    markdown += QStringLiteral("\n---\n\n");
    markdown += QString::fromUtf8(
        QJsonDocument(resultEnvelope).toJson(QJsonDocument::Indented));

    if (!PipelineQueue::writeArtifactText(projectRoot_, taskId,
                                          PipelineArtifacts::fileWorkerResultMd(), markdown)) {
        return false;
    }

    const QJsonObject diffEnvelope =
        PipelineArtifacts::makeEnvelope(taskId,
                                        QStringLiteral("worker"),
                                        QStringLiteral("completed"),
                                        QJsonObject{{QStringLiteral("diff"), result.diffText},
                                                    {QStringLiteral("files_touched"),
                                                     QJsonArray::fromStringList(result.touchedFiles)}},
                                        QStringLiteral("Repository diff after worker execution."));

    const QString diffBody = result.diffText.trimmed().isEmpty()
                                 ? QStringLiteral("# No git diff captured\n")
                                 : result.diffText;
    const QString diffDocument =
        QStringLiteral("---\nvexara_artifact: changes.diff\n---\n") + diffBody + QStringLiteral("\n\n")
        + QString::fromUtf8(QJsonDocument(diffEnvelope).toJson(QJsonDocument::Indented));

    return PipelineQueue::writeArtifactText(projectRoot_, taskId,
                                            PipelineArtifacts::fileChangesDiff(), diffDocument);
}

void WorkerAgent::enqueueTestingStage(qint64 workerTaskId)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), QStringLiteral("hierarchical_stage"));
    payload.insert(QStringLiteral("protocol_version"), 2);
    payload.insert(QStringLiteral("stage"), PipelineStages::testing());
    payload.insert(QStringLiteral("pipeline_id"), activePipelineId_);
    payload.insert(QStringLiteral("user_task"), activeUserTask_);
    payload.insert(QStringLiteral("context"), activeTaskPayload_.value(QStringLiteral("context")));
    payload.insert(QStringLiteral("worker_task_id"), workerTaskId);
    payload.insert(QStringLiteral("supervisor_task_id"),
                   activeTaskPayload_.value(QStringLiteral("supervisor_task_id")));

    const qint64 nextTaskId =
        queue_->enqueue(payload, PipelineQueuePriority::kIntraPipelineStage);
    if (nextTaskId > 0) {
        queue_->recordEvent(nextTaskId,
                            QStringLiteral("worker"),
                            QStringLiteral("handoff_enqueued"),
                            QJsonObject{{QStringLiteral("stage"), PipelineStages::testing()},
                                        {QStringLiteral("pipeline_id"), activePipelineId_}});
    }
}

void WorkerAgent::handleBackendFinished(AgentRole role, bool success, const QString& summary)
{
    if (activeTaskId_ <= 0 || role != AgentRole::Builder) {
        return;
    }

    const qint64 taskId = activeTaskId_;

    if (!success) {
        activeTaskId_ = 0;
        lastError_ = summary;
        queue_->markFailed(taskId, summary);
        queue_->recordEvent(taskId,
                            QStringLiteral("worker"),
                            QStringLiteral("failed"),
                            QJsonObject{{QStringLiteral("error"), summary.left(500)}});
        emit finished(taskId, false, summary);
        return;
    }

    QJsonObject responseObject;
    const bool structuredBackend =
        activeBackendKind_ == VexaraCore::AgentServiceKind::OpenAiHttp
        || activeBackendKind_ == VexaraCore::AgentServiceKind::OpenRouterHttp;
    if (structuredBackend) {
        const QJsonDocument doc = QJsonDocument::fromJson(extractJsonBody(summary).toUtf8());
        if (doc.isObject()) {
            responseObject = doc.object();
        }
    }

    WorkerExecutionResult result = finalizeResult(summary, responseObject);
    if (structuredBackend) {
        result.editsApplied = applyStructuredEdits(responseObject);
    } else {
        result.touchedFiles = WorkerGitDiff::changedFiles(activeContext_.projectPath);
        QStringList filesToOpen = result.touchedFiles;
        if (activeBackendKind_ == VexaraCore::AgentServiceKind::AiderCli
            && !activeContext_.targetFiles.isEmpty()) {
            QStringList scoped;
            for (const QString& touched : result.touchedFiles) {
                for (const QString& target : activeContext_.targetFiles) {
                    if (touched.endsWith(target, Qt::CaseInsensitive)
                        || touched.compare(target, Qt::CaseInsensitive) == 0) {
                        scoped.append(touched);
                        break;
                    }
                }
            }
            if (!scoped.isEmpty()) {
                filesToOpen = scoped;
            }
        }
        if (filesToOpen.size() > 2) {
            filesToOpen = filesToOpen.mid(0, 2);
        }
        result.editsApplied = syncEditorWithDiskChanges(filesToOpen);
        result.diffText = WorkerGitDiff::captureDiff(activeContext_.projectPath);

        if (activeBackendKind_ == VexaraCore::AgentServiceKind::AiderCli
            && !activeContext_.targetFiles.isEmpty()) {
            QStringList outOfScope;
            for (const QString& touched : result.touchedFiles) {
                bool allowed = false;
                for (const QString& target : activeContext_.targetFiles) {
                    if (touched.endsWith(target, Qt::CaseInsensitive)
                        || touched.compare(target, Qt::CaseInsensitive) == 0) {
                        allowed = true;
                        break;
                    }
                }
                if (!allowed) {
                    outOfScope.append(touched);
                }
            }
            if (!outOfScope.isEmpty()) {
                const QString scopeList = activeContext_.targetFiles.join(QStringLiteral(", "));
                const QString extraList = outOfScope.mid(0, 8).join(QStringLiteral(", "));
                lastError_ =
                    QStringLiteral("Aider changed %1 file(s) outside the allowed scope (%2): %3")
                        .arg(outOfScope.size())
                        .arg(scopeList, extraList);
                queue_->markFailed(taskId, lastError_);
                queue_->recordEvent(taskId,
                                    QStringLiteral("worker"),
                                    QStringLiteral("scope_violation"),
                                    QJsonObject{{QStringLiteral("allowed"), scopeList},
                                                {QStringLiteral("extra"), extraList}});
                activeTaskId_ = 0;
                emit finished(taskId, false, lastError_);
                return;
            }
        }
    }

    if (!writeWorkerArtifacts(taskId, result)) {
        lastError_ = QStringLiteral("Failed to write worker artifacts for task %1.").arg(taskId);
        queue_->markFailed(taskId, lastError_);
        activeTaskId_ = 0;
        emit finished(taskId, false, lastError_);
        return;
    }

    queue_->markCompleted(taskId);
    queue_->recordEvent(taskId,
                        QStringLiteral("worker"),
                        QStringLiteral("completed"),
                        QJsonObject{{QStringLiteral("edits_applied"), result.editsApplied},
                                    {QStringLiteral("files_touched"), result.touchedFiles.size()}});

    enqueueTestingStage(taskId);
    activeTaskId_ = 0;
    emit finished(taskId, true, result.summary);
}

} // namespace VexaraOrchestration
