#include "VexaraOrchestration/VerificationRunner.h"

#include <QFileInfo>
#include <QProcess>
#include <QTimer>

namespace VexaraOrchestration {

VerificationRunner::VerificationRunner(QObject* parent)
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
                if (summary.length() > 2000) {
                    summary = summary.right(2000);
                }
                if (summary.isEmpty()) {
                    summary = success
                                  ? QStringLiteral("Verification passed.")
                                  : QStringLiteral("Verification failed (exit %1).").arg(exitCode);
                }
                finishProcess(success, summary);
            });
}

void VerificationRunner::configure(const VexaraCore::VerificationSettings& settings)
{
    settings_ = settings;
}

bool VerificationRunner::isRunning() const
{
    return process_->state() != QProcess::NotRunning;
}

void VerificationRunner::run(const QString& workingDirectory)
{
    if (isRunning()) {
        emit verificationFinished(false, QStringLiteral("Verification is already running."));
        return;
    }
    if (settings_.command.trimmed().isEmpty()) {
        emit verificationFinished(false, QStringLiteral("No verification command configured."));
        return;
    }
    if (workingDirectory.isEmpty() || !QFileInfo(workingDirectory).isDir()) {
        emit verificationFinished(false, QStringLiteral("Open a project folder before running verification."));
        return;
    }

    capturedOutput_.clear();
    emit verificationStarted();

    process_->setProgram(QStringLiteral("cmd.exe"));
    process_->setArguments(
        {QStringLiteral("/C"), settings_.command});
    process_->setWorkingDirectory(workingDirectory);
    process_->start();

    if (!process_->waitForStarted(5000)) {
        finishProcess(false, QStringLiteral("Failed to start verification command."));
        return;
    }

    QTimer::singleShot(settings_.timeoutMs, this, [this]() {
        if (isRunning()) {
            process_->kill();
            finishProcess(false, QStringLiteral("Verification timed out."));
        }
    });
}

void VerificationRunner::finishProcess(bool success, const QString& summary)
{
    emit verificationFinished(success, summary);
}

} // namespace VexaraOrchestration
