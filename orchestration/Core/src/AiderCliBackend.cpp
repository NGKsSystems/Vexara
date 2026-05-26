#include "VexaraOrchestration/AiderCliBackend.h"

#include <QFileInfo>

namespace VexaraOrchestration {

AiderCliBackend::AiderCliBackend(QObject* parent)
    : ITaskBackend(parent)
    , bridge_(this)
{
    connect(&bridge_, &AiderBridge::taskStarted, this, [this](const QString& prompt) {
        emit taskStarted(activeRole_, prompt);
    });
    connect(&bridge_, &AiderBridge::outputChunk, this,
            [this](const QString& chunk) { emit outputChunk(activeRole_, chunk); });
    connect(&bridge_, &AiderBridge::taskFinished, this,
            [this](bool success, const QString& summary) {
                emit taskFinished(activeRole_, success, summary);
            });
}

VexaraCore::AgentServiceKind AiderCliBackend::serviceKind() const
{
    return VexaraCore::AgentServiceKind::AiderCli;
}

TaskBackendCapabilities AiderCliBackend::capabilities() const
{
    return TaskBackendCapability::Execution;
}

void AiderCliBackend::configure(const VexaraCore::AiderSettings& settings)
{
    settings_ = settings;
    settings_.ensureDefaults();
    bridge_.configure(settings_);
}

bool AiderCliBackend::isConfigured() const
{
    return bridge_.isConfigured();
}

bool AiderCliBackend::isRunning() const
{
    return bridge_.isRunning();
}

void AiderCliBackend::executeTask(AgentRole role,
                                    const QString& composedPrompt,
                                    const TaskContext& context)
{
    activeRole_ = role;

    settings_.ensureDefaults();
    bridge_.configure(settings_);

    const QString projectRoot = context.projectPath.trimmed();
    const QFileInfo projectDir(projectRoot);
    const QString workingDirectory =
        projectDir.isDir() ? projectDir.absoluteFilePath() : QString();

    bridge_.runTask(composedPrompt.trimmed(), workingDirectory, settings_.resolvedCommand(),
                     context.targetFiles);
}

void AiderCliBackend::cancelActiveTask()
{
    bridge_.cancelActiveTask();
}

} // namespace VexaraOrchestration
