#include "VexaraOrchestration/AiderBridge.h"

#include "VexaraCore/OpenClawSettings.h"

#include <QFileInfo>
#include <QProcess>
#include <QTimer>

namespace VexaraOrchestration {

namespace {

bool isMessageFlag(const QString& arg)
{
    return arg == QStringLiteral("--message") || arg == QStringLiteral("-m");
}

bool argumentsIncludeMessage(const QStringList& arguments)
{
    for (int i = 0; i < arguments.size(); ++i) {
        if (!isMessageFlag(arguments.at(i))) {
            continue;
        }
        if (i + 1 < arguments.size() && !arguments.at(i + 1).isEmpty()
            && !arguments.at(i + 1).startsWith(QLatin1Char('-'))) {
            return true;
        }
    }
    return false;
}

} // namespace

AiderBridge::AiderBridge(QObject* parent)
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
                if (summary.length() > 4000) {
                    summary = summary.right(4000);
                }
                if (summary.isEmpty()) {
                    summary = success ? QStringLiteral("Aider finished successfully.")
                                      : QStringLiteral("Aider failed (exit %1).").arg(exitCode);
                }
                finishProcess(success, summary);
            });
}

void AiderBridge::configure(const VexaraCore::AiderSettings& settings)
{
    settings_ = settings;
    settings_.ensureDefaults();
}

bool AiderBridge::isConfigured() const
{
    return settings_.isConfigured();
}

bool AiderBridge::isRunning() const
{
    return process_->state() != QProcess::NotRunning;
}

void AiderBridge::runTask(const QString& prompt,
                          const QString& workingDirectory,
                          const QString& programOverride,
                          const QStringList& targetFiles)
{
    settings_.ensureDefaults();

    const QString program = programOverride.trimmed().isEmpty() ? settings_.resolvedCommand()
                                                                  : programOverride.trimmed();
    if (program.isEmpty()
        || program.compare(QStringLiteral("aider"), Qt::CaseInsensitive) == 0) {
        emit taskFinished(
            false,
            QStringLiteral("Aider executable path is not configured. Set the full path in "
                             "Settings → Aider CLI → Executable (not the bare name \"aider\")."));
        return;
    }
    if (!QFileInfo::exists(program)) {
        emit taskFinished(false,
                         QStringLiteral("Aider executable not found at: %1").arg(program));
        return;
    }
    if (!settings_.isConfigured()) {
        emit taskFinished(false,
                         QStringLiteral("Aider is not configured. Set aider.command and aider.model "
                                        "in agent_execution (vexara.json)."));
        return;
    }
    if (isRunning()) {
        emit taskFinished(false, QStringLiteral("Aider is already running."));
        return;
    }

    const QFileInfo workingDirInfo(workingDirectory.trimmed());
    if (!workingDirInfo.isDir()) {
        emit taskFinished(false,
                         QStringLiteral("Open a project folder before running Aider in the pipeline."));
        return;
    }

    const QString resolvedWorkingDirectory = workingDirInfo.absoluteFilePath();
    const QString trimmedPrompt = prompt.trimmed();
    if (trimmedPrompt.isEmpty()) {
        emit taskFinished(false, QStringLiteral("Aider task prompt is empty."));
        return;
    }

    const QStringList arguments =
        settings_.resolveArguments(trimmedPrompt, resolvedWorkingDirectory, targetFiles);
    if (arguments.isEmpty()) {
        emit taskFinished(false,
                         QStringLiteral("Aider arguments are empty. Configure aider.args in "
                                        "Settings → Aider CLI → Arguments."));
        return;
    }
    if (!argumentsIncludeMessage(arguments)) {
        emit taskFinished(
            false,
            QStringLiteral("Aider arguments must include --message with {prompt}. "
                           "Example: --model {model} --message {prompt} --yes --exit"));
        return;
    }

    capturedOutput_.clear();
    emit taskStarted(trimmedPrompt);

    if (settings_.usesOllamaModel()) {
        const QString baseUrl = settings_.resolvedOllamaBaseUrl();
        const VexaraCore::OpenClawSettings::OllamaRuntimeStatus runtime =
            VexaraCore::OpenClawSettings::probeOllamaRuntime(baseUrl, settings_.model);
        if (!runtime.detail.isEmpty()) {
            emit outputChunk(runtime.detail + QStringLiteral("\n"));
        }
        if (!runtime.reachable) {
            finishProcess(false, runtime.detail);
            return;
        }

        if (!runtime.modelLoaded) {
            emit outputChunk(
                QStringLiteral("Loading Ollama model into RAM (CPU — first request can take a "
                               "minute)…\n"));
            QString warmDetail;
            const int warmupTimeoutSec = runtime.vramBytes <= 0 ? 90 : 120;
            if (!VexaraCore::OpenClawSettings::warmOllamaModel(baseUrl, settings_.model, &warmDetail,
                                                               warmupTimeoutSec)) {
                finishProcess(
                    false,
                    QStringLiteral(
                        "%1\n\nBuilder aborted: Ollama did not answer in time. Close stuck "
                        "aider/python processes in Task Manager, restart Ollama, then retry.")
                        .arg(warmDetail));
                return;
            }
            emit outputChunk(warmDetail + QStringLiteral("\n"));
        } else {
            emit outputChunk(
                QStringLiteral("Ollama model already loaded — skipping warmup.\n"));
        }
    }

    process_->setProgram(program);
    process_->setArguments(arguments);
    process_->setWorkingDirectory(resolvedWorkingDirectory);
    process_->setProcessEnvironment(settings_.processEnvironment());
    process_->start();

    if (!process_->waitForStarted(5000)) {
        const QString processError = process_->errorString().trimmed();
        QString message = QStringLiteral("Failed to start Aider: %1").arg(program);
        if (!processError.isEmpty()) {
            message += QStringLiteral(" (%1)").arg(processError);
        }
        finishProcess(false, message);
        return;
    }

    QTimer::singleShot(settings_.timeoutMs, this, [this]() {
        if (isRunning()) {
            process_->kill();
            finishProcess(false, QStringLiteral("Aider timed out."));
        }
    });
}

void AiderBridge::cancelActiveTask()
{
    if (!isRunning()) {
        return;
    }
    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(3000);
    }
    finishProcess(false, QStringLiteral("Aider cancelled."));
}

void AiderBridge::finishProcess(bool success, const QString& summary)
{
    emit taskFinished(success, summary);
}

} // namespace VexaraOrchestration
