#include "VexaraOrchestration/OpenClawCliBackend.h"

namespace VexaraOrchestration {



OpenClawCliBackend::OpenClawCliBackend(QObject* parent)

    : ITaskBackend(parent)

    , bridge_(this)

{

    connect(&bridge_, &OpenClawBridge::taskStarted, this, [this](const QString& prompt) {

        emit taskStarted(activeRole_, prompt);

    });

    connect(&bridge_, &OpenClawBridge::outputChunk, this,

            [this](const QString& chunk) { emit outputChunk(activeRole_, chunk); });

    connect(&bridge_, &OpenClawBridge::taskFinished, this,

            [this](bool success, const QString& summary) {

                emit taskFinished(activeRole_, success, summary);

            });

}



VexaraCore::AgentServiceKind OpenClawCliBackend::serviceKind() const

{

    return VexaraCore::AgentServiceKind::OpenClawCli;

}



TaskBackendCapabilities OpenClawCliBackend::capabilities() const

{

    return TaskBackendCapability::Planning | TaskBackendCapability::Review;

}



void OpenClawCliBackend::configure(const VexaraCore::OpenClawSettings& settings)
{
    settings_ = settings;
    settings_.ensureDefaults();
    bridge_.configure(settings_);
}

bool OpenClawCliBackend::isConfigured() const
{
    return bridge_.isConfigured();
}



bool OpenClawCliBackend::isRunning() const

{

    return bridge_.isRunning();

}



void OpenClawCliBackend::executeTask(AgentRole role,

                                     const QString& composedPrompt,

                                     const TaskContext& context)

{

    activeRole_ = role;
    settings_.ensureDefaults();
    bridge_.configure(settings_);
    bridge_.runTask(composedPrompt.trimmed(), context.projectPath.trimmed());
}

void OpenClawCliBackend::cancelActiveTask()
{
    bridge_.cancelActiveTask();
}



} // namespace VexaraOrchestration


