#pragma once

#include "VexaraCore/AgentServiceKind.h"
#include "VexaraCore/ModelProfile.h"

#include <QJsonObject>
#include <QVector>

namespace VexaraCore {

class AgentExecutionSettings;

class ModelSettings {
public:
    QString defaultModelId;
    QJsonObject agentModelAssignments;

    QVector<ModelProfile> profiles() const;
    ModelProfile profileById(const QString& id) const;
    bool hasProfile(const QString& id) const;
    QString modelIdForAgent(const QString& agentId) const;
    void setProfileApiKey(const QString& profileId, const QString& apiKey);
    void clearProfileApiKey(const QString& profileId);

    void ensureDefaults();
    QString ensureOpenRouterProfile(const QString& modelSlug, const QString& displayName);
    void syncAssignmentsFromAgentExecution(const AgentExecutionSettings& execution);
    bool loadFromJson(const QJsonObject& root);
    void saveToJson(QJsonObject& root) const;
    bool consumedLegacyApiKeys() const;

private:
    bool legacyApiKeysMigrated_ = false;
    QVector<ModelProfile> profiles_;
};

} // namespace VexaraCore
