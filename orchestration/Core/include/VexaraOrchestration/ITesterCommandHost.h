#pragma once

#include <QString>

namespace VexaraOrchestration {

class ITesterCommandHost {
public:
    virtual ~ITesterCommandHost() = default;

    virtual QString projectRoot() const = 0;
    virtual QString verificationCommand() const = 0;
    virtual void appendTerminalOutput(const QString& text) = 0;
};

} // namespace VexaraOrchestration
