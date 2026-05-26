#include "VexaraOrchestration/PipelineArtifacts.h"

#include "VexaraCore/PipelinePaths.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>

namespace VexaraOrchestration {

namespace {

constexpr auto kSchemaVersionKey = "schema_version";
constexpr auto kTaskIdKey = "task_id";
constexpr auto kSourceAgentKey = "source_agent";
constexpr auto kTimestampKey = "timestamp";
constexpr auto kStatusKey = "status";
constexpr auto kPayloadKey = "payload";
constexpr auto kReasoningKey = "reasoning";

} // namespace

QString PipelineArtifacts::fileTaskJson()
{
    return QStringLiteral("task.json");
}

QString PipelineArtifacts::filePlanJson()
{
    return QStringLiteral("plan.json");
}

QString PipelineArtifacts::fileResearchSummaryMd()
{
    return QStringLiteral("research_summary.md");
}

QString PipelineArtifacts::fileApprovedPlanJson()
{
    return QStringLiteral("approved_plan.json");
}

QString PipelineArtifacts::fileWorkerResultMd()
{
    return QStringLiteral("worker_result.md");
}

QString PipelineArtifacts::fileChangesDiff()
{
    return QStringLiteral("changes.diff");
}

QString PipelineArtifacts::fileTestResultsJson()
{
    return QStringLiteral("test_results.json");
}

QString PipelineArtifacts::fileReviewDecisionJson()
{
    return QStringLiteral("review_decision.json");
}

QJsonObject PipelineArtifacts::makeEnvelope(qint64 taskId,
                                            const QString& sourceAgent,
                                            const QString& status,
                                            const QJsonObject& payload,
                                            const QString& reasoning)
{
    QJsonObject envelope;
    envelope.insert(kSchemaVersionKey, kSchemaVersion);
    envelope.insert(kTaskIdKey, taskId);
    envelope.insert(kSourceAgentKey, sourceAgent);
    envelope.insert(kTimestampKey, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    envelope.insert(kStatusKey, status);
    envelope.insert(kPayloadKey, payload);
    if (!reasoning.trimmed().isEmpty()) {
        envelope.insert(kReasoningKey, reasoning);
    }
    return envelope;
}

qint64 PipelineArtifacts::jsonToTaskId(const QJsonValue& value)
{
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble());
    }
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().trimmed().toLongLong(&ok);
        if (ok) {
            return parsed;
        }
    }
    return 0;
}

qint64 PipelineArtifacts::payloadTaskRef(const QJsonObject& payload, const QString& key)
{
    return jsonToTaskId(payload.value(key));
}

bool PipelineArtifacts::parseEnvelope(const QJsonObject& json, PipelineArtifactEnvelope& out)
{
    if (!json.contains(kTaskIdKey) || !json.contains(kSourceAgentKey)) {
        return false;
    }

    out.schemaVersion = json.value(kSchemaVersionKey).toInt(kSchemaVersion);
    out.taskId = static_cast<qint64>(json.value(kTaskIdKey).toDouble());
    out.sourceAgent = json.value(kSourceAgentKey).toString();
    out.timestamp = QDateTime::fromString(json.value(kTimestampKey).toString(), Qt::ISODateWithMs);
    out.status = json.value(kStatusKey).toString();
    out.payload = json.value(kPayloadKey).toObject();
    out.reasoning = json.value(kReasoningKey).toString();
    return out.taskId > 0 && !out.sourceAgent.isEmpty();
}

bool PipelineArtifacts::ensureTaskDir(const QString& projectRoot, qint64 taskId)
{
    const QString dirPath = VexaraCore::PipelinePaths::taskArtifactDir(projectRoot, taskId);
    if (dirPath.isEmpty()) {
        return false;
    }
    return QDir().mkpath(dirPath);
}

bool PipelineArtifacts::writeJson(const QString& projectRoot,
                                  qint64 taskId,
                                  const QString& fileName,
                                  const QJsonObject& document)
{
    if (!ensureTaskDir(projectRoot, taskId)) {
        return false;
    }

    const QString dirPath = VexaraCore::PipelinePaths::taskArtifactDir(projectRoot, taskId);
    QFile file(QDir(dirPath).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
    return true;
}

bool PipelineArtifacts::readJson(const QString& projectRoot,
                                 qint64 taskId,
                                 const QString& fileName,
                                 QJsonObject& documentOut)
{
    const QString dirPath = VexaraCore::PipelinePaths::taskArtifactDir(projectRoot, taskId);
    if (dirPath.isEmpty()) {
        return false;
    }

    QFile file(QDir(dirPath).filePath(fileName));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return false;
    }

    documentOut = doc.object();
    return true;
}

bool PipelineArtifacts::writeText(const QString& projectRoot,
                                  qint64 taskId,
                                  const QString& fileName,
                                  const QString& content)
{
    if (!ensureTaskDir(projectRoot, taskId)) {
        return false;
    }

    const QString dirPath = VexaraCore::PipelinePaths::taskArtifactDir(projectRoot, taskId);
    QFile file(QDir(dirPath).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(content.toUtf8());
    return true;
}

bool PipelineArtifacts::readText(const QString& projectRoot,
                                 qint64 taskId,
                                 const QString& fileName,
                                 QString& contentOut)
{
    const QString dirPath = VexaraCore::PipelinePaths::taskArtifactDir(projectRoot, taskId);
    if (dirPath.isEmpty()) {
        return false;
    }

    QFile file(QDir(dirPath).filePath(fileName));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    contentOut = QString::fromUtf8(file.readAll());
    return true;
}

} // namespace VexaraOrchestration
