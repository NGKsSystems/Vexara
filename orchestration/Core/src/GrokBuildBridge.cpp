#include "VexaraOrchestration/GrokBuildBridge.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

namespace VexaraOrchestration {

GrokBuildBridge::GrokBuildBridge(QObject* parent)
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
                    summary = success
                                  ? QStringLiteral("Grok Build finished successfully.")
                                  : QStringLiteral("Grok Build failed (exit %1).").arg(exitCode);
                }
                finishProcess(success, summary);
            });
}

void GrokBuildBridge::configure(const VexaraCore::GrokBuildSettings& settings)
{
    settings_ = settings;
}

bool GrokBuildBridge::isConfigured() const
{
    return settings_.isConfigured();
}

bool GrokBuildBridge::isRunning() const
{
    return process_->state() != QProcess::NotRunning;
}

void GrokBuildBridge::runTask(const QString& prompt, const QString& workingDirectory)
{
    if (!settings_.isConfigured()) {
        emit taskFinished(false, QStringLiteral("Grok Build is not configured. Set grok_build.command in vexara.json."));
        return;
    }
    if (isRunning()) {
        emit taskFinished(false, QStringLiteral("Grok Build is already running."));
        return;
    }

    capturedOutput_.clear();
    emit taskStarted(prompt);

    QStringList args = settings_.args;
    for (QString& arg : args) {
        arg.replace(QStringLiteral("{prompt}"), prompt);
        arg.replace(QStringLiteral("{cwd}"), workingDirectory);
    }

    process_->setProgram(settings_.command);
    process_->setArguments(args);
    if (!workingDirectory.isEmpty() && QFileInfo(workingDirectory).isDir()) {
        process_->setWorkingDirectory(workingDirectory);
    }

    process_->start();
    if (!process_->waitForStarted(5000)) {
        finishProcess(false, QStringLiteral("Failed to start Grok Build: %1").arg(settings_.command));
        return;
    }

    QTimer::singleShot(settings_.timeoutMs, this, [this]() {
        if (isRunning()) {
            process_->kill();
            finishProcess(false, QStringLiteral("Grok Build timed out."));
        }
    });
}

void GrokBuildBridge::finishProcess(bool success, const QString& summary)
{
    emit taskFinished(success, summary);
}

} // namespace VexaraOrchestration
