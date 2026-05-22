#include "VexaraCore/Paths.h"

#include <QDir>
#include <QStandardPaths>

namespace VexaraCore::Paths {

QString appDataDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(base);
    dir.mkpath(QStringLiteral("."));
    return dir.absolutePath();
}

QString globalConfigPath()
{
    return QDir(appDataDir()).filePath(QStringLiteral("vexara.json"));
}

QString projectConfigPath(const QString& projectRoot)
{
    if (projectRoot.trimmed().isEmpty()) {
        return QString();
    }
    return QDir(projectRoot).filePath(QStringLiteral(".vexara/project.json"));
}

} // namespace VexaraCore::Paths
