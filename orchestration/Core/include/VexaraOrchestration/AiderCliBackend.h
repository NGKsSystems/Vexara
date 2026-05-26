#pragma once

#include "VexaraCore/AiderSettings.h"
#include "VexaraOrchestration/AiderBridge.h"
#include "VexaraOrchestration/ITaskBackend.h"

namespace VexaraOrchestration {

class AiderCliBackend : public ITaskBackend {
    Q_OBJECT

public:
    explicit AiderCliBackend(QObject* parent = nullptr);

    VexaraCore::AgentServiceKind serviceKind() const override;
    TaskBackendCapabilities capabilities() const override;
    void configure(const VexaraCore::AiderSettings& settings);
    bool isConfigured() const override;
    bool isRunning() const override;
    void executeTask(AgentRole role,
                     const QString& composedPrompt,
                     const TaskContext& context) override;
    void cancelActiveTask();

private:
    AiderBridge bridge_;
    VexaraCore::AiderSettings settings_;
    AgentRole activeRole_ = AgentRole::Builder;
};

} // namespace VexaraOrchestration
