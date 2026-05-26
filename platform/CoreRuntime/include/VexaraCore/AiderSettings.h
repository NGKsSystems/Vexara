#pragma once

#include <QJsonObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace VexaraCore {

class AiderSettings {
public:
    QString command;
    QString model = QStringLiteral("ollama/qwen2.5-coder:32b");
    QString ollamaApiKey = QStringLiteral("ollama-local");
    QString ollamaBaseUrl = QStringLiteral("http://127.0.0.1:11434");
    QStringList args;
    int timeoutMs = 600000;
    /** Per LLM API call timeout passed to Aider --timeout (seconds). */
    int llmTimeoutSec = 180;

    QString resolvedCommand() const;
    QString resolvedOllamaBaseUrl() const;
    bool usesOllamaModel() const;
    QString ollamaModelTag() const;
    QString modelForAiderCli() const;
    QProcessEnvironment processEnvironment() const;
    bool isConfigured() const;
    void ensureDefaults();
    bool prepareOllamaCpuConfigFiles(const QString& workingDirectory,
                                     QString* settingsPathOut,
                                     QString* metadataPathOut) const;
    QStringList resolveArguments(const QString& prompt,
                                 const QString& workingDirectory,
                                 const QStringList& targetFiles = QStringList()) const;
    static QStringList parseArgsField(const QString& text);
    bool loadFromJson(const QJsonObject& root);
    void saveToJson(QJsonObject& root) const;

private:
    void normalizeCommand();
    void normalizeArgs();
};

} // namespace VexaraCore
