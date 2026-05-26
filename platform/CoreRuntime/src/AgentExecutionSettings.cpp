#include "VexaraCore/AgentExecutionSettings.h"



#include "VexaraCore/RunTaskSettings.h"



#include <QJsonObject>



namespace VexaraCore {



QString AgentExecutionSettings::roleKeyOrchestrator()

{

    return QStringLiteral("orchestrator");

}



QString AgentExecutionSettings::roleKeyBuilder()

{

    return QStringLiteral("builder");

}



QString AgentExecutionSettings::roleKeySupervisor()

{

    return QStringLiteral("supervisor");

}



void AgentExecutionSettings::ensureDefaults()

{

    if (orchestratorBackend.trimmed().isEmpty()) {

        orchestratorBackend = QStringLiteral("none");

    }

    if (builderBackend.trimmed().isEmpty()) {

        builderBackend = QStringLiteral("grok_cli");

    }

    if (supervisorBackend.trimmed().isEmpty()) {

        supervisorBackend = QStringLiteral("none");

    }

    openclaw.ensureDefaults();
    aider.ensureDefaults();

    if (aider.usesOllamaModel()) {
        if (aider.ollamaBaseUrl.trimmed().isEmpty()) {
            aider.ollamaBaseUrl = openclaw.ollamaBaseUrl;
        }
        if (aider.ollamaApiKey.trimmed().isEmpty()) {
            aider.ollamaApiKey = openclaw.ollamaApiKey;
        }
    }

}



AgentServiceKind AgentExecutionSettings::serviceForRoleKey(const QString& roleKey) const

{

    if (roleKey == roleKeyOrchestrator()) {

        return agentServiceKindFromString(orchestratorBackend);

    }

    if (roleKey == roleKeyBuilder()) {

        return agentServiceKindFromString(builderBackend);

    }

    if (roleKey == roleKeySupervisor()) {

        return agentServiceKindFromString(supervisorBackend);

    }

    return AgentServiceKind::None;

}



void AgentExecutionSettings::setServiceForRoleKey(const QString& roleKey, AgentServiceKind kind)

{

    const QString serialized = agentServiceKindToString(kind);

    if (roleKey == roleKeyOrchestrator()) {

        orchestratorBackend = serialized;

        return;

    }

    if (roleKey == roleKeyBuilder()) {

        builderBackend = serialized;

        return;

    }

    if (roleKey == roleKeySupervisor()) {

        supervisorBackend = serialized;

    }

}



void AgentExecutionSettings::migrateLegacyRunTask(const RunTaskSettings& runTask)

{

    if (builderBackend == QStringLiteral("grok_cli")

        && runTask.backendKind() != RunTaskBackendKind::GrokCli) {

        builderBackend = agentServiceKindToString(
            runTask.backendKind() == RunTaskBackendKind::OpenAiHttp
                ? AgentServiceKind::OpenAiHttp
                : AgentServiceKind::GrokCli);

    }

}



bool AgentExecutionSettings::loadFromJson(const QJsonObject& root)

{

    const QJsonObject agentExecution = root.value(QStringLiteral("agent_execution")).toObject();

    if (!agentExecution.isEmpty()) {

        const QJsonObject roleBackends =

            agentExecution.value(QStringLiteral("role_backends")).toObject();

        orchestratorBackend =

            roleBackends.value(roleKeyOrchestrator()).toString(orchestratorBackend);

        builderBackend = roleBackends.value(roleKeyBuilder()).toString(builderBackend);

        supervisorBackend = roleBackends.value(roleKeySupervisor()).toString(supervisorBackend);

        openclaw.loadFromJson(agentExecution);
        aider.loadFromJson(agentExecution);

    } else {

        RunTaskSettings legacy;

        legacy.loadFromJson(root);

        migrateLegacyRunTask(legacy);

    }



    ensureDefaults();

    return true;

}



void AgentExecutionSettings::saveToJson(QJsonObject& root) const

{

    QJsonObject roleBackends;

    roleBackends.insert(roleKeyOrchestrator(), orchestratorBackend);

    roleBackends.insert(roleKeyBuilder(), builderBackend);

    roleBackends.insert(roleKeySupervisor(), supervisorBackend);



    QJsonObject agentExecution;

    agentExecution.insert(QStringLiteral("role_backends"), roleBackends);

    openclaw.saveToJson(agentExecution);
    aider.saveToJson(agentExecution);

    root.insert(QStringLiteral("agent_execution"), agentExecution);



    QJsonObject runTask;

    runTask.insert(QStringLiteral("backend"), builderBackend);

    root.insert(QStringLiteral("run_task"), runTask);

}



} // namespace VexaraCore


