#include "VexaraCore/ProjectSettings.h"

#include "VexaraCore/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace VexaraCore {

namespace {

constexpr int kSupportedVersion = 1;

} // namespace

bool ProjectSettings::ensureProjectConfig(QString* errorMessage) const
{
    if (projectRoot.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Project root is empty");
        }
        return false;
    }

    QDir vexaraDir(QDir(projectRoot).filePath(QStringLiteral(".vexara")));
    if (!vexaraDir.exists() && !vexaraDir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create .vexara directory");
        }
        return false;
    }

    const QString configPath = Paths::projectConfigPath(projectRoot);
    if (QFileInfo::exists(configPath)) {
        return true;
    }

    ProjectSettings defaults;
    defaults.projectRoot = projectRoot;
    defaults.displayName = QFileInfo(projectRoot).fileName();
    return defaults.save(errorMessage);
}

bool ProjectSettings::load(QString* errorMessage)
{
    const QString path = Paths::projectConfigPath(projectRoot);
    QFile file(path);
    if (!file.exists()) {
        displayName = QFileInfo(projectRoot).fileName();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open project config: %1").arg(path);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid project config JSON: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = doc.object();
    version = root.value(QStringLiteral("version")).toInt(kSupportedVersion);
    displayName = root.value(QStringLiteral("display_name")).toString(displayName);
    return true;
}

bool ProjectSettings::save(QString* errorMessage) const
{
    if (projectRoot.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Project root is empty");
        }
        return false;
    }

    QDir vexaraDir(QDir(projectRoot).filePath(QStringLiteral(".vexara")));
    if (!vexaraDir.exists() && !vexaraDir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create .vexara directory");
        }
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kSupportedVersion);
    root.insert(QStringLiteral("display_name"), displayName.isEmpty() ? QFileInfo(projectRoot).fileName() : displayName);

    const QString path = Paths::projectConfigPath(projectRoot);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write project config: %1").arg(path);
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

} // namespace VexaraCore
