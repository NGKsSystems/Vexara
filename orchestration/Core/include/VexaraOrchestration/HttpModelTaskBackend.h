#pragma once

#include "VexaraCore/AgentServiceKind.h"
#include "VexaraCore/ModelSettings.h"
#include "VexaraOrchestration/AgentChatClient.h"
#include "VexaraOrchestration/AgentPromptComposer.h"
#include "VexaraOrchestration/ITaskBackend.h"

namespace VexaraOrchestration {

class HttpModelTaskBackend : public ITaskBackend {
    Q_OBJECT

public:
    explicit HttpModelTaskBackend(VexaraCore::AgentServiceKind serviceKind, QObject* parent = nullptr);

    VexaraCore::AgentServiceKind serviceKind() const override;
    TaskBackendCapabilities capabilities() const override;
    void configure(const VexaraCore::ModelSettings& models, const AgentPromptComposer& composer);
    bool isConfiguredForRole(AgentRole role) const;
    bool isConfigured() const override;
    bool isRunning() const override;
    void executeTask(AgentRole role,
                     const QString& composedPrompt,
                     const TaskContext& context) override;

private:
    bool profileMatchesService(const VexaraCore::ModelProfile& profile) const;
    VexaraCore::ModelProfile profileForRole(AgentRole role) const;

    const VexaraCore::AgentServiceKind serviceKind_;
    AgentChatClient chatClient_;
    VexaraCore::ModelSettings models_;
    AgentPromptComposer composer_;
    AgentRole activeRole_ = AgentRole::Builder;
};

} // namespace VexaraOrchestration
