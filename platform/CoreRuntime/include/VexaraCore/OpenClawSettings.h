#pragma once

#include <QJsonObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace VexaraCore {

class OpenClawSettings {
public:
    struct LocalSetupResult {
        bool success = false;
        QString message;
    };

    struct ModelCatalogResult {
        QStringList modelIds;
        QStringList installedIds;
        QStringList suggestedIds;
        QStringList configuredIds;
        QString detail;
    };

    struct OllamaRuntimeStatus {
        bool reachable = false;
        bool modelLoaded = false;
        qint64 vramBytes = -1;
        QString detail;
    };

    QString command;
    QString agentId = QStringLiteral("main");
    QString model = QStringLiteral("ollama/qwen2.5-coder:14b");
    QString ollamaApiKey = QStringLiteral("ollama-local");
    QString ollamaBaseUrl = QStringLiteral("http://127.0.0.1:11434");
    bool localOllamaMode = true;
    QStringList args;
    QString promptTemplate;
    int timeoutMs = 600000;

    bool isConfigured() const;
    bool usesLocalOllama() const;
    bool isLocalOllamaReady(QString* detail = nullptr) const;
    void ensureDefaults();
    QString composePrompt(const QString& userTask, const QString& projectPath) const;
    QStringList resolveArguments(const QString& prompt, const QString& workingDirectory) const;
    QProcessEnvironment processEnvironment() const;
    QString friendlyErrorMessage(const QString& rawOutput) const;
    LocalSetupResult applyLocalOllamaConfiguration() const;
    ModelCatalogResult discoverAllowedModels() const;
    ModelCatalogResult discoverOpenClawPickerModels(bool queryOpenClawCli = true) const;
    static ModelCatalogResult discoverOllamaInstalledModels(const QString& baseUrl);
    static ModelCatalogResult discoverOllamaPickerModels(const QString& baseUrl);
    static ModelCatalogResult discoverOllamaModels(const QString& baseUrl);
    static QStringList defaultOllamaModelPresets();
    static QString ollamaModelTagFromId(const QString& modelId);
    static OllamaRuntimeStatus probeOllamaRuntime(const QString& baseUrl, const QString& modelId);
    static bool warmOllamaModel(const QString& baseUrl,
                                const QString& modelId,
                                QString* detail = nullptr,
                                int timeoutSec = 90);
    bool loadFromJson(const QJsonObject& root);
    void saveToJson(QJsonObject& root) const;
    static QStringList parseArgsField(const QString& text);
    static QString defaultInstallPath();

private:
    void normalizeArgs();
};

} // namespace VexaraCore
