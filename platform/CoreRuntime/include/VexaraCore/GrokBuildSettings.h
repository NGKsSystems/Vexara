#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace VexaraCore {

class GrokBuildSettings {
public:
    QString command;
    QStringList args;
    int timeoutMs = 600000;

    bool isConfigured() const;
    bool loadFromJson(const QJsonObject& root);
    void saveToJson(QJsonObject& root) const;
};

} // namespace VexaraCore
