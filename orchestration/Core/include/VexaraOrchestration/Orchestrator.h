#pragma once

#include "VexaraCore/GlobalSettings.h"
#include "VexaraOrchestration/AgentRegistry.h"
#include "VexaraOrchestration/GrokBuildBridge.h"
#include "VexaraOrchestration/VerificationRunner.h"

#include <QObject>
#include <QString>

namespace VexaraOrchestration {

class Orchestrator : public QObject {
    Q_OBJECT

public:
    explicit Orchestrator(QObject* parent = nullptr);

    const AgentRegistry& registry() const;
    AgentRegistry& registry();

    void configure(const VexaraCore::GlobalSettings& settings);
    void setProjectRoot(const QString& path);
    QString projectRoot() const;

    void bootstrapDefaultAgents();
    void submitTask(const QString& prompt);
    void approvePendingChanges();
    void rejectPendingChanges();
    void runVerification();

    bool hasPendingReview() const;

signals:
    void taskStateChanged(const QString& summary);
    void verificationStateChanged(const QString& summary);

private:
    void patchAgent(const QString& id,
                    AgentState state,
                    const QString& statusLine,
                    const QString& planSummary = QString());
    void applyModelAssignments(const VexaraCore::ModelSettings& models);
    void onGrokTaskFinished(bool success, const QString& summary);
    void onVerificationFinished(bool success, const QString& summary);

    AgentRegistry registry_;
    GrokBuildBridge grokBridge_;
    VerificationRunner verificationRunner_;
    VexaraCore::GlobalSettings settings_;
    QString projectRoot_;
    bool pendingReview_ = false;
};

} // namespace VexaraOrchestration
