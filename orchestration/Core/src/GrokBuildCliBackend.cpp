#include "VexaraOrchestration/GrokBuildCliBackend.h"



namespace VexaraOrchestration {



GrokBuildCliBackend::GrokBuildCliBackend(QObject* parent)

    : ITaskBackend(parent)

    , bridge_(this)

{

    connect(&bridge_, &GrokBuildBridge::taskStarted, this, [this](const QString& prompt) {

        emit taskStarted(activeRole_, prompt);

    });

    connect(&bridge_, &GrokBuildBridge::outputChunk, this,

            [this](const QString& chunk) { emit outputChunk(activeRole_, chunk); });

    connect(&bridge_, &GrokBuildBridge::taskFinished, this,

            [this](bool success, const QString& summary) {

                emit taskFinished(activeRole_, success, summary);

            });

}



VexaraCore::AgentServiceKind GrokBuildCliBackend::serviceKind() const

{

    return VexaraCore::AgentServiceKind::GrokCli;

}



TaskBackendCapabilities GrokBuildCliBackend::capabilities() const

{

    return TaskBackendCapability::Execution;

}



void GrokBuildCliBackend::configure(const VexaraCore::GrokBuildSettings& settings)

{

    bridge_.configure(settings);

}



bool GrokBuildCliBackend::isConfigured() const

{

    return bridge_.isConfigured();

}



bool GrokBuildCliBackend::isRunning() const

{

    return bridge_.isRunning();

}



void GrokBuildCliBackend::executeTask(AgentRole role,

                                      const QString& composedPrompt,

                                      const TaskContext& context)

{

    activeRole_ = role;

    bridge_.runTask(composedPrompt, context.projectPath.trimmed());

}

void GrokBuildCliBackend::cancelActiveTask()
{
    bridge_.cancelActiveTask();
}



} // namespace VexaraOrchestration


