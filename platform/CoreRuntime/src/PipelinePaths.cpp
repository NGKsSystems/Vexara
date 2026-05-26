#include "VexaraCore/PipelinePaths.h"

#include <QDir>

namespace VexaraCore::PipelinePaths {

QString pipelineRoot(const QString& projectRoot)
{
    if (projectRoot.trimmed().isEmpty()) {
        return QString();
    }
    return QDir(projectRoot).filePath(QStringLiteral(".vexara/pipeline"));
}

QString queueDatabasePath(const QString& projectRoot)
{
    const QString root = pipelineRoot(projectRoot);
    if (root.isEmpty()) {
        return QString();
    }
    return QDir(root).filePath(QStringLiteral("queue.db"));
}

QString artifactsRoot(const QString& projectRoot)
{
    const QString root = pipelineRoot(projectRoot);
    if (root.isEmpty()) {
        return QString();
    }
    return QDir(root).filePath(QStringLiteral("artifacts"));
}

QString taskArtifactDir(const QString& projectRoot, qint64 taskId)
{
    const QString root = artifactsRoot(projectRoot);
    if (root.isEmpty() || taskId <= 0) {
        return QString();
    }
    return QDir(root).filePath(QString::number(taskId));
}

} // namespace VexaraCore::PipelinePaths
