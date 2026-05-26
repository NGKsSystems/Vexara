#include "VexaraCore/VerificationSettings.h"

#include <QJsonObject>

namespace VexaraCore {

void VerificationSettings::normalizeCommand()
{
    command = command.trimmed();
    const QString lower = command.toLower();

    if (lower == QStringLiteral("build_release.bat")
        || lower == QStringLiteral(".\\build_release.bat")
        || lower == QStringLiteral("./build_release.bat")
        || lower == QStringLiteral("build\\build_release.bat")
        || lower == QStringLiteral("build/build_release.bat")) {
        command = QStringLiteral("tools/build_release.bat");
    }
}

void VerificationSettings::ensureDefaults()
{
    normalizeCommand();
    if (command.isEmpty()) {
        command = QStringLiteral("tools/build_release.bat");
    }
}

bool VerificationSettings::loadFromJson(const QJsonObject& root)
{
    const QJsonObject verification = root.value(QStringLiteral("verification")).toObject();
    const QString loadedCommand = verification.value(QStringLiteral("command")).toString();
    if (!loadedCommand.isEmpty()) {
        command = loadedCommand;
    }
    timeoutMs = verification.value(QStringLiteral("timeout_ms")).toInt(600000);
    ensureDefaults();
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
