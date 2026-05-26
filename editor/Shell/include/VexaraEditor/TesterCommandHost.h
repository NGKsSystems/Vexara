#pragma once

#include "VexaraEditor/TerminalPanel.h"
#include "VexaraOrchestration/ITesterCommandHost.h"

namespace VexaraEditor {

class TesterCommandHost : public VexaraOrchestration::ITesterCommandHost {
public:
    TesterCommandHost() = default;

    void setTerminalPanel(TerminalPanel* panel);
    void setProjectRoot(const QString& projectRoot);
    void setVerificationCommand(const QString& command);

    QString projectRoot() const override;
    QString verificationCommand() const override;
    void appendTerminalOutput(const QString& text) override;

private:
    TerminalPanel* panel_ = nullptr;
    QString projectRoot_;
    QString verificationCommand_;
};

} // namespace VexaraEditor
