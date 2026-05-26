#pragma once



#include "VexaraCore/GrokBuildSettings.h"
#include "VexaraCore/AgentServiceKind.h"

#include "VexaraCore/GrokTaskContext.h"
#include "VexaraCore/OpenClawSettings.h"

#include "VexaraOrchestration/AgentSnapshot.h"

#include "VexaraOrchestration/TaskContext.h"



#include <QJsonArray>
#include <QJsonObject>
#include <QString>



namespace VexaraOrchestration {



class AgentPromptComposer {

public:

    void configure(const VexaraCore::GrokBuildSettings& grokBuild,

                   const VexaraCore::OpenClawSettings& openClaw);



    QString composeUserPrompt(AgentRole role,

                              const QString& userTask,

                              const TaskContext& context) const;



    QString systemContextFor(AgentRole role, const TaskContext& context) const;

    QString composeBuilderPromptWithPlan(const QString& userTask,
                                         const QString& orchestratorPlan,
                                         const TaskContext& context) const;

    QString composeSupervisorReviewPrompt(const QString& userTask,
                                          const QString& builderResult,
                                          const TaskContext& context) const;

    QString composePlannerPrompt(const QString& userTask, const TaskContext& context) const;

    QString composeSupervisorPlanReviewPrompt(const QString& userTask,
                                              const QJsonObject& planPayload,
                                              const QJsonArray& availableBackends,
                                              const TaskContext& context) const;

    QString composeWorkerPrompt(const QString& userTask,
                                const QJsonObject& approvedPlanPayload,
                                const QString& workerInstructions,
                                const TaskContext& context,
                                bool structuredEditsExpected,
                                VexaraCore::AgentServiceKind cliBackendKind
                                    = VexaraCore::AgentServiceKind::None) const;

    QString composeTesterEvaluationPrompt(const QString& userTask,
                                          const QJsonObject& approvedPlanPayload,
                                          const QString& workerResultMarkdown,
                                          const QJsonArray& commandResults,
                                          const TaskContext& context) const;

    QString composeReviewerPrompt(const QString& userTask,
                                  const QJsonObject& approvedPlanPayload,
                                  const QString& workerResultMarkdown,
                                  const QJsonObject& testResultsPayload,
                                  const TaskContext& context) const;

private:

    QString composeOrchestratorPrompt(const QString& userTask, const TaskContext& context) const;

    QString composeSupervisorPrompt(const QString& userTask, const TaskContext& context) const;



    VexaraCore::GrokBuildSettings grokBuild_;

    VexaraCore::OpenClawSettings openClaw_;

};



} // namespace VexaraOrchestration


