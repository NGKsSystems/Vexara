#include "VexaraCore/GrokBuildSettings.h"

#include <QJsonArray>
#include <QJsonObject>

namespace VexaraCore {

bool GrokBuildSettings::isConfigured() const
{
    return !command.trimmed().isEmpty();
}

bool GrokBuildSettings::loadFromJson(const QJsonObject& root)
{
    const QJsonObject grok = root.value(QStringLiteral("grok_build")).toObject();
    command = grok.value(QStringLiteral("command")).toString();
    timeoutMs = grok.value(QStringLiteral("timeout_ms")).toInt(600000);

    args.clear();
    const QJsonArray argArray = grok.value(QStringLiteral("args")).toArray();
    for (const QJsonValue& value : argArray) {
        args.append(value.toString());
    }
    return true;
}

void GrokBuildSettings::saveToJson(QJsonObject& root) const
{
    QJsonObject grok;
    grok.insert(QStringLiteral("command"), command);
    grok.insert(QStringLiteral("timeout_ms"), timeoutMs);
    QJsonArray argArray;
    for (const QString& arg : args) {
        argArray.append(arg);
    }
    grok.insert(QStringLiteral("args"), argArray);
    root.insert(QStringLiteral("grok_build"), grok);
}

} // namespace VexaraCore
