#include "VexaraCore/ModelSettings.h"

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

QString modelProviderLabel(ModelProvider provider)
{
    switch (provider) {
    case ModelProvider::OpenAi:
        return QStringLiteral("OpenAI");
    case ModelProvider::Anthropic:
        return QStringLiteral("Anthropic");
    case ModelProvider::Xai:
        return QStringLiteral("xAI / Grok");
    case ModelProvider::Google:
        return QStringLiteral("Google");
    case ModelProvider::Local:
        return QStringLiteral("Local");
    case ModelProvider::Custom:
        return QStringLiteral("Custom");
    }
    return QStringLiteral("Custom");
}

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
    if (!profiles_.isEmpty()) {
        if (defaultModelId.isEmpty() || !hasProfile(defaultModelId)) {
            defaultModelId = profiles_.first().id;
        }
        return;
    }

    profiles_.append(makeDefault(
        QStringLiteral("grok-default"),
        QStringLiteral("Grok"),
        ModelProvider::Xai,
        QStringLiteral("grok-3"),
        QStringLiteral("XAI_API_KEY")));
    profiles_.append(makeDefault(
        QStringLiteral("openai-default"),
        QStringLiteral("OpenAI"),
        ModelProvider::OpenAi,
        QStringLiteral("gpt-4o"),
        QStringLiteral("OPENAI_API_KEY")));
    profiles_.append(makeDefault(
        QStringLiteral("anthropic-default"),
        QStringLiteral("Claude"),
        ModelProvider::Anthropic,
        QStringLiteral("claude-sonnet-4-20250514"),
        QStringLiteral("ANTHROPIC_API_KEY")));

    defaultModelId = QStringLiteral("grok-default");

    if (agentModelAssignments.isEmpty()) {
        agentModelAssignments.insert(QStringLiteral("builder-1"), defaultModelId);
        agentModelAssignments.insert(QStringLiteral("supervisor-1"), defaultModelId);
        agentModelAssignments.insert(QStringLiteral("orchestrator-1"), defaultModelId);
    }
}

bool ModelSettings::loadFromJson(const QJsonObject& root)
{
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
        profile.apiKeyEnv = entry.value(QStringLiteral("api_key_env")).toString();
        profile.apiKey = entry.value(QStringLiteral("api_key")).toString();
        if (!profile.modelName.isEmpty()) {
            profiles_.append(profile);
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
        if (!profile.apiKeyEnv.isEmpty()) {
            entry.insert(QStringLiteral("api_key_env"), profile.apiKeyEnv);
        }
        if (!profile.apiKey.isEmpty()) {
            entry.insert(QStringLiteral("api_key"), profile.apiKey);
        }
        profileMap.insert(profile.id, entry);
    }
    models.insert(QStringLiteral("profiles"), profileMap);
    root.insert(QStringLiteral("models"), models);
}

} // namespace VexaraCore
