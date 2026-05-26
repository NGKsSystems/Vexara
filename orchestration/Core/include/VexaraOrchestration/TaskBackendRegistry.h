#pragma once



#include "VexaraCore/AgentExecutionSettings.h"

#include "VexaraCore/GlobalSettings.h"

#include "VexaraOrchestration/GrokBuildCliBackend.h"

#include "VexaraOrchestration/HttpModelTaskBackend.h"

#include "VexaraOrchestration/ITaskBackend.h"

#include "VexaraOrchestration/OpenClawCliBackend.h"
#include "VexaraOrchestration/AiderCliBackend.h"



#include <QObject>



namespace VexaraOrchestration {



class TaskBackendRegistry : public QObject {

    Q_OBJECT



public:

    explicit TaskBackendRegistry(QObject* parent = nullptr);



    void configure(const VexaraCore::GlobalSettings& settings, const AgentPromptComposer& composer);



    VexaraCore::AgentServiceKind serviceForRole(AgentRole role) const;

    ITaskBackend* backendForRole(AgentRole role);

    const ITaskBackend* backendForRole(AgentRole role) const;

    ITaskBackend* backendForService(VexaraCore::AgentServiceKind service);

    bool isRoleConfigured(AgentRole role) const;

    bool anyBackendRunning() const;
    void cancelRunningTasks();



    GrokBuildCliBackend& grokCli();

    HttpModelTaskBackend& openAiHttp();

    HttpModelTaskBackend& openRouterHttp();

    OpenClawCliBackend& openClawCli();
    AiderCliBackend& aiderCli();
    const VexaraCore::AiderSettings& aiderSettings() const;
    void refreshAiderCliSettings();
    void refreshOpenClawCliSettings();
    const VexaraCore::OpenClawSettings& openClawSettings() const;



signals:

    void taskStarted(AgentRole role, const QString& prompt);

    void outputChunk(AgentRole role, const QString& text);

    void taskFinished(AgentRole role, bool success, const QString& summary);



private:

    void wireBackend(ITaskBackend& backend);



    VexaraCore::AgentExecutionSettings execution_;

    GrokBuildCliBackend grokCli_;

    HttpModelTaskBackend openAiHttp_;

    HttpModelTaskBackend openRouterHttp_;

    OpenClawCliBackend openClawCli_;
    AiderCliBackend aiderCli_;

};



} // namespace VexaraOrchestration


