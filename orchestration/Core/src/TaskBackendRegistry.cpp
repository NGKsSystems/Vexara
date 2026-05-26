#include "VexaraOrchestration/TaskBackendRegistry.h"



#include "VexaraOrchestration/AgentRoleIds.h"



namespace VexaraOrchestration {



TaskBackendRegistry::TaskBackendRegistry(QObject* parent)

    : QObject(parent)

    , openAiHttp_(VexaraCore::AgentServiceKind::OpenAiHttp, this)

    , openRouterHttp_(VexaraCore::AgentServiceKind::OpenRouterHttp, this)

{

    wireBackend(grokCli_);

    wireBackend(openAiHttp_);

    wireBackend(openRouterHttp_);

    wireBackend(openClawCli_);
    wireBackend(aiderCli_);
}



void TaskBackendRegistry::wireBackend(ITaskBackend& backend)

{

    connect(&backend, &ITaskBackend::taskStarted, this, &TaskBackendRegistry::taskStarted);

    connect(&backend, &ITaskBackend::outputChunk, this, &TaskBackendRegistry::outputChunk);

    connect(&backend, &ITaskBackend::taskFinished, this, &TaskBackendRegistry::taskFinished);

}



void TaskBackendRegistry::configure(const VexaraCore::GlobalSettings& settings,

                                    const AgentPromptComposer& composer)

{

    execution_ = settings.agentExecution;

    execution_.ensureDefaults();

    grokCli_.configure(settings.grokBuild);

    openAiHttp_.configure(settings.models, composer);

    openRouterHttp_.configure(settings.models, composer);

    openClawCli_.configure(execution_.openclaw);
    refreshOpenClawCliSettings();
}

void TaskBackendRegistry::refreshOpenClawCliSettings()
{
    openClawCli_.configure(execution_.openclaw);
}

void TaskBackendRegistry::refreshAiderCliSettings()
{
    aiderCli_.configure(execution_.aider);
}

const VexaraCore::AiderSettings& TaskBackendRegistry::aiderSettings() const
{
    return execution_.aider;
}



VexaraCore::AgentServiceKind TaskBackendRegistry::serviceForRole(AgentRole role) const

{

    return execution_.serviceForRoleKey(roleKeyForRole(role));

}



ITaskBackend* TaskBackendRegistry::backendForService(VexaraCore::AgentServiceKind service)

{

    switch (service) {

    case VexaraCore::AgentServiceKind::GrokCli:

        return &grokCli_;

    case VexaraCore::AgentServiceKind::OpenAiHttp:

        return &openAiHttp_;

    case VexaraCore::AgentServiceKind::OpenRouterHttp:

        return &openRouterHttp_;

    case VexaraCore::AgentServiceKind::OpenClawCli:
        refreshOpenClawCliSettings();
        return &openClawCli_;

    case VexaraCore::AgentServiceKind::AiderCli:
        refreshAiderCliSettings();
        return &aiderCli_;

    case VexaraCore::AgentServiceKind::None:

        return nullptr;

    }

    return nullptr;

}



ITaskBackend* TaskBackendRegistry::backendForRole(AgentRole role)

{

    return backendForService(serviceForRole(role));

}



const ITaskBackend* TaskBackendRegistry::backendForRole(AgentRole role) const

{

    return const_cast<TaskBackendRegistry*>(this)->backendForRole(role);

}



bool TaskBackendRegistry::isRoleConfigured(AgentRole role) const

{

    const ITaskBackend* backend = backendForRole(role);

    if (!backend) {

        return false;

    }

    if (const auto* http = dynamic_cast<const HttpModelTaskBackend*>(backend)) {
        return http->isConfiguredForRole(role);
    }

    return backend->isConfigured();
}



bool TaskBackendRegistry::anyBackendRunning() const

{

    return grokCli_.isRunning() || openAiHttp_.isRunning() || openRouterHttp_.isRunning()
           || openClawCli_.isRunning() || aiderCli_.isRunning();
}

void TaskBackendRegistry::cancelRunningTasks()
{
    if (grokCli_.isRunning()) {
        grokCli_.cancelActiveTask();
    }
    if (openClawCli_.isRunning()) {
        openClawCli_.cancelActiveTask();
    }
    if (aiderCli_.isRunning()) {
        aiderCli_.cancelActiveTask();
    }
}



GrokBuildCliBackend& TaskBackendRegistry::grokCli()

{

    return grokCli_;

}



HttpModelTaskBackend& TaskBackendRegistry::openAiHttp()

{

    return openAiHttp_;

}



HttpModelTaskBackend& TaskBackendRegistry::openRouterHttp()

{

    return openRouterHttp_;

}



OpenClawCliBackend& TaskBackendRegistry::openClawCli()
{
    return openClawCli_;
}

const VexaraCore::OpenClawSettings& TaskBackendRegistry::openClawSettings() const
{
    return execution_.openclaw;
}

AiderCliBackend& TaskBackendRegistry::aiderCli()
{
    return aiderCli_;
}

} // namespace VexaraOrchestration


