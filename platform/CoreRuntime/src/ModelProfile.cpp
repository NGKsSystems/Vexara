#include "VexaraCore/ModelProfile.h"

#include "VexaraCore/SecureCredentialStore.h"

#include <QProcessEnvironment>

namespace VexaraCore {

QString modelProviderLabel(ModelProvider provider)
{
    switch (provider) {
    case ModelProvider::OpenAi:
        return QStringLiteral("OpenAI");
    case ModelProvider::Anthropic:
        return QStringLiteral("Anthropic");
    case ModelProvider::Xai:
        return QStringLiteral("xAI / Grok");
    case ModelProvider::OpenRouter:
        return QStringLiteral("OpenRouter");
    case ModelProvider::Google:
        return QStringLiteral("Google");
    case ModelProvider::Local:
        return QStringLiteral("Local");
    case ModelProvider::Custom:
        return QStringLiteral("Custom");
    }
    return QStringLiteral("Custom");
}

QString ModelProfile::resolvedApiKey() const
{
    if (SecureCredentialStore::isAvailable()) {
        const QString stored = SecureCredentialStore::load(id);
        if (!stored.isEmpty()) {
            return stored;
        }
    }
    if (!apiKeyEnv.isEmpty()) {
        return QProcessEnvironment::systemEnvironment().value(apiKeyEnv);
    }
    return QString();
}

bool ModelProfile::isUsableForChat() const
{
    return !modelName.isEmpty() && !resolvedApiKey().isEmpty()
           && (provider == ModelProvider::OpenAi || provider == ModelProvider::Xai
               || provider == ModelProvider::OpenRouter);
}

} // namespace VexaraCore
