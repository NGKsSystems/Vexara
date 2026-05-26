#pragma once

#include "VexaraCore/GrokTaskContext.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace VexaraCore {

class GrokBuildSettings {
public:
    QString command;
    QStringList args;
    QString promptTemplate;
    int timeoutMs = 600000;

    bool isConfigured() const;
    void ensureDefaults();
    QString composePrompt(const QString& userTask, const GrokTaskContext& context) const;
    bool loadFromJson(const QJsonObject& root);
    void saveToJson(QJsonObject& root) const;

    static QString defaultInstallPath();
};

} // namespace VexaraCore
