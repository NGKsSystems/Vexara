#include "VexaraCore/ModelSettings.h"

#include "VexaraCore/AgentExecutionSettings.h"
#include "VexaraCore/AgentServiceKind.h"
#include "VexaraCore/OpenRouterSettings.h"
#include "VexaraCore/SecureCredentialStore.h"

#include <QJsonArray>
#include <QJsonObject>

namespace VexaraCore {

namespace {

ModelProvider providerFromString(const QString& value)
{
    const QString lower = value.toLower();
    if (lower == QStringLiteral("openai")) {
        return ModelProvider::OpenAi;
    }
    if (lower == QStringLiteral("anthropic")) {
        return ModelProvider::Anthropic;
    }
    if (lower == QStringLiteral("xai") || lower == QStringLiteral("grok")) {
        return ModelProvider::Xai;
    }
    if (lower == QStringLiteral("openrouter")) {
        return ModelProvider::OpenRouter;
    }
    if (lower == QStringLiteral("google")) {
        return ModelProvider::Google;
    }
    if (lower == QStringLiteral("local") || lower == QStringLiteral("ollama")) {
        return ModelProvider::Local;
    }
    return ModelProvider::Custom;
}

QString providerToString(ModelProvider provider)
{
    switch (provider) {
    case ModelProvider::OpenAi:
        return QStringLiteral("openai");
    case ModelProvider::Anthropic:
        return QStringLiteral("anthropic");
    case ModelProvider::Xai:
        return QStringLiteral("xai");
    case ModelProvider::OpenRouter:
        return QStringLiteral("openrouter");
    case ModelProvider::Google:
        return QStringLiteral("google");
    case ModelProvider::Local:
        return QStringLiteral("local");
    case ModelProvider::Custom:
        return QStringLiteral("custom");
    }
    return QStringLiteral("custom");
}

ModelProfile makeDefault(const QString& id,
                         const QString& displayName,
                         ModelProvider provider,
                         const QString& modelName,
                         const QString& apiKeyEnv)
{
    ModelProfile profile;
    profile.id = id;
    profile.displayName = displayName;
    profile.provider = provider;
    profile.modelName = modelName;
    profile.apiKeyEnv = apiKeyEnv;
    return profile;
}

} // namespace

QVector<ModelProfile> ModelSettings::profiles() const
{
    return profiles_;
}

ModelProfile ModelSettings::profileById(const QString& id) const
{
    for (const ModelProfile& profile : profiles_) {
        if (profile.id == id) {
            return profile;
        }
    }
    return ModelProfile{};
}

bool ModelSettings::hasProfile(const QString& id) const
{
    return !profileById(id).id.isEmpty();
}

void ModelSettings::setProfileApiKey(const QString& profileId, const QString& apiKey)
{
    for (ModelProfile& profile : profiles_) {
        if (profile.id != profileId) {
            continue;
        }
        profile.apiKey.clear();
        const QString trimmed = apiKey.trimmed();
        if (trimmed.isEmpty()) {
            return;
        }
        if (SecureCredentialStore::isAvailable()) {
            SecureCredentialStore::save(profileId, trimmed);
        }
        return;
    }
}

void ModelSettings::clearProfileApiKey(const QString& profileId)
{
    for (ModelProfile& profile : profiles_) {
        if (profile.id != profileId) {
            continue;
        }
        profile.apiKey.clear();
        if (SecureCredentialStore::isAvailable()) {
            SecureCredentialStore::remove(profileId);
        }
        return;
    }
}

QString ModelSettings::modelIdForAgent(const QString& agentId) const
{
    const QJsonValue assigned = agentModelAssignments.value(agentId);
    if (assigned.isString() && hasProfile(assigned.toString())) {
        return assigned.toString();
    }
    if (!defaultModelId.isEmpty() && hasProfile(defaultModelId)) {
        return defaultModelId;
    }
    if (!profiles_.isEmpty()) {
        return profiles_.first().id;
    }
    return QString();
}

void ModelSettings::ensureDefaults()
{
    const auto addIfMissing = [this](const ModelProfile& profile) {
        if (!hasProfile(profile.id)) {
            profiles_.append(profile);
        }
    };

    addIfMissing(makeDefault(QStringLiteral("grok-default"),
                             QStringLiteral("Grok"),
                             ModelProvider::Xai,
                             QStringLiteral("grok-3"),
                             QStringLiteral("XAI_API_KEY")));
    addIfMissing(makeDefault(QStringLiteral("openai-default"),
                             QStringLiteral("OpenAI"),
                             ModelProvider::OpenAi,
                             QStringLiteral("gpt-4o"),
                             QStringLiteral("OPENAI_API_KEY")));
    addIfMissing(makeDefault(QStringLiteral("openrouter-default"),
                             QStringLiteral("OpenRouter"),
                             ModelProvider::OpenRouter,
                             QStringLiteral("openai/gpt-4o"),
                             QStringLiteral("OPENROUTER_API_KEY")));
    addIfMissing(makeDefault(QStringLiteral("anthropic-default"),
                             QStringLiteral("Claude"),
                             ModelProvider::Anthropic,
                             QStringLiteral("claude-sonnet-4-20250514"),
                             QStringLiteral("ANTHROPIC_API_KEY")));

    if (defaultModelId.isEmpty() || !hasProfile(defaultModelId)) {
        defaultModelId = hasProfile(QStringLiteral("grok-default"))
                             ? QStringLiteral("grok-default")
                             : profiles_.first().id;
    }

    if (agentModelAssignments.isEmpty()) {
        agentModelAssignments.insert(QStringLiteral("builder-1"), defaultModelId);
        agentModelAssignments.insert(QStringLiteral("supervisor-1"), defaultModelId);
        agentModelAssignments.insert(QStringLiteral("orchestrator-1"), defaultModelId);
    }
}

QString ModelSettings::ensureOpenRouterProfile(const QString& modelSlug,
                                               const QString& displayName)
{
    const QString slug = modelSlug.trimmed();
    if (slug.isEmpty()) {
        return QString();
    }

    const QString profileId = OpenRouterSettings::profileIdForModelSlug(slug);
    for (ModelProfile& profile : profiles_) {
        if (profile.id == profileId) {
            profile.modelName = slug;
            if (!displayName.trimmed().isEmpty()) {
                profile.displayName = displayName.trimmed();
            }
            profile.provider = ModelProvider::OpenRouter;
            profile.apiKeyEnv = QStringLiteral("OPENROUTER_API_KEY");
            return profileId;
        }
    }

    ModelProfile profile;
    profile.id = profileId;
    profile.displayName = displayName.trimmed().isEmpty() ? slug : displayName.trimmed();
    profile.provider = ModelProvider::OpenRouter;
    profile.modelName = slug;
    profile.apiKeyEnv = QStringLiteral("OPENROUTER_API_KEY");
    profiles_.append(profile);
    return profileId;
}

void ModelSettings::syncAssignmentsFromAgentExecution(const AgentExecutionSettings& execution)
{
    const auto assignForBackend = [this](const QString& agentId, AgentServiceKind kind) {
        QString profileId;
        switch (kind) {
        case AgentServiceKind::OpenAiHttp:
            profileId = QStringLiteral("openai-default");
            break;
        case AgentServiceKind::OpenRouterHttp:
            profileId = QStringLiteral("openrouter-default");
            break;
        case AgentServiceKind::GrokCli:
            profileId = QStringLiteral("grok-default");
            break;
        case AgentServiceKind::OpenClawCli:
        case AgentServiceKind::AiderCli:
        case AgentServiceKind::None:
            return;
        }
        if (hasProfile(profileId)) {
            agentModelAssignments.insert(agentId, profileId);
        }
    };

    assignForBackend(QStringLiteral("orchestrator-1"),
                     execution.serviceForRoleKey(AgentExecutionSettings::roleKeyOrchestrator()));
    assignForBackend(QStringLiteral("builder-1"),
                     execution.serviceForRoleKey(AgentExecutionSettings::roleKeyBuilder()));
    assignForBackend(QStringLiteral("supervisor-1"),
                     execution.serviceForRoleKey(AgentExecutionSettings::roleKeySupervisor()));
}

bool ModelSettings::consumedLegacyApiKeys() const
{
    return legacyApiKeysMigrated_;
}

bool ModelSettings::loadFromJson(const QJsonObject& root)
{
    legacyApiKeysMigrated_ = false;
    const QJsonObject models = root.value(QStringLiteral("models")).toObject();
    defaultModelId = models.value(QStringLiteral("default_model")).toString();
    agentModelAssignments = models.value(QStringLiteral("agent_assignments")).toObject();

    profiles_.clear();
    const QJsonObject profileMap = models.value(QStringLiteral("profiles")).toObject();
    for (auto it = profileMap.begin(); it != profileMap.end(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject entry = it.value().toObject();
        ModelProfile profile;
        profile.id = it.key();
        profile.displayName = entry.value(QStringLiteral("display_name")).toString(profile.id);
        profile.provider = providerFromString(entry.value(QStringLiteral("provider")).toString());
        profile.modelName = entry.value(QStringLiteral("model")).toString();
        profile.apiBaseUrl = entry.value(QStringLiteral("api_base_url")).toString();
        profile.apiKeyEnv = entry.value(QStringLiteral("api_key_env")).toString();
        const QString legacyKey = entry.value(QStringLiteral("api_key")).toString();
        if (!profile.modelName.isEmpty()) {
            profiles_.append(profile);
            if (!legacyKey.isEmpty() && SecureCredentialStore::isAvailable()) {
                if (SecureCredentialStore::save(profile.id, legacyKey)) {
                    legacyApiKeysMigrated_ = true;
                }
            }
        }
    }

    ensureDefaults();
    return true;
}

void ModelSettings::saveToJson(QJsonObject& root) const
{
    QJsonObject models;
    models.insert(QStringLiteral("default_model"), defaultModelId);
    models.insert(QStringLiteral("agent_assignments"), agentModelAssignments);

    QJsonObject profileMap;
    for (const ModelProfile& profile : profiles_) {
        QJsonObject entry;
        entry.insert(QStringLiteral("display_name"), profile.displayName);
        entry.insert(QStringLiteral("provider"), providerToString(profile.provider));
        entry.insert(QStringLiteral("model"), profile.modelName);
        if (!profile.apiBaseUrl.isEmpty()) {
            entry.insert(QStringLiteral("api_base_url"), profile.apiBaseUrl);
        }
        if (!profile.apiKeyEnv.isEmpty()) {
            entry.insert(QStringLiteral("api_key_env"), profile.apiKeyEnv);
        }
        profileMap.insert(profile.id, entry);
    }
    models.insert(QStringLiteral("profiles"), profileMap);
    root.insert(QStringLiteral("models"), models);
}

} // namespace VexaraCore
