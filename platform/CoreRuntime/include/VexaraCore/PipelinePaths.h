#pragma once

#include <QString>

namespace VexaraCore::PipelinePaths {

QString pipelineRoot(const QString& projectRoot);
QString queueDatabasePath(const QString& projectRoot);
QString artifactsRoot(const QString& projectRoot);
QString taskArtifactDir(const QString& projectRoot, qint64 taskId);

} // namespace VexaraCore::PipelinePaths
