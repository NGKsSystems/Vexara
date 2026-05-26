#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace VexaraOrchestration {

struct PipelineArtifactEnvelope {
    int schemaVersion = 1;
    qint64 taskId = 0;
    QString sourceAgent;
    QDateTime timestamp;
    QString status;
    QJsonObject payload;
    QString reasoning;
};

class PipelineArtifacts {
public:
    static constexpr int kSchemaVersion = 1;

    static QString fileTaskJson();
    static QString filePlanJson();
    static QString fileResearchSummaryMd();
    static QString fileApprovedPlanJson();
    static QString fileWorkerResultMd();
    static QString fileChangesDiff();
    static QString fileTestResultsJson();
    static QString fileReviewDecisionJson();

    static QJsonObject makeEnvelope(qint64 taskId,
                                    const QString& sourceAgent,
                                    const QString& status,
                                    const QJsonObject& payload,
                                    const QString& reasoning = QString());

    static bool parseEnvelope(const QJsonObject& json, PipelineArtifactEnvelope& out);
    static qint64 jsonToTaskId(const QJsonValue& value);
    static qint64 payloadTaskRef(const QJsonObject& payload, const QString& key);

    static bool writeJson(const QString& projectRoot,
                          qint64 taskId,
                          const QString& fileName,
                          const QJsonObject& document);
    static bool readJson(const QString& projectRoot,
                         qint64 taskId,
                         const QString& fileName,
                         QJsonObject& documentOut);
    static bool writeText(const QString& projectRoot,
                          qint64 taskId,
                          const QString& fileName,
                          const QString& content);
    static bool readText(const QString& projectRoot,
                         qint64 taskId,
                         const QString& fileName,
                         QString& contentOut);

    static bool ensureTaskDir(const QString& projectRoot, qint64 taskId);
};

} // namespace VexaraOrchestration
