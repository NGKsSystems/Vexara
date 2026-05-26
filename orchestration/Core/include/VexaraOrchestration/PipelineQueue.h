#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct sqlite3;

namespace VexaraOrchestration {

enum class PipelineTaskStatus {
    Pending,
    Running,
    Completed,
    Failed,
    Deferred,
};

struct PipelineTaskRecord {
    qint64 id = 0;
    PipelineTaskStatus status = PipelineTaskStatus::Pending;
    int priority = 0;
    int retryCount = 0;
    int maxRetries = 3;
    QDateTime deferredUntil;
    QJsonObject payload;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct PipelineEventRecord {
    qint64 id = 0;
    qint64 taskId = 0;
    QString agentName;
    QString eventType;
    QDateTime timestamp;
    QJsonObject payload;
};

class PipelineQueue {
public:
    PipelineQueue() = default;
    ~PipelineQueue();

    PipelineQueue(const PipelineQueue&) = delete;
    PipelineQueue& operator=(const PipelineQueue&) = delete;

    bool open(const QString& databasePath);
    void close();
    bool isOpen() const;

    qint64 enqueue(const QJsonObject& payload,
                   int priority = 0,
                   int maxRetries = 3);
    bool claimNextReady(PipelineTaskRecord& taskOut);
    bool markCompleted(qint64 taskId);
    bool markFailed(qint64 taskId, const QString& errorSummary);
    bool getTask(qint64 taskId, PipelineTaskRecord& taskOut) const;

    bool recordEvent(qint64 taskId,
                     const QString& agentName,
                     const QString& eventType,
                     const QJsonObject& payload = QJsonObject());

    void releaseExpiredDeferredTasks();
    int reconcileStaleRunningTasks(const QString& reason = QString());
    int cancelPendingRootPlanningTasks(const QString& reason = QString());
    int clearAllDeferredTasks();

    int pendingCount() const;
    int pendingRootPipelineCount() const;
    int deferredCount() const;
    bool latestRootPlanningUserTask(QString& userTaskOut) const;
    QString lastError() const;

    static QString statusToString(PipelineTaskStatus status);
    static PipelineTaskStatus statusFromString(const QString& value);

    static bool writeArtifactJson(const QString& projectRoot,
                                  qint64 taskId,
                                  const QString& fileName,
                                  const QJsonObject& document);
    static bool readArtifactJson(const QString& projectRoot,
                                 qint64 taskId,
                                 const QString& fileName,
                                 QJsonObject& documentOut);
    static bool writeArtifactText(const QString& projectRoot,
                                  qint64 taskId,
                                  const QString& fileName,
                                  const QString& content);
    static bool readArtifactText(const QString& projectRoot,
                                 qint64 taskId,
                                 const QString& fileName,
                                 QString& contentOut);

private:
    bool ensureSchema();
    bool executeSql(const char* sql);
    bool runMigrations();
    QString isoTimestampUtc(const QDateTime& value) const;
    QDateTime parseIsoTimestamp(const QString& value) const;

    sqlite3* db_ = nullptr;
    QString databasePath_;
    QString lastError_;
};

} // namespace VexaraOrchestration
