#include "VexaraCore/RunTaskSettings.h"

#include <QJsonObject>

namespace VexaraCore {

RunTaskBackendKind RunTaskSettings::backendKindFromString(const QString& value)
{
    if (value == QStringLiteral("openai_http")) {
        return RunTaskBackendKind::OpenAiHttp;
    }
    return RunTaskBackendKind::GrokCli;
}

QString RunTaskSettings::backendKindToString(RunTaskBackendKind kind)
{
    switch (kind) {
    case RunTaskBackendKind::OpenAiHttp:
        return QStringLiteral("openai_http");
    case RunTaskBackendKind::GrokCli:
        return QStringLiteral("grok_cli");
    }
    return QStringLiteral("grok_cli");
}

void RunTaskSettings::ensureDefaults()
{
    if (backend.trimmed().isEmpty()) {
        backend = QStringLiteral("grok_cli");
    }
}

RunTaskBackendKind RunTaskSettings::backendKind() const
{
    return backendKindFromString(backend);
}

bool RunTaskSettings::loadFromJson(const QJsonObject& root)
{
    const QJsonObject runTask = root.value(QStringLiteral("run_task")).toObject();
    backend = runTask.value(QStringLiteral("backend")).toString();
    ensureDefaults();
    return true;
}

void RunTaskSettings::saveToJson(QJsonObject& root) const
{
    QJsonObject runTask;
    runTask.insert(QStringLiteral("backend"), backend);
    root.insert(QStringLiteral("run_task"), runTask);
}

} // namespace VexaraCore
