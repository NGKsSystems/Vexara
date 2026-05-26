#pragma once

#include "VexaraCore/AgentServiceKind.h"
#include "VexaraOrchestration/PipelineQueue.h"
#include "VexaraOrchestration/TaskBackendRegistry.h"
#include "VexaraOrchestration/TaskContext.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace VexaraOrchestration {

struct PipelineTaskRecord;

class SupervisorAgent : public QObject {
    Q_OBJECT

public:
    explicit SupervisorAgent(QObject* parent = nullptr);

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
    void finished(qint64 taskId, bool success, const QString& summary);
    void reworkRequested(qint64 taskId, const QString& pipelineId, const QString& feedback);

private:
    struct SupervisorDecision {
        QString decision;
        double confidence = 0.0;
        QString reasoning;
        QString chosenBackend;
        QJsonObject approvedPlan;
        QJsonArray planIssues;
        QString riskAssessment;
        QString workerInstructions;
    };

    TaskContext contextFromPayload(const QJsonObject& payload) const;
    bool loadPlanArtifact(qint64 planTaskId, QJsonObject& planEnvelopeOut) const;
    QJsonArray availableWorkerBackends() const;
    bool isBackendConfigured(VexaraCore::AgentServiceKind kind) const;
    QString extractJsonBody(const QString& rawResponse) const;
    SupervisorDecision parseDecision(const QString& rawResponse) const;
    QString normalizeBackendChoice(const QString& choice, const QJsonArray& catalog) const;
    QJsonObject buildApprovedPlanEnvelope(qint64 taskId,
                                          const SupervisorDecision& decision) const;
    QJsonObject buildWorkerStagePayload(const QJsonObject& taskPayload,
                                        qint64 supervisorTaskId,
                                        const SupervisorDecision& decision) const;
    QJsonObject buildPlanningReworkPayload(const QJsonObject& taskPayload,
                                           qint64 supervisorTaskId,
                                           const SupervisorDecision& decision) const;
    void enqueuePlanningRework(qint64 supervisorTaskId, const SupervisorDecision& decision);
    void enqueueWorkerStage(qint64 supervisorTaskId, const SupervisorDecision& decision);

    TaskBackendRegistry* backends_ = nullptr;
    AgentPromptComposer* composer_ = nullptr;
    PipelineQueue* queue_ = nullptr;
    QString projectRoot_;
    QString lastError_;

    qint64 activeTaskId_ = 0;
    QJsonObject activeTaskPayload_;
    QJsonObject activePlanPayload_;
    qint64 activePlanTaskId_ = 0;
    QString activePipelineId_;
    QString activeUserTask_;
    TaskContext activeContext_;
};

} // namespace VexaraOrchestration
