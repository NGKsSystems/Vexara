#pragma once

#include "VexaraOrchestration/AgentPromptComposer.h"
#include "VexaraOrchestration/AgentRegistry.h"
#include "VexaraOrchestration/AgentSnapshot.h"
#include "VexaraOrchestration/PipelineQueue.h"
#include "VexaraOrchestration/PipelineStageIds.h"
#include "VexaraOrchestration/PlannerAgent.h"
#include "VexaraOrchestration/SupervisorAgent.h"
#include "VexaraOrchestration/WorkerAgent.h"
#include "VexaraOrchestration/TesterAgent.h"
#include "VexaraOrchestration/ReviewerAgent.h"
#include "VexaraOrchestration/IWorkerEditorHost.h"
#include "VexaraOrchestration/ITesterCommandHost.h"
#include "VexaraOrchestration/TaskBackendRegistry.h"
#include "VexaraOrchestration/TaskPipeline.h"

#include "VexaraCore/GrokTaskContext.h"
#include "VexaraCore/VerificationSettings.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

class QTimer;

namespace VexaraOrchestration {

class DirectorAgent : public QObject {
    Q_OBJECT

public:
    explicit DirectorAgent(QObject* parent = nullptr);
    ~DirectorAgent() override;

    void configure(TaskBackendRegistry* backends,
                     AgentPromptComposer* composer,
                     AgentRegistry* registry,
                     PipelineQueue* queue,
                     const QString& projectRoot,
                     IWorkerEditorHost* editorHost = nullptr,
                     ITesterCommandHost* commandHost = nullptr,
                     const VexaraCore::VerificationSettings& verification = {});
    void start();
    void stop();
    bool isActive() const;
    bool isDispatching() const;
    bool isPlannerActive() const;
    bool isSupervisorActive() const;
    bool isWorkerActive() const;
    bool isTesterActive() const;
    bool isReviewerActive() const;
    qint64 activeTaskId() const;
    QString activeStage() const;
    QString currentStageLabel() const;

    void forwardBackendFinished(AgentRole role, bool success, const QString& summary);

    qint64 enqueuePipelineStage(const QJsonObject& payload,
                                int priority = PipelineQueuePriority::kIntraPipelineStage);
    qint64 enqueuePipelineStart(const QString& userTask,
                                const VexaraCore::GrokTaskContext& context,
                                int priority = 0);

    int pendingTaskCount() const;
    int pendingRootPipelineCount() const;
    int deferredTaskCount() const;
    int clearDeferredTasks();
    int cancelPendingRootPipelines(const QString& reason = QString());
    void abortActivePipelineWork(const QString& reason = QString());
    void resumeQueue();
    bool isQueuePaused() const;
    QString queuePauseReason() const;
    QString lastError() const;

signals:
    void taskDispatched(qint64 taskId, AgentRole role, const QString& pipelineStageId);
    void taskCompleted(qint64 taskId, bool success, const QString& summary);
    void pipelineCompleted(const QString& pipelineId, bool success, const QString& summary);
    void systemStateChanged(const QString& summary);

public slots:
    void handleBackendFinished(AgentRole role, bool success, const QString& summary);

private slots:
    void poll();
    void handlePlannerFinished(qint64 taskId, bool success, const QString& summary);
    void handleSupervisorFinished(qint64 taskId, bool success, const QString& summary);
    void handleWorkerFinished(qint64 taskId, bool success, const QString& summary);
    void handleTesterFinished(qint64 taskId, bool success, const QString& summary);
    void handleReviewerFinished(qint64 taskId, bool success, const QString& summary);

private:
    struct ActivePipelineContext {
        QString pipelineId;
        QString userTask;
        TaskContext context;
        QString orchestratorPlan;
        QString builderResult;
        bool runOrchestrator = false;
        bool runSupervisor = false;
    };

    bool canPoll() const;
    void tryDispatchNext();
    void scheduleTryDispatchNext();
    void finishPipelineRun(const QString& pipelineId, bool success, const QString& summary);
    bool isOpenClawPlanningReady(QString* detail) const;
    bool shouldPauseAfterPlanningFailure(const QString& summary) const;
    void pauseQueueForPlanning(const QString& reason);
    QString pipelineIdForTask(qint64 taskId) const;
    bool dispatchTask(const PipelineTaskRecord& task);
    bool dispatchHierarchicalStage(const PipelineTaskRecord& task);
    bool dispatchPlanningStage(const PipelineTaskRecord& task);
    bool dispatchSupervisorReviewStage(const PipelineTaskRecord& task);
    bool dispatchWorkerStage(const PipelineTaskRecord& task);
    bool dispatchTestingStage(const PipelineTaskRecord& task);
    bool dispatchReviewStage(const PipelineTaskRecord& task);
    bool dispatchLegacyPipelineStage(const PipelineTaskRecord& task);
    QString stageFromPayload(const QJsonObject& payload) const;
    bool isHierarchicalPayload(const QJsonObject& payload) const;
    QString composePromptForPayload(const QJsonObject& payload) const;
    TaskContext taskContextFromPayload(const QJsonObject& payload) const;
    AgentRole roleFromPayload(const QJsonObject& payload) const;
    bool writeTaskArtifact(const PipelineTaskRecord& task, const QString& status) const;
    void patchAgentForRole(AgentRole role, AgentState state, const QString& statusLine);
    void enqueueLegacyFollowUpStage(AgentRole completedRole,
                                    const QString& summary,
                                    const PipelineTaskRecord& task);
    QJsonObject buildLegacyStagePayload(const ActivePipelineContext& pipeline,
                                        AgentRole role) const;

    TaskBackendRegistry* backends_ = nullptr;
    AgentPromptComposer* composer_ = nullptr;
    AgentRegistry* registry_ = nullptr;
    PipelineQueue* queue_ = nullptr;
    PlannerAgent planner_;
    SupervisorAgent supervisor_;
    WorkerAgent worker_;
    TesterAgent tester_;
    ReviewerAgent reviewer_;
    QString projectRoot_;
    QTimer* pollTimer_ = nullptr;

    qint64 activeTaskId_ = 0;
    AgentRole activeRole_ = AgentRole::Builder;
    QString activeStage_;
    ActivePipelineContext activePipeline_;
    QString lastError_;
    bool queuePausedForPlanning_ = false;
    QString queuePauseReason_;
    int consecutivePlanningFailures_ = 0;
};

} // namespace VexaraOrchestration
