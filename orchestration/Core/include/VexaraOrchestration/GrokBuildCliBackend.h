#pragma once



#include "VexaraCore/GrokBuildSettings.h"

#include "VexaraOrchestration/GrokBuildBridge.h"

#include "VexaraOrchestration/ITaskBackend.h"



namespace VexaraOrchestration {



class GrokBuildCliBackend : public ITaskBackend {

    Q_OBJECT



public:

    explicit GrokBuildCliBackend(QObject* parent = nullptr);



    VexaraCore::AgentServiceKind serviceKind() const override;

    TaskBackendCapabilities capabilities() const override;

    void configure(const VexaraCore::GrokBuildSettings& settings);

    bool isConfigured() const override;

    bool isRunning() const override;

    void executeTask(AgentRole role,

                     const QString& composedPrompt,

                     const TaskContext& context) override;

    void cancelActiveTask();



private:

    GrokBuildBridge bridge_;

    AgentRole activeRole_ = AgentRole::Builder;

};



} // namespace VexaraOrchestration


