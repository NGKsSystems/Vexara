#pragma once



#include "VexaraCore/AgentServiceKind.h"

#include "VexaraCore/OpenClawSettings.h"
#include "VexaraCore/AiderSettings.h"



#include <QJsonObject>

#include <QString>



namespace VexaraCore {



class RunTaskSettings;



class AgentExecutionSettings {

public:

    QString orchestratorBackend = QStringLiteral("none");

    QString builderBackend = QStringLiteral("grok_cli");

    QString supervisorBackend = QStringLiteral("none");

    OpenClawSettings openclaw;
    AiderSettings aider;



    void ensureDefaults();

    AgentServiceKind serviceForRoleKey(const QString& roleKey) const;

    void setServiceForRoleKey(const QString& roleKey, AgentServiceKind kind);



    bool loadFromJson(const QJsonObject& root);

    void saveToJson(QJsonObject& root) const;



    void migrateLegacyRunTask(const RunTaskSettings& runTask);



    static QString roleKeyOrchestrator();

    static QString roleKeyBuilder();

    static QString roleKeySupervisor();

};



} // namespace VexaraCore


