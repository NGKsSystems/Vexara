#include "VexaraCore/VerificationSettings.h"

#include <QJsonObject>

namespace VexaraCore {

bool VerificationSettings::loadFromJson(const QJsonObject& root)
{
    const QJsonObject verification = root.value(QStringLiteral("verification")).toObject();
    const QString loadedCommand = verification.value(QStringLiteral("command")).toString();
    if (!loadedCommand.isEmpty()) {
        command = loadedCommand;
    }
    timeoutMs = verification.value(QStringLiteral("timeout_ms")).toInt(600000);
    return true;
}

void VerificationSettings::saveToJson(QJsonObject& root) const
{
    QJsonObject verification;
    verification.insert(QStringLiteral("command"), command);
    verification.insert(QStringLiteral("timeout_ms"), timeoutMs);
    root.insert(QStringLiteral("verification"), verification);
}

} // namespace VexaraCore
