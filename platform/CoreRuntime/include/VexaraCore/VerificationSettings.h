#pragma once

#include <QJsonObject>
#include <QString>

namespace VexaraCore {

class VerificationSettings {
public:
    QString command = QStringLiteral("tools/build_release.bat");
    int timeoutMs = 600000;

    void ensureDefaults();
    bool loadFromJson(const QJsonObject& root);
    void saveToJson(QJsonObject& root) const;

private:
    void normalizeCommand();
};

} // namespace VexaraCore
