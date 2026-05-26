#include "VexaraOrchestration/OpenClawBridge.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

namespace VexaraOrchestration {

OpenClawBridge::OpenClawBridge(QObject* parent)
    : QObject(parent)
    , process_(new QProcess(this))
{
    connect(process_, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString chunk = QString::fromLocal8Bit(process_->readAllStandardOutput());
        if (!chunk.isEmpty()) {
            capturedOutput_.append(chunk);
            emit outputChunk(chunk);
        }
    });
    connect(process_, &QProcess::readyReadStandardError, this, [this]() {
        const QString chunk = QString::fromLocal8Bit(process_->readAllStandardError());
        if (!chunk.isEmpty()) {
            capturedOutput_.append(chunk);
            emit outputChunk(chunk);
        }
    });
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                const bool success = status == QProcess::NormalExit && exitCode == 0;
                QString summary = capturedOutput_.trimmed();
                if (!success) {
                    summary = settings_.friendlyErrorMessage(summary);
                } else if (summary.length() > 4000) {
                    summary = summary.right(4000);
                }
                if (summary.isEmpty()) {
                    summary = success ? QStringLiteral("OpenClaw finished successfully.")
                                      : QStringLiteral("OpenClaw failed (exit %1).").arg(exitCode);
                }
                finishProcess(success, summary);
            });
}

void OpenClawBridge::configure(const VexaraCore::OpenClawSettings& settings)
{
    settings_ = settings;
    settings_.ensureDefaults();
    localOllamaConfigured_ = false;
}

bool OpenClawBridge::isConfigured() const
{
    return settings_.isConfigured();
}

bool OpenClawBridge::isRunning() const
{
    return process_->state() != QProcess::NotRunning;
}

void OpenClawBridge::runTask(const QString& prompt, const QString& workingDirectory)
{
    settings_.ensureDefaults();

    if (!settings_.isConfigured()) {
        emit taskFinished(false,
                         QStringLiteral("OpenClaw is not configured. Set the executable in "
                                        "Settings → OpenClaw CLI."));
        return;
    }
    if (isRunning()) {
        emit taskFinished(false, QStringLiteral("OpenClaw is already running."));
        return;
    }

    const QString trimmedPrompt = prompt.trimmed();
    if (trimmedPrompt.isEmpty()) {
        emit taskFinished(false, QStringLiteral("OpenClaw task prompt is empty."));
        return;
    }

    if (settings_.usesLocalOllama()) {
        QString readinessDetail;
        if (!settings_.isLocalOllamaReady(&readinessDetail)) {
            emit taskFinished(
                false,
                QStringLiteral("%1 Use \"Configure for Ollama\" in the Agents panel.").arg(readinessDetail));
            return;
        }

        if (!localOllamaConfigured_) {
            const VexaraCore::OpenClawSettings::LocalSetupResult setup =
                settings_.applyLocalOllamaConfiguration();
            if (setup.success) {
                localOllamaConfigured_ = true;
                emit outputChunk(QStringLiteral("[Vexara] %1\n").arg(setup.message));
            } else {
                emit taskFinished(false, settings_.friendlyErrorMessage(setup.message));
                return;
            }
        }
    }

    const QFileInfo workingDirInfo(workingDirectory.trimmed());
    const QString resolvedWorkingDirectory =
        workingDirInfo.isDir() ? workingDirInfo.absoluteFilePath() : QString();

    const QStringList arguments =
        settings_.resolveArguments(trimmedPrompt, resolvedWorkingDirectory);
    if (arguments.isEmpty()) {
        emit taskFinished(false, QStringLiteral("OpenClaw arguments are empty."));
        return;
    }

    capturedOutput_.clear();
    taskFinishedEmitted_ = false;
    emit taskStarted(trimmedPrompt);

    process_->setProgram(settings_.command);
    process_->setArguments(arguments);
    process_->setProcessEnvironment(settings_.processEnvironment());
    if (!resolvedWorkingDirectory.isEmpty()) {
        process_->setWorkingDirectory(resolvedWorkingDirectory);
    }

    process_->start();
    if (!process_->waitForStarted(5000)) {
        finishProcess(false,
                      QStringLiteral("Failed to start OpenClaw: %1 (%2)")
                          .arg(settings_.command, process_->errorString()));
        return;
    }

    QTimer::singleShot(settings_.timeoutMs, this, [this]() {
        if (isRunning()) {
            process_->kill();
            finishProcess(false, QStringLiteral("OpenClaw timed out."));
        }
    });
}

void OpenClawBridge::cancelActiveTask()
{
    if (!isRunning()) {
        return;
    }
    finishProcess(false, QStringLiteral("OpenClaw cancelled."));
}

void OpenClawBridge::finishProcess(bool success, const QString& summary)
{
    if (taskFinishedEmitted_) {
        return;
    }
    taskFinishedEmitted_ = true;
    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(3000);
    }
    emit taskFinished(success, summary);
}

} // namespace VexaraOrchestration
