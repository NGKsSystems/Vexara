#pragma once

#include "VexaraCore/VerificationSettings.h"
#include "VexaraOrchestration/AgentPromptComposer.h"
#include "VexaraOrchestration/ITesterCommandHost.h"
#include "VexaraOrchestration/PipelineQueue.h"
#include "VexaraOrchestration/TaskBackendRegistry.h"
#include "VexaraOrchestration/TaskContext.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

class QProcess;
class QTimer;

namespace VexaraOrchestration {

struct PipelineTaskRecord;

class TesterAgent : public QObject {
    Q_OBJECT

public:
    explicit TesterAgent(QObject* parent = nullptr);
    ~TesterAgent() override;

    void configure(TaskBackendRegistry* backends,
                   AgentPromptComposer* composer,
                   PipelineQueue* queue,
                   ITesterCommandHost* commandHost,
                   const VexaraCore::VerificationSettings& verification,
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
    enum class Phase { Idle, RunningCommands, RunningEvaluation };

    struct CommandRunResult {
        QString command;
        int exitCode = -1;
        bool success = false;
        QString output;
    };

    struct TesterDecision {
        QString overallVerdict;
        QString summary;
        QJsonArray subtaskResults;
        QJsonArray issues;
        QString reasoning;
    };

    TaskContext contextFromPayload(const QJsonObject& payload) const;
    bool loadApprovedPlan(qint64 supervisorTaskId, QJsonObject& envelopeOut) const;
    bool loadWorkerResult(qint64 workerTaskId, QString& markdownOut) const;
    QStringList buildCommandQueue() const;
    void beginCommandPhase();
    void runNextCommand();
    void finishCurrentCommand(int exitCode, bool crashed);
    void beginEvaluationPhase();
    QString extractJsonBody(const QString& rawResponse) const;
    TesterDecision parseDecision(const QString& rawResponse) const;
    QJsonObject buildTestResultsPayload(const TesterDecision& decision) const;
    bool writeTestResultsArtifact(qint64 taskId, const TesterDecision& decision) const;
    void enqueueReviewStage(qint64 testingTaskId);
    void completeTask(bool success, const QString& summary);

    TaskBackendRegistry* backends_ = nullptr;
    AgentPromptComposer* composer_ = nullptr;
    PipelineQueue* queue_ = nullptr;
    ITesterCommandHost* commandHost_ = nullptr;
    VexaraCore::VerificationSettings verification_;
    QString projectRoot_;
    QString lastError_;

    QProcess* commandProcess_ = nullptr;
    QTimer* commandTimeoutTimer_ = nullptr;

    Phase phase_ = Phase::Idle;
    qint64 activeTaskId_ = 0;
    QJsonObject activeTaskPayload_;
    QJsonObject activeApprovedPayload_;
    QString activePipelineId_;
    QString activeUserTask_;
    qint64 activeWorkerTaskId_ = 0;
    qint64 activeSupervisorTaskId_ = 0;
    TaskContext activeContext_;
    QString workerResultMarkdown_;

    QStringList commandQueue_;
    int commandIndex_ = 0;
    QVector<CommandRunResult> commandRuns_;
    QString activeCommandOutput_;
};

} // namespace VexaraOrchestration
