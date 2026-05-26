#include "VexaraOrchestration/WorkerGitDiff.h"

#include <QDir>
#include <QProcess>

namespace VexaraOrchestration {

bool WorkerGitDiff::isGitRepository(const QString& projectRoot)
{
    if (projectRoot.trimmed().isEmpty()) {
        return false;
    }
    return QDir(projectRoot).exists(QStringLiteral(".git"));
}

QString WorkerGitDiff::captureDiff(const QString& projectRoot)
{
    if (!isGitRepository(projectRoot)) {
        return QString();
    }

    QProcess process;
    process.setWorkingDirectory(projectRoot);
    process.start(QStringLiteral("git"),
                  {QStringLiteral("diff"), QStringLiteral("HEAD"), QStringLiteral("--")});
    if (!process.waitForStarted(3000) || !process.waitForFinished(120000)) {
        return QString();
    }

    return QString::fromLocal8Bit(process.readAllStandardOutput());
}

QStringList WorkerGitDiff::changedFiles(const QString& projectRoot)
{
    if (!isGitRepository(projectRoot)) {
        return {};
    }

    QProcess process;
    process.setWorkingDirectory(projectRoot);
    process.start(QStringLiteral("git"),
                  {QStringLiteral("diff"), QStringLiteral("--name-only"), QStringLiteral("HEAD")});
    if (!process.waitForStarted(3000) || !process.waitForFinished(120000)) {
        return {};
    }

    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    QStringList files;
    for (const QString& line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        files.append(line.trimmed());
    }
    return files;
}

} // namespace VexaraOrchestration
