#include "VexaraCore/GlobalSettings.h"

#include "VexaraCore/Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace VexaraCore {

namespace {

constexpr int kSupportedVersion = 1;

} // namespace

bool GlobalSettings::load(QString* errorMessage)
{
    const QString path = Paths::globalConfigPath();
    QFile file(path);
    if (!file.exists()) {
        terminal.ensureDefaults();
        models.ensureDefaults();
        grokBuild.loadFromJson(QJsonObject{});
        grokBuild.ensureDefaults();
        runTask.loadFromJson(QJsonObject{});
        runTask.ensureDefaults();
        agentExecution.loadFromJson(QJsonObject{});
        agentExecution.ensureDefaults();
        verification.loadFromJson(QJsonObject{});
        verification.ensureDefaults();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open global config: %1").arg(path);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid global config JSON: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = doc.object();
    version = root.value(QStringLiteral("version")).toInt(kSupportedVersion);
    lastProjectRoot = root.value(QStringLiteral("last_project_root")).toString();
    terminal.loadFromJson(root);
    models.loadFromJson(root);
    if (models.consumedLegacyApiKeys()) {
        save(errorMessage);
    }
    grokBuild.loadFromJson(root);
    grokBuild.ensureDefaults();
    runTask.loadFromJson(root);
    runTask.ensureDefaults();
    agentExecution.loadFromJson(root);
    agentExecution.ensureDefaults();
    verification.loadFromJson(root);
    verification.ensureDefaults();
    return true;
}

bool GlobalSettings::save(QString* errorMessage) const
{
    QDir().mkpath(Paths::appDataDir());

    QJsonObject root;
    root.insert(QStringLiteral("version"), kSupportedVersion);
    if (!lastProjectRoot.isEmpty()) {
        root.insert(QStringLiteral("last_project_root"), lastProjectRoot);
    }
    terminal.saveToJson(root);
    models.saveToJson(root);
    grokBuild.saveToJson(root);
    agentExecution.saveToJson(root);
    verification.saveToJson(root);

    const QString path = Paths::globalConfigPath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write global config: %1").arg(path);
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

} // namespace VexaraCore
