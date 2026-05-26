#pragma once

#include <QString>

namespace VexaraCore {

enum class ModelProvider {
    OpenAi,
    Anthropic,
    Xai,
    OpenRouter,
    Google,
    Local,
    Custom,
};

struct ModelProfile {
    QString id;
    QString displayName;
    ModelProvider provider = ModelProvider::Custom;
    QString modelName;
    /** Optional override (e.g. OpenRouter-compatible gateways). */
    QString apiBaseUrl;
    QString apiKeyEnv;
    /** Transient only (e.g. legacy JSON migration); never written to vexara.json. */
    QString apiKey;

    QString resolvedApiKey() const;
    bool isUsableForChat() const;
};

QString modelProviderLabel(ModelProvider provider);

} // namespace VexaraCore
