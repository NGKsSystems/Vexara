#pragma once

#include <QString>

namespace VexaraCore {

class SecureCredentialStore {
public:
    static bool isAvailable();
    static QString storageLabel();

    static bool exists(const QString& profileId);
    static bool save(const QString& profileId, const QString& secret);
    static QString load(const QString& profileId);
    static bool remove(const QString& profileId);
};

} // namespace VexaraCore
