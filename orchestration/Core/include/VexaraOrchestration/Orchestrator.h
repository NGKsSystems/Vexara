#pragma once

#include "VexaraCore/GlobalSettings.h"
#include "VexaraCore/GrokTaskContext.h"
#include "VexaraOrchestration/AgentChatClient.h"
#include "VexaraOrchestration/AgentPromptComposer.h"
#include "VexaraOrchestration/AgentRegistry.h"
#include "VexaraOrchestration/ChatMessage.h"
#include "VexaraOrchestration/TaskBackendRegistry.h"
#include "VexaraOrchestration/DirectorAgent.h"
#include "VexaraOrchestration/IWorkerEditorHost.h"
#include "VexaraOrchestration/ITesterCommandHost.h"
#include "VexaraOrchestration/PipelineQueue.h"
#include "VexaraOrchestration/TaskPipeline.h"
#include "VexaraOrchestration/VerificationRunner.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

namespace VexaraOrchestration {

class Orchestrator : public QObject {
    Q_OBJECT

public:
    explicit Orchestrator(QObject* parent = nullptr);

    const AgentRegistry& registry() const;
    AgentRegistry& registry();

    void configure(const VexaraCore::GlobalSettings& settings);
    void setProjectRoot(const QString& path);
    void setWorkerEditorHost(IWorkerEditorHost* editorHost);
    void setTesterCommandHost(ITesterCommandHost* commandHost);
    QString projectRoot() const;

    void bootstrapDefaultAgents();
    void submitTask(const QString& userTask, const VexaraCore::GrokTaskContext& context = {});
    qint64 enqueuePipelineTask(const QString& userTask,
                               const VexaraCore::GrokTaskContext& context = {});
    void enqueueThreeTestPipelineTasks(const VexaraCore::GrokTaskContext& context = {});
    void sendChat(const QString& message);
    void approvePendingChanges();
    void rejectPendingChanges();
    void keepPendingChanges();
    void deletePendingOrQueuedWork();
    void restartPipelineWork();
    void runVerification();

    bool hasPendingReview() const;
    QString lastPipelineUserTask() const;
    bool isGrokBuildConfigured() const;
    QString grokBuildCommand() const;
    bool isTaskRunning() const;
    bool isDirectorActive() const;
    bool isHierarchicalPipelineRunning() const;
    bool isHierarchicalPipelineReady() const;
    QString activePipelineStage() const;
    int pendingQueueTaskCount() const;
    int pendingRootPipelineCount() const;
    int deferredQueueTaskCount() const;
    int clearDeferredPipelineTasks();
    int cancelPendingPipelineTasks();
    void resumePipelineQueue();
    bool isPipelineQueuePaused() const;
    QString pipelineQueuePauseReason() const;
    bool isChatBusy() const;
    QVector<ChatMessage> chatHistory() const;
    QString formattedChatTranscript() const;

    VexaraCore::AgentServiceKind serviceForRole(AgentRole role) const;
    QString effectiveModelDisplayForRole(AgentRole role) const;
    bool isRoleBackendConfigured(AgentRole role) const;
    bool usesOpenClawForPlannerRoles() const;
    QString openClawPlannerStatus() const;
    bool configureOpenClawForLocalOllama(QString* message = nullptr);
    QString formattedPipelineProgress() const;

signals:
    void taskStateChanged(const QString& summary);
    void verificationStateChanged(const QString& summary);
    void chatHistoryChanged();
    void pipelineProgressChanged();

private:
    void onTaskStarted(AgentRole role, const QString& prompt);
    void onTaskOutputChunk(AgentRole role, const QString& chunk);
    void onTaskFinished(AgentRole role, bool success, const QString& summary);
    QString notConfiguredMessage(AgentRole role) const;
    bool isHttpService(VexaraCore::AgentServiceKind service) const;

    void runPipelineStage(AgentRole role);
    void handleOrchestratorFinished(bool success, const QString& summary);
    void handleBuilderFinished(bool success, const QString& summary);
    void handleSupervisorFinished(bool success, const QString& summary);
    void finishPipeline(bool success, const QString& summary);
    void appendStageToTranscript(AgentRole role, const QString& prompt);
    void appendStageOutput(AgentRole role, const QString& chunk);
    void refreshPlanFromTranscript();
    void schedulePlanRefresh();
    void emitTaskStateThrottled(const QString& summary);
    QString outputHeaderForRole(AgentRole role) const;
    void updateAgentStatusesForStage(AgentRole role, bool starting);
    bool isDirectorPipelineEngaged() const;
    void appendDirectorPipelineTaskHeader(const QString& userTask);
    void appendHierarchicalPlanLine(const QString& line);
    void appendQueueStatusLine();
    void onDirectorTaskDispatched(qint64 taskId, AgentRole role, const QString& pipelineStageId);
    void onDirectorTaskCompleted(qint64 taskId, bool success, const QString& summary);
    void onDirectorSystemStateChanged(const QString& summary);
    void onDirectorPipelineCompleted(const QString& pipelineId, bool success, const QString& summary);

    void resetPipelineProgress();
    void publishPipelineProgress();
    void onPipelinePromptReceived();
    void onPipelineStageStarted(const QString& stageLabel);
    void onPipelineStageFinished(bool success);
    QString handoffAfterStage(const QString& stageLabel) const;
    QString formatPipelineProgressText() const;

    void patchAgent(const QString& id,
                    AgentState state,
                    const QString& statusLine,
                    const QString& planSummary = QString());
    void applyModelAssignments(const VexaraCore::ModelSettings& models);
    void appendChatMessage(const QString& role, const QString& speaker, const QString& content);
    QString chatSystemContext() const;
    void onVerificationFinished(bool success, const QString& summary);
    void onChatFinished(bool success, const QString& reply, const QString& errorSummary);
    void openPipelineQueueForProject(const QString& projectRoot);
    void closePipelineQueue();

    AgentRegistry registry_;
    TaskBackendRegistry taskBackends_;
    AgentPromptComposer promptComposer_;
    AgentChatClient chatClient_;
    VerificationRunner verificationRunner_;
    DirectorAgent director_;
    PipelineQueue pipelineQueue_;
    IWorkerEditorHost* workerEditorHost_ = nullptr;
    ITesterCommandHost* testerCommandHost_ = nullptr;
    VexaraCore::GlobalSettings settings_;
    TaskPipelineState pipelineState_;
    QString projectRoot_;
    QVector<ChatMessage> chatHistory_;
    QString taskLiveOutput_;
    AgentRole activeTaskRole_ = AgentRole::Builder;
    bool pendingReview_ = false;
    bool legacyPipelineActive_ = false;
    bool hierarchicalPlanSessionActive_ = false;
    QString lastPipelineUserTask_;
    VexaraCore::GrokTaskContext lastPipelineContext_;

    struct PipelineProgressLine {
        enum class Kind { Milestone, Handoff, Stage };
        Kind kind = Kind::Milestone;
        QString text;
        QString agentLabel;
        QString actionVerb;
        QString state;
        QDateTime startedUtc;
        QDateTime endedUtc;
    };

    QVector<PipelineProgressLine> pipelineProgressLines_;
    int runningPipelineProgressIndex_ = -1;
    QString lastDispatchedStageLabel_;
    QString cachedPipelineProgressText_;

    QTimer planRefreshTimer_;
    QTimer taskStateEmitTimer_;
    QString pendingTaskStateSummary_;
};

} // namespace VexaraOrchestration
