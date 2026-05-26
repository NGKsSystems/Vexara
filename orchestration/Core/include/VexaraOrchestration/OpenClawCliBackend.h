#pragma once



#include "VexaraCore/AgentServiceKind.h"

#include "VexaraCore/OpenClawSettings.h"

#include "VexaraOrchestration/ITaskBackend.h"

#include "VexaraOrchestration/OpenClawBridge.h"



namespace VexaraOrchestration {



class OpenClawCliBackend : public ITaskBackend {

    Q_OBJECT



public:

    explicit OpenClawCliBackend(QObject* parent = nullptr);



    VexaraCore::AgentServiceKind serviceKind() const override;

    TaskBackendCapabilities capabilities() const override;

    void configure(const VexaraCore::OpenClawSettings& settings);

    bool isConfigured() const override;

    bool isRunning() const override;

    void executeTask(AgentRole role,

                     const QString& composedPrompt,

                     const TaskContext& context) override;

    void cancelActiveTask();



private:

    OpenClawBridge bridge_;

    AgentRole activeRole_ = AgentRole::Orchestrator;
    VexaraCore::OpenClawSettings settings_;
};



} // namespace VexaraOrchestration


