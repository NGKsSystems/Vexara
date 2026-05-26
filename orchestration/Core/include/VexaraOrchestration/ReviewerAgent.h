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

class ReviewerAgent : public QObject {
    Q_OBJECT

public:
    explicit ReviewerAgent(QObject* parent = nullptr);

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
    void pipelineApproved(const QString& pipelineId, const QString& summary);

private:
    struct ReviewDecision {
        QString decision;
        double confidence = 0.0;
        QString reasoning;
        QString reworkStage;
        QString summary;
        QJsonArray issues;
    };

    TaskContext contextFromPayload(const QJsonObject& payload) const;
    bool loadApprovedPlan(qint64 supervisorTaskId, QJsonObject& envelopeOut) const;
    bool loadWorkerResult(qint64 workerTaskId, QString& markdownOut) const;
    bool loadTestResults(qint64 testingTaskId, QJsonObject& envelopeOut) const;
    QString extractJsonBody(const QString& rawResponse) const;
    ReviewDecision parseDecision(const QString& rawResponse) const;
    QString normalizeReworkStage(const QString& stage) const;
    QJsonObject buildReviewDecisionPayload(const ReviewDecision& decision) const;
    bool writeReviewDecisionArtifact(qint64 taskId, const ReviewDecision& decision) const;
    QJsonObject buildPlanningReworkPayload(qint64 reviewerTaskId, const ReviewDecision& decision) const;
    QJsonObject buildSupervisorReworkPayload(qint64 reviewerTaskId, const ReviewDecision& decision) const;
    QJsonObject buildWorkerReworkPayload(qint64 reviewerTaskId, const ReviewDecision& decision) const;
    QJsonObject buildTesterReworkPayload(qint64 reviewerTaskId, const ReviewDecision& decision) const;
    QJsonObject buildEscalationPayload(qint64 reviewerTaskId, const ReviewDecision& decision) const;
    void enqueueReworkStage(qint64 reviewerTaskId, const ReviewDecision& decision);

    TaskBackendRegistry* backends_ = nullptr;
    AgentPromptComposer* composer_ = nullptr;
    PipelineQueue* queue_ = nullptr;
    QString projectRoot_;
    QString lastError_;

    qint64 activeTaskId_ = 0;
    QJsonObject activeTaskPayload_;
    QJsonObject activeApprovedPayload_;
    QJsonObject activeTestResultsPayload_;
    QString activeWorkerResultMarkdown_;
    QString activePipelineId_;
    QString activeUserTask_;
    qint64 activeWorkerTaskId_ = 0;
    qint64 activeSupervisorTaskId_ = 0;
    qint64 activeTestingTaskId_ = 0;
    TaskContext activeContext_;
};

} // namespace VexaraOrchestration
