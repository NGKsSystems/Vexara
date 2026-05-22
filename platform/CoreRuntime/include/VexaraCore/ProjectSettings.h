#pragma once

#include <QString>

namespace VexaraCore {

class ProjectSettings {
public:
    QString projectRoot;
    int version = 1;
    QString displayName;

    bool ensureProjectConfig(QString* errorMessage = nullptr) const;
    bool load(QString* errorMessage = nullptr);
    bool save(QString* errorMessage = nullptr) const;
};

} // namespace VexaraCore
