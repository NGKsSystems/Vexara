#pragma once

#include "VexaraOrchestration/AgentPromptComposer.h"
#include "VexaraOrchestration/PipelineQueue.h"
#include "VexaraOrchestration/TaskBackendRegistry.h"
#include "VexaraOrchestration/TaskContext.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace VexaraOrchestration {

struct PipelineTaskRecord;

class PlannerAgent : public QObject {
    Q_OBJECT

public:
    explicit PlannerAgent(QObject* parent = nullptr);

    void configure(TaskBackendRegistry* backends,
                   AgentPromptComposer* composer,
                   PipelineQueue* queue,
                   const QString& projectRoot);

    bool isRunning() const;
    qint64 activeTaskId() const;
    QString lastError() const;

    bool execute(const PipelineTaskRecord& task);

public slots:
    void handleBackendFinished(AgentRole role, bool success, const QString& summary);

signals:
    void started(qint64 taskId);
    void outputChunk(qint64 taskId, const QString& chunk);
    void finished(qint64 taskId, bool success, const QString& summary);

private:
    TaskContext contextFromPayload(const QJsonObject& payload) const;
    QJsonObject buildPlanEnvelope(qint64 taskId,
                                  const QString& pipelineId,
                                  const QString& userTask,
                                  const QString& rawResponse) const;
    QJsonObject buildSupervisorStagePayload(const QJsonObject& taskPayload,
                                            qint64 planTaskId) const;
    QJsonObject parseStructuredPlan(const QString& rawResponse) const;
    QString extractJsonBody(const QString& rawResponse) const;

    TaskBackendRegistry* backends_ = nullptr;
    AgentPromptComposer* composer_ = nullptr;
    PipelineQueue* queue_ = nullptr;
    QString projectRoot_;
    QString lastError_;

    qint64 activeTaskId_ = 0;
    QJsonObject activeTaskPayload_;
    QString activePipelineId_;
    QString activeUserTask_;
    TaskContext activeContext_;
};

} // namespace VexaraOrchestration
