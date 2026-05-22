#pragma once

#include <QString>

namespace VexaraCore {

enum class ModelProvider {
    OpenAi,
    Anthropic,
    Xai,
    Google,
    Local,
    Custom,
};

struct ModelProfile {
    QString id;
    QString displayName;
    ModelProvider provider = ModelProvider::Custom;
    QString modelName;
    QString apiKeyEnv;
    QString apiKey;
};

QString modelProviderLabel(ModelProvider provider);

} // namespace VexaraCore
