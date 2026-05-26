#pragma once

#include <QJsonObject>
#include <QString>

namespace VexaraCore {

enum class RunTaskBackendKind {
    GrokCli,
    OpenAiHttp,
};

class RunTaskSettings {
public:
    QString backend = QStringLiteral("grok_cli");

    void ensureDefaults();
    RunTaskBackendKind backendKind() const;
    bool loadFromJson(const QJsonObject& root);
    void saveToJson(QJsonObject& root) const;

    static RunTaskBackendKind backendKindFromString(const QString& value);
    static QString backendKindToString(RunTaskBackendKind kind);
};

} // namespace VexaraCore
