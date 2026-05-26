#include "VexaraEditor/TesterCommandHost.h"

namespace VexaraEditor {

void TesterCommandHost::setTerminalPanel(TerminalPanel* panel)
{
    panel_ = panel;
}

void TesterCommandHost::setProjectRoot(const QString& projectRoot)
{
    projectRoot_ = projectRoot;
}

void TesterCommandHost::setVerificationCommand(const QString& command)
{
    verificationCommand_ = command;
}

QString TesterCommandHost::projectRoot() const
{
    return projectRoot_;
}

QString TesterCommandHost::verificationCommand() const
{
    return verificationCommand_;
}

void TesterCommandHost::appendTerminalOutput(const QString& text)
{
    if (panel_) {
        panel_->appendTranscriptOutput(text);
    }
}

} // namespace VexaraEditor
