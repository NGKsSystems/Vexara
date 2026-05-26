#include "VexaraOrchestration/HttpModelTaskBackend.h"

#include "VexaraOrchestration/AgentRoleIds.h"

namespace VexaraOrchestration {

HttpModelTaskBackend::HttpModelTaskBackend(VexaraCore::AgentServiceKind serviceKind, QObject* parent)
    : ITaskBackend(parent)
    , serviceKind_(serviceKind)
    , chatClient_(this)
{
    connect(&chatClient_, &AgentChatClient::requestFinished, this,
            [this](bool success, const QString& reply, const QString& errorSummary) {
                if (success) {
                    if (!reply.isEmpty()) {
                        emit outputChunk(activeRole_, reply);
                    }
                    emit taskFinished(activeRole_, true, reply);
                } else {
                    emit taskFinished(activeRole_, false, errorSummary);
                }
            });
}

VexaraCore::AgentServiceKind HttpModelTaskBackend::serviceKind() const
{
    return serviceKind_;
}

TaskBackendCapabilities HttpModelTaskBackend::capabilities() const
{
    return TaskBackendCapability::Execution | TaskBackendCapability::Planning
           | TaskBackendCapability::Review;
}

void HttpModelTaskBackend::configure(const VexaraCore::ModelSettings& models,
                                     const AgentPromptComposer& composer)
{
    models_ = models;
    models_.ensureDefaults();
    composer_ = composer;
}

bool HttpModelTaskBackend::profileMatchesService(const VexaraCore::ModelProfile& profile) const
{
    switch (serviceKind_) {
    case VexaraCore::AgentServiceKind::OpenAiHttp:
        return profile.provider == VexaraCore::ModelProvider::OpenAi;
    case VexaraCore::AgentServiceKind::OpenRouterHttp:
        return profile.provider == VexaraCore::ModelProvider::OpenRouter;
    default:
        return false;
    }
}

VexaraCore::ModelProfile HttpModelTaskBackend::profileForRole(AgentRole role) const
{
    return models_.profileById(models_.modelIdForAgent(agentIdForRole(role)));
}

bool HttpModelTaskBackend::isConfiguredForRole(AgentRole role) const
{
    const VexaraCore::ModelProfile profile = profileForRole(role);
    return profileMatchesService(profile) && profile.isUsableForChat();
}

bool HttpModelTaskBackend::isConfigured() const
{
    return isConfiguredForRole(AgentRole::Builder) || isConfiguredForRole(AgentRole::Orchestrator)
           || isConfiguredForRole(AgentRole::Supervisor);
}

bool HttpModelTaskBackend::isRunning() const
{
    return chatClient_.isBusy();
}

void HttpModelTaskBackend::executeTask(AgentRole role,
                                       const QString& composedPrompt,
                                       const TaskContext& context)
{
    if (chatClient_.isBusy()) {
        emit taskFinished(role, false, QStringLiteral("An HTTP task is already running."));
        return;
    }

    const VexaraCore::ModelProfile profile = profileForRole(role);
    if (!profileMatchesService(profile) || !profile.isUsableForChat()) {
        const QString serviceLabel = VexaraCore::agentServiceKindLabel(serviceKind_);
        emit taskFinished(
            role,
            false,
            QStringLiteral("%1 is not configured for %2. Assign %3 to a matching model profile "
                           "and add an API key.")
                .arg(serviceLabel, roleDisplayName(role), agentIdForRole(role)));
        return;
    }

    activeRole_ = role;
    emit taskStarted(role, composedPrompt);

    chatClient_.sendMessage(profile,
                            roleDisplayName(role),
                            {},
                            composedPrompt,
                            composer_.systemContextFor(role, context));
}

} // namespace VexaraOrchestration
