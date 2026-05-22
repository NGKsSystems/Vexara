#pragma once

#include "VexaraCore/ModelProfile.h"

#include <QJsonObject>
#include <QVector>

namespace VexaraCore {

class ModelSettings {
public:
    QString defaultModelId;
    QJsonObject agentModelAssignments;

    QVector<ModelProfile> profiles() const;
    ModelProfile profileById(const QString& id) const;
    bool hasProfile(const QString& id) const;
    QString modelIdForAgent(const QString& agentId) const;

    void ensureDefaults();
    bool loadFromJson(const QJsonObject& root);
    void saveToJson(QJsonObject& root) const;

private:
    QVector<ModelProfile> profiles_;
};

} // namespace VexaraCore
