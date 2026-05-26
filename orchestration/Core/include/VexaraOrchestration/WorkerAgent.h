#pragma once

#include "VexaraOrchestration/AgentPromptComposer.h"
#include "VexaraOrchestration/IWorkerEditorHost.h"
#include "VexaraOrchestration/PipelineQueue.h"
#include "VexaraOrchestration/TaskBackendRegistry.h"
#include "VexaraOrchestration/TaskContext.h"

#include "VexaraCore/AgentServiceKind.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace VexaraOrchestration {

struct PipelineTaskRecord;

class WorkerAgent : public QObject {
    Q_OBJECT

public:
    explicit WorkerAgent(QObject* parent = nullptr);

    void configure(TaskBackendRegistry* backends,
                   AgentPromptComposer* composer,
                   PipelineQueue* queue,
                   IWorkerEditorHost* editorHost,
                   const QString& projectRoot);

    bool isRunning() const;
    qint64 activeTaskId() const;
    QString lastError() const;

    bool execute(const PipelineTaskRecord& task);

public slots:
    void handleBackendFinished(AgentRole role, bool success, const QString& summary);

signals:
    void started(qint64 taskId);
    void finished(qint64 taskId, bool success, const QString& summary);

private:
    struct WorkerExecutionResult {
        QString summary;
        QString diffText;
        QStringList touchedFiles;
        int editsApplied = 0;
    };

    TaskContext contextFromPayload(const QJsonObject& payload) const;
    bool loadApprovedPlan(qint64 supervisorTaskId, QJsonObject& envelopeOut) const;
    VexaraCore::AgentServiceKind backendKindFromString(const QString& value) const;
    bool isBackendReady(VexaraCore::AgentServiceKind kind) const;
    QString extractJsonBody(const QString& rawResponse) const;
    int applyStructuredEdits(const QJsonObject& responseObject);
    int syncEditorWithDiskChanges(const QStringList& changedFiles);
    WorkerExecutionResult finalizeResult(const QString& rawSummary,
                                         const QJsonObject& responseObject) const;
    bool writeWorkerArtifacts(qint64 taskId, const WorkerExecutionResult& result) const;
    void enqueueTestingStage(qint64 workerTaskId);

    TaskBackendRegistry* backends_ = nullptr;
    AgentPromptComposer* composer_ = nullptr;
    PipelineQueue* queue_ = nullptr;
    IWorkerEditorHost* editorHost_ = nullptr;
    QString projectRoot_;
    QString lastError_;

    qint64 activeTaskId_ = 0;
    QJsonObject activeTaskPayload_;
    QJsonObject activeApprovedPayload_;
    QString activePipelineId_;
    QString activeUserTask_;
    QString activeWorkerInstructions_;
    QString activeChosenBackend_;
    VexaraCore::AgentServiceKind activeBackendKind_ = VexaraCore::AgentServiceKind::None;
    TaskContext activeContext_;
    QString baselineDiff_;
};

} // namespace VexaraOrchestration
