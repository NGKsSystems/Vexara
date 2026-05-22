#pragma once

#include "VexaraCore/GrokBuildSettings.h"
#include "VexaraCore/ModelSettings.h"
#include "VexaraCore/TerminalSettings.h"
#include "VexaraCore/VerificationSettings.h"

#include <QString>

namespace VexaraCore {

class GlobalSettings {
public:
    int version = 1;
    QString lastProjectRoot;
    TerminalSettings terminal;
    ModelSettings models;
    GrokBuildSettings grokBuild;
    VerificationSettings verification;

    bool load(QString* errorMessage = nullptr);
    bool save(QString* errorMessage = nullptr) const;
};

} // namespace VexaraCore
