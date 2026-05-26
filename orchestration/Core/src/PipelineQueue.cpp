#include "VexaraOrchestration/PipelineQueue.h"

#include "VexaraOrchestration/PipelineArtifacts.h"
#include "VexaraOrchestration/PipelineStageIds.h"

#include <sqlite3.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QVector>

namespace VexaraOrchestration {

namespace {

constexpr int kSchemaVersion = 1;
constexpr int kDefaultDeferHours = 1;

} // namespace

PipelineQueue::~PipelineQueue()
{
    close();
}

bool PipelineQueue::open(const QString& databasePath)
{
    close();
    if (databasePath.trimmed().isEmpty()) {
        lastError_ = QStringLiteral("Pipeline queue path is empty.");
        return false;
    }

    QDir().mkpath(QFileInfo(databasePath).absolutePath());

    const QByteArray pathUtf8 = QFileInfo(databasePath).absoluteFilePath().toUtf8();
    const int rc = sqlite3_open_v2(pathUtf8.constData(),
                                   &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK || db_ == nullptr) {
        lastError_ = QStringLiteral("Failed to open pipeline queue: %1")
                         .arg(db_ ? QString::fromUtf8(sqlite3_errmsg(db_)) : QStringLiteral("unknown"));
        close();
        return false;
    }

    databasePath_ = QFileInfo(databasePath).absoluteFilePath();
    sqlite3_busy_timeout(db_, 5000);

    if (!executeSql("PRAGMA foreign_keys = ON;") || !ensureSchema()) {
        close();
        return false;
    }

    return true;
}

void PipelineQueue::close()
{
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    databasePath_.clear();
}

bool PipelineQueue::isOpen() const
{
    return db_ != nullptr;
}

QString PipelineQueue::lastError() const
{
    return lastError_;
}

QString PipelineQueue::statusToString(PipelineTaskStatus status)
{
    switch (status) {
    case PipelineTaskStatus::Pending:
        return QStringLiteral("pending");
    case PipelineTaskStatus::Running:
        return QStringLiteral("running");
    case PipelineTaskStatus::Completed:
        return QStringLiteral("completed");
    case PipelineTaskStatus::Failed:
        return QStringLiteral("failed");
    case PipelineTaskStatus::Deferred:
        return QStringLiteral("deferred");
    }
    return QStringLiteral("pending");
}

PipelineTaskStatus PipelineQueue::statusFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("running")) {
        return PipelineTaskStatus::Running;
    }
    if (normalized == QStringLiteral("completed")) {
        return PipelineTaskStatus::Completed;
    }
    if (normalized == QStringLiteral("failed")) {
        return PipelineTaskStatus::Failed;
    }
    if (normalized == QStringLiteral("deferred")) {
        return PipelineTaskStatus::Deferred;
    }
    return PipelineTaskStatus::Pending;
}

bool PipelineQueue::executeSql(const char* sql)
{
    char* errMsg = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        lastError_ = QString::fromUtf8(errMsg ? errMsg : sqlite3_errmsg(db_));
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool PipelineQueue::ensureSchema()
{
  return runMigrations();
}

bool PipelineQueue::runMigrations()
{
    if (!executeSql("CREATE TABLE IF NOT EXISTS schema_version ("
                    "version INTEGER NOT NULL"
                    ");")) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    int version = 0;
    if (sqlite3_prepare_v2(db_, "SELECT version FROM schema_version LIMIT 1;", -1, &stmt, nullptr) ==
        SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            version = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (version < 1) {
        const char* migrationV1 =
            "BEGIN;"
            "CREATE TABLE IF NOT EXISTS tasks ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  status TEXT NOT NULL DEFAULT 'pending',"
            "  priority INTEGER NOT NULL DEFAULT 0,"
            "  retry_count INTEGER NOT NULL DEFAULT 0,"
            "  max_retries INTEGER NOT NULL DEFAULT 3,"
            "  deferred_until TEXT,"
            "  payload_json TEXT NOT NULL,"
            "  created_at TEXT NOT NULL,"
            "  updated_at TEXT NOT NULL"
            ");"
            "CREATE TABLE IF NOT EXISTS events ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  task_id INTEGER NOT NULL,"
            "  agent_name TEXT NOT NULL,"
            "  event_type TEXT NOT NULL,"
            "  timestamp TEXT NOT NULL,"
            "  payload_json TEXT NOT NULL DEFAULT '{}',"
            "  FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE"
            ");"
            "CREATE TABLE IF NOT EXISTS deferred_tasks ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  task_id INTEGER NOT NULL UNIQUE,"
            "  deferred_until TEXT NOT NULL,"
            "  reason TEXT NOT NULL,"
            "  payload_json TEXT NOT NULL,"
            "  created_at TEXT NOT NULL,"
            "  FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_tasks_ready ON tasks(status, priority DESC, created_at);"
            "CREATE INDEX IF NOT EXISTS idx_tasks_deferred_until ON tasks(deferred_until);"
            "CREATE INDEX IF NOT EXISTS idx_events_task_id ON events(task_id, timestamp);"
            "CREATE INDEX IF NOT EXISTS idx_events_agent_name ON events(agent_name);"
            "CREATE INDEX IF NOT EXISTS idx_deferred_tasks_until ON deferred_tasks(deferred_until);"
            "DELETE FROM schema_version;"
            "INSERT INTO schema_version(version) VALUES(1);"
            "COMMIT;";

        if (!executeSql(migrationV1)) {
            return false;
        }
        version = kSchemaVersion;
    }

    if (version != kSchemaVersion) {
        lastError_ = QStringLiteral("Unsupported pipeline queue schema version %1.").arg(version);
        return false;
    }

    return true;
}

QString PipelineQueue::isoTimestampUtc(const QDateTime& value) const
{
    return value.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime PipelineQueue::parseIsoTimestamp(const QString& value) const
{
    return QDateTime::fromString(value, Qt::ISODateWithMs).toUTC();
}

qint64 PipelineQueue::enqueue(const QJsonObject& payload, int priority, int maxRetries)
{
    if (!isOpen()) {
        lastError_ = QStringLiteral("Pipeline queue is not open.");
        return 0;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QByteArray payloadJson =
        QJsonDocument(payload).toJson(QJsonDocument::Compact);

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO tasks(status, priority, retry_count, max_retries, deferred_until, payload_json, "
        "created_at, updated_at) "
        "VALUES('pending', ?, 0, ?, NULL, ?, ?, ?);";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        return 0;
    }

    sqlite3_bind_int(stmt, 1, priority);
    sqlite3_bind_int(stmt, 2, qMax(1, maxRetries));
    sqlite3_bind_text(stmt, 3, payloadJson.constData(), payloadJson.size(), SQLITE_TRANSIENT);
    const QByteArray createdAt = isoTimestampUtc(now).toUtf8();
    sqlite3_bind_text(stmt, 4, createdAt.constData(), createdAt.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, createdAt.constData(), createdAt.size(), SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        return 0;
    }

  return sqlite3_last_insert_rowid(db_);
}

bool PipelineQueue::claimNextReady(PipelineTaskRecord& taskOut)
{
    if (!isOpen()) {
        lastError_ = QStringLiteral("Pipeline queue is not open.");
        return false;
    }

    releaseExpiredDeferredTasks();

    if (!executeSql("BEGIN IMMEDIATE;")) {
        return false;
    }

    sqlite3_stmt* selectStmt = nullptr;
    const char* selectSql =
        "SELECT t.id, t.status, t.priority, t.retry_count, t.max_retries, t.deferred_until, "
        "t.payload_json, t.created_at, t.updated_at "
        "FROM tasks t "
        "WHERE t.status = 'pending' "
        "AND (t.deferred_until IS NULL OR t.deferred_until <= ?) "
        "AND t.id NOT IN ("
        "  SELECT dt.task_id FROM deferred_tasks dt WHERE dt.deferred_until > ?"
        ") "
        "ORDER BY t.priority DESC, t.created_at ASC "
        "LIMIT 1;";

    const QString nowIso = isoTimestampUtc(QDateTime::currentDateTimeUtc());
    const QByteArray nowUtf8 = nowIso.toUtf8();

    if (sqlite3_prepare_v2(db_, selectSql, -1, &selectStmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        executeSql("ROLLBACK;");
        return false;
    }

    sqlite3_bind_text(selectStmt, 1, nowUtf8.constData(), nowUtf8.size(), SQLITE_STATIC);
    sqlite3_bind_text(selectStmt, 2, nowUtf8.constData(), nowUtf8.size(), SQLITE_STATIC);

    if (sqlite3_step(selectStmt) != SQLITE_ROW) {
        sqlite3_finalize(selectStmt);
        executeSql("COMMIT;");
        return false;
    }

    taskOut.id = sqlite3_column_int64(selectStmt, 0);
    taskOut.status = statusFromString(QString::fromUtf8(
        reinterpret_cast<const char*>(sqlite3_column_text(selectStmt, 1))));
    taskOut.priority = sqlite3_column_int(selectStmt, 2);
    taskOut.retryCount = sqlite3_column_int(selectStmt, 3);
    taskOut.maxRetries = sqlite3_column_int(selectStmt, 4);
    if (sqlite3_column_type(selectStmt, 5) != SQLITE_NULL) {
        taskOut.deferredUntil =
            parseIsoTimestamp(QString::fromUtf8(
                reinterpret_cast<const char*>(sqlite3_column_text(selectStmt, 5))));
    }
    const char* payloadText =
        reinterpret_cast<const char*>(sqlite3_column_text(selectStmt, 6));
    const QJsonDocument payloadDoc =
        QJsonDocument::fromJson(QByteArray(payloadText ? payloadText : "{}"));
    taskOut.payload = payloadDoc.isObject() ? payloadDoc.object() : QJsonObject();
    taskOut.createdAt = parseIsoTimestamp(QString::fromUtf8(
        reinterpret_cast<const char*>(sqlite3_column_text(selectStmt, 7))));
    taskOut.updatedAt = parseIsoTimestamp(QString::fromUtf8(
        reinterpret_cast<const char*>(sqlite3_column_text(selectStmt, 8))));
    sqlite3_finalize(selectStmt);

    sqlite3_stmt* updateStmt = nullptr;
    const char* updateSql =
        "UPDATE tasks SET status='running', updated_at=? WHERE id=? AND status='pending';";
    if (sqlite3_prepare_v2(db_, updateSql, -1, &updateStmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        executeSql("ROLLBACK;");
        return false;
    }

    sqlite3_bind_text(updateStmt, 1, nowUtf8.constData(), nowUtf8.size(), SQLITE_STATIC);
    sqlite3_bind_int64(updateStmt, 2, taskOut.id);
    const int updateRc = sqlite3_step(updateStmt);
    sqlite3_finalize(updateStmt);

    if (updateRc != SQLITE_DONE || sqlite3_changes(db_) != 1) {
        executeSql("ROLLBACK;");
        return false;
    }

    taskOut.status = PipelineTaskStatus::Running;
    taskOut.updatedAt = QDateTime::currentDateTimeUtc();

    if (!executeSql("COMMIT;")) {
        return false;
    }

    return true;
}

bool PipelineQueue::markCompleted(qint64 taskId)
{
    if (!isOpen() || taskId <= 0) {
        return false;
    }

    const QByteArray now = isoTimestampUtc(QDateTime::currentDateTimeUtc()).toUtf8();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE tasks SET status='completed', updated_at=? WHERE id=? AND status='running';";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_text(stmt, 1, now.constData(), now.size(), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, taskId);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE || sqlite3_changes(db_) != 1) {
        lastError_ = QStringLiteral("Failed to mark task %1 completed.").arg(taskId);
        return false;
    }

    return true;
}

bool PipelineQueue::markFailed(qint64 taskId, const QString& errorSummary)
{
    if (!isOpen() || taskId <= 0) {
        return false;
    }

    PipelineTaskRecord task;
    if (!getTask(taskId, task)) {
        return false;
    }

    const int nextRetry = task.retryCount + 1;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QByteArray nowIso = isoTimestampUtc(now).toUtf8();

    if (nextRetry >= task.maxRetries) {
        const QDateTime deferUntil = now.addSecs(kDefaultDeferHours * 3600);
        const QByteArray deferIso = isoTimestampUtc(deferUntil).toUtf8();
        const QByteArray payloadJson =
            QJsonDocument(task.payload).toJson(QJsonDocument::Compact);
        const QByteArray reason = errorSummary.toUtf8();

        if (!executeSql("BEGIN IMMEDIATE;")) {
            return false;
        }

        sqlite3_stmt* updateStmt = nullptr;
        const char* updateSql =
            "UPDATE tasks SET status='deferred', retry_count=?, deferred_until=?, updated_at=? "
            "WHERE id=?;";

        if (sqlite3_prepare_v2(db_, updateSql, -1, &updateStmt, nullptr) != SQLITE_OK) {
            lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
            executeSql("ROLLBACK;");
            return false;
        }

        sqlite3_bind_int(updateStmt, 1, nextRetry);
        sqlite3_bind_text(updateStmt, 2, deferIso.constData(), deferIso.size(), SQLITE_STATIC);
        sqlite3_bind_text(updateStmt, 3, nowIso.constData(), nowIso.size(), SQLITE_STATIC);
        sqlite3_bind_int64(updateStmt, 4, taskId);
        const int updateRc = sqlite3_step(updateStmt);
        sqlite3_finalize(updateStmt);

        if (updateRc != SQLITE_DONE) {
            lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
            executeSql("ROLLBACK;");
            return false;
        }

        sqlite3_stmt* deferStmt = nullptr;
        const char* deferSql =
            "INSERT INTO deferred_tasks(task_id, deferred_until, reason, payload_json, created_at) "
            "VALUES(?, ?, ?, ?, ?) "
            "ON CONFLICT(task_id) DO UPDATE SET "
            "deferred_until=excluded.deferred_until, reason=excluded.reason, "
            "payload_json=excluded.payload_json, created_at=excluded.created_at;";

        if (sqlite3_prepare_v2(db_, deferSql, -1, &deferStmt, nullptr) != SQLITE_OK) {
            lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
            executeSql("ROLLBACK;");
            return false;
        }

        sqlite3_bind_int64(deferStmt, 1, taskId);
        sqlite3_bind_text(deferStmt, 2, deferIso.constData(), deferIso.size(), SQLITE_STATIC);
        sqlite3_bind_text(deferStmt, 3, reason.constData(), reason.size(), SQLITE_TRANSIENT);
        sqlite3_bind_text(deferStmt, 4, payloadJson.constData(), payloadJson.size(), SQLITE_STATIC);
        sqlite3_bind_text(deferStmt, 5, nowIso.constData(), nowIso.size(), SQLITE_STATIC);
        const int deferRc = sqlite3_step(deferStmt);
        sqlite3_finalize(deferStmt);

        if (deferRc != SQLITE_DONE) {
            lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
            executeSql("ROLLBACK;");
            return false;
        }

        if (!executeSql("COMMIT;")) {
            return false;
        }

        recordEvent(taskId,
                    QStringLiteral("director"),
                    QStringLiteral("deferred"),
                    QJsonObject{{QStringLiteral("error"), errorSummary},
                                {QStringLiteral("retry_count"), nextRetry}});
        return true;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE tasks SET status='pending', retry_count=?, updated_at=? WHERE id=?;";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_int(stmt, 1, nextRetry);
    sqlite3_bind_text(stmt, 2, nowIso.constData(), nowIso.size(), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, taskId);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        return false;
    }

    recordEvent(taskId,
                QStringLiteral("director"),
                QStringLiteral("retry_scheduled"),
                QJsonObject{{QStringLiteral("error"), errorSummary},
                            {QStringLiteral("retry_count"), nextRetry}});
    return true;
}

bool PipelineQueue::getTask(qint64 taskId, PipelineTaskRecord& taskOut) const
{
    if (!isOpen() || taskId <= 0) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, status, priority, retry_count, max_retries, deferred_until, payload_json, "
        "created_at, updated_at FROM tasks WHERE id=?;";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, taskId);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    taskOut.id = sqlite3_column_int64(stmt, 0);
    taskOut.status = statusFromString(QString::fromUtf8(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))));
    taskOut.priority = sqlite3_column_int(stmt, 2);
    taskOut.retryCount = sqlite3_column_int(stmt, 3);
    taskOut.maxRetries = sqlite3_column_int(stmt, 4);
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
        taskOut.deferredUntil = parseIsoTimestamp(QString::fromUtf8(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))));
    }
    const char* payloadText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    const QJsonDocument payloadDoc =
        QJsonDocument::fromJson(QByteArray(payloadText ? payloadText : "{}"));
    taskOut.payload = payloadDoc.isObject() ? payloadDoc.object() : QJsonObject();
    taskOut.createdAt = parseIsoTimestamp(QString::fromUtf8(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7))));
    taskOut.updatedAt = parseIsoTimestamp(QString::fromUtf8(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8))));
    sqlite3_finalize(stmt);
    return true;
}

bool PipelineQueue::recordEvent(qint64 taskId,
                                const QString& agentName,
                                const QString& eventType,
                                const QJsonObject& payload)
{
    if (!isOpen() || taskId <= 0) {
        return false;
    }

    const QByteArray payloadJson =
        QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QByteArray timestamp = isoTimestampUtc(QDateTime::currentDateTimeUtc()).toUtf8();
    const QByteArray agentUtf8 = agentName.toUtf8();
    const QByteArray typeUtf8 = eventType.toUtf8();

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO events(task_id, agent_name, event_type, timestamp, payload_json) "
        "VALUES(?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_int64(stmt, 1, taskId);
    sqlite3_bind_text(stmt, 2, agentUtf8.constData(), agentUtf8.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, typeUtf8.constData(), typeUtf8.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, timestamp.constData(), timestamp.size(), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, payloadJson.constData(), payloadJson.size(), SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        return false;
    }

    return true;
}

int PipelineQueue::pendingCount() const
{
    if (!isOpen()) {
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM tasks WHERE status='pending';", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int PipelineQueue::pendingRootPipelineCount() const
{
    if (!isOpen()) {
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT payload_json FROM tasks WHERE status='pending';", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return 0;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* payloadText =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const QJsonDocument payloadDoc =
            QJsonDocument::fromJson(QByteArray(payloadText ? payloadText : "{}"));
        if (!payloadDoc.isObject()) {
            continue;
        }
        const QJsonObject payload = payloadDoc.object();
        const QString kind = payload.value(QStringLiteral("kind")).toString();
        const int protocolVersion = payload.value(QStringLiteral("protocol_version")).toInt();
        const QString stage =
            payload.value(QStringLiteral("stage")).toString().trimmed().toLower();
        const bool hierarchical =
            protocolVersion >= 2 || kind == QStringLiteral("hierarchical_stage");
        if (hierarchical && stage == PipelineStages::planning()) {
            ++count;
        }
    }
    sqlite3_finalize(stmt);
    return count;
}

bool PipelineQueue::latestRootPlanningUserTask(QString& userTaskOut) const
{
    userTaskOut.clear();
    if (!isOpen()) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT payload_json FROM tasks ORDER BY id DESC LIMIT 200;",
                           -1,
                           &stmt,
                           nullptr) != SQLITE_OK) {
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* payloadText =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const QJsonDocument payloadDoc =
            QJsonDocument::fromJson(QByteArray(payloadText ? payloadText : "{}"));
        if (!payloadDoc.isObject()) {
            continue;
        }
        const QJsonObject payload = payloadDoc.object();
        const QString kind = payload.value(QStringLiteral("kind")).toString();
        const int protocolVersion = payload.value(QStringLiteral("protocol_version")).toInt();
        const QString stage =
            payload.value(QStringLiteral("stage")).toString().trimmed().toLower();
        const bool hierarchical =
            protocolVersion >= 2 || kind == QStringLiteral("hierarchical_stage");
        if (!hierarchical || stage != PipelineStages::planning()) {
            continue;
        }
        const QString userTask = payload.value(QStringLiteral("user_task")).toString().trimmed();
        if (!userTask.isEmpty()) {
            userTaskOut = userTask;
            sqlite3_finalize(stmt);
            return true;
        }
    }

    sqlite3_finalize(stmt);
    return false;
}

int PipelineQueue::deferredCount() const
{
    if (!isOpen()) {
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM deferred_tasks;", -1, &stmt, nullptr) !=
        SQLITE_OK) {
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int PipelineQueue::reconcileStaleRunningTasks(const QString& reason)
{
    if (!isOpen()) {
        return 0;
    }

    QVector<qint64> runningTaskIds;
    sqlite3_stmt* selectStmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT id FROM tasks WHERE status='running';", -1, &selectStmt,
                           nullptr) != SQLITE_OK) {
        return 0;
    }
    while (sqlite3_step(selectStmt) == SQLITE_ROW) {
        runningTaskIds.append(sqlite3_column_int64(selectStmt, 0));
    }
    sqlite3_finalize(selectStmt);

    if (runningTaskIds.isEmpty()) {
        return 0;
    }

    const QString recoveryReason = reason.trimmed().isEmpty()
                                       ? QStringLiteral("Recovered after interrupted run.")
                                       : reason.trimmed();
    const QByteArray nowIso = isoTimestampUtc(QDateTime::currentDateTimeUtc()).toUtf8();

    if (!executeSql("BEGIN IMMEDIATE;")) {
        return 0;
    }

    sqlite3_stmt* updateStmt = nullptr;
    const char* updateSql =
        "UPDATE tasks SET status='completed', updated_at=? WHERE status='running';";
    if (sqlite3_prepare_v2(db_, updateSql, -1, &updateStmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        executeSql("ROLLBACK;");
        return 0;
    }

    sqlite3_bind_text(updateStmt, 1, nowIso.constData(), nowIso.size(), SQLITE_STATIC);
    const int updateRc = sqlite3_step(updateStmt);
    sqlite3_finalize(updateStmt);

    if (updateRc != SQLITE_DONE) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        executeSql("ROLLBACK;");
        return 0;
    }

    if (!executeSql("COMMIT;")) {
        return 0;
    }

    for (qint64 taskId : runningTaskIds) {
        recordEvent(taskId,
                    QStringLiteral("director"),
                    QStringLiteral("recovered"),
                    QJsonObject{{QStringLiteral("reason"), recoveryReason}});
    }

    return runningTaskIds.size();
}

int PipelineQueue::cancelPendingRootPlanningTasks(const QString& reason)
{
    if (!isOpen()) {
        return 0;
    }

    const QString cancelReason = reason.trimmed().isEmpty()
                                     ? QStringLiteral("Cancelled by user.")
                                     : reason.trimmed();
    QVector<qint64> taskIds;
    sqlite3_stmt* selectStmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT id, payload_json FROM tasks WHERE status='pending';", -1,
                           &selectStmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    while (sqlite3_step(selectStmt) == SQLITE_ROW) {
        const qint64 taskId = sqlite3_column_int64(selectStmt, 0);
        const char* payloadText =
            reinterpret_cast<const char*>(sqlite3_column_text(selectStmt, 1));
        const QJsonDocument payloadDoc =
            QJsonDocument::fromJson(QByteArray(payloadText ? payloadText : "{}"));
        if (!payloadDoc.isObject()) {
            continue;
        }
        const QJsonObject payload = payloadDoc.object();
        const QString kind = payload.value(QStringLiteral("kind")).toString();
        const int protocolVersion = payload.value(QStringLiteral("protocol_version")).toInt();
        const QString stage =
            payload.value(QStringLiteral("stage")).toString().trimmed().toLower();
        const bool hierarchical =
            protocolVersion >= 2 || kind == QStringLiteral("hierarchical_stage");
        if (hierarchical && stage == PipelineStages::planning()) {
            taskIds.append(taskId);
        }
    }
    sqlite3_finalize(selectStmt);

    if (taskIds.isEmpty()) {
        return 0;
    }

    const QByteArray nowIso = isoTimestampUtc(QDateTime::currentDateTimeUtc()).toUtf8();
    if (!executeSql("BEGIN IMMEDIATE;")) {
        return 0;
    }

    sqlite3_stmt* updateStmt = nullptr;
    const char* updateSql =
        "UPDATE tasks SET status='completed', updated_at=? WHERE id=? AND status='pending';";
    if (sqlite3_prepare_v2(db_, updateSql, -1, &updateStmt, nullptr) != SQLITE_OK) {
        executeSql("ROLLBACK;");
        return 0;
    }

    for (qint64 taskId : taskIds) {
        sqlite3_bind_text(updateStmt, 1, nowIso.constData(), nowIso.size(), SQLITE_STATIC);
        sqlite3_bind_int64(updateStmt, 2, taskId);
        sqlite3_step(updateStmt);
        sqlite3_reset(updateStmt);
    }
    sqlite3_finalize(updateStmt);

    if (!executeSql("COMMIT;")) {
        return 0;
    }

    for (qint64 taskId : taskIds) {
        recordEvent(taskId,
                    QStringLiteral("director"),
                    QStringLiteral("cancelled"),
                    QJsonObject{{QStringLiteral("reason"), cancelReason}});
    }

    return taskIds.size();
}

int PipelineQueue::clearAllDeferredTasks()
{
    if (!isOpen()) {
        lastError_ = QStringLiteral("Pipeline queue is not open.");
        return 0;
    }

    QVector<qint64> deferredTaskIds;
    sqlite3_stmt* selectStmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT id FROM tasks WHERE status='deferred';", -1, &selectStmt,
                           nullptr) == SQLITE_OK) {
        while (sqlite3_step(selectStmt) == SQLITE_ROW) {
            deferredTaskIds.append(sqlite3_column_int64(selectStmt, 0));
        }
        sqlite3_finalize(selectStmt);
    }

    if (deferredTaskIds.isEmpty()) {
        executeSql("DELETE FROM deferred_tasks;");
        return 0;
    }

    const QString nowIso = isoTimestampUtc(QDateTime::currentDateTimeUtc());
    const QByteArray nowUtf8 = nowIso.toUtf8();

    if (!executeSql("BEGIN IMMEDIATE;")) {
        return 0;
    }

    sqlite3_stmt* deleteStmt = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM deferred_tasks;", -1, &deleteStmt, nullptr) == SQLITE_OK) {
        sqlite3_step(deleteStmt);
        sqlite3_finalize(deleteStmt);
    }

    sqlite3_stmt* updateStmt = nullptr;
    const char* updateSql =
        "UPDATE tasks SET status='completed', deferred_until=NULL, updated_at=? "
        "WHERE status='deferred';";
    if (sqlite3_prepare_v2(db_, updateSql, -1, &updateStmt, nullptr) != SQLITE_OK) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        executeSql("ROLLBACK;");
        return 0;
    }

    sqlite3_bind_text(updateStmt, 1, nowUtf8.constData(), nowUtf8.size(), SQLITE_STATIC);
    const int updateRc = sqlite3_step(updateStmt);
    sqlite3_finalize(updateStmt);

    if (updateRc != SQLITE_DONE) {
        lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
        executeSql("ROLLBACK;");
        return 0;
    }

    if (!executeSql("COMMIT;")) {
        return 0;
    }

    for (qint64 taskId : deferredTaskIds) {
        recordEvent(taskId,
                    QStringLiteral("director"),
                    QStringLiteral("deferred_cleared"),
                    QJsonObject{{QStringLiteral("reason"), QStringLiteral("Cleared by user.")}});
    }

    return deferredTaskIds.size();
}

void PipelineQueue::releaseExpiredDeferredTasks()
{
    if (!isOpen()) {
        return;
    }

    const QString nowIso = isoTimestampUtc(QDateTime::currentDateTimeUtc());
    const QByteArray nowUtf8 = nowIso.toUtf8();

    sqlite3_stmt* deleteStmt = nullptr;
    const char* deleteSql = "DELETE FROM deferred_tasks WHERE deferred_until <= ?;";
    if (sqlite3_prepare_v2(db_, deleteSql, -1, &deleteStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(deleteStmt, 1, nowUtf8.constData(), nowUtf8.size(), SQLITE_STATIC);
        sqlite3_step(deleteStmt);
        sqlite3_finalize(deleteStmt);
    }

    sqlite3_stmt* updateStmt = nullptr;
    const char* updateSql =
        "UPDATE tasks SET status='pending', deferred_until=NULL, updated_at=? "
        "WHERE status='deferred' AND deferred_until IS NOT NULL AND deferred_until <= ?;";
    if (sqlite3_prepare_v2(db_, updateSql, -1, &updateStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(updateStmt, 1, nowUtf8.constData(), nowUtf8.size(), SQLITE_STATIC);
        sqlite3_bind_text(updateStmt, 2, nowUtf8.constData(), nowUtf8.size(), SQLITE_STATIC);
        sqlite3_step(updateStmt);
        sqlite3_finalize(updateStmt);
    }
}

bool PipelineQueue::writeArtifactJson(const QString& projectRoot,
                                      qint64 taskId,
                                      const QString& fileName,
                                      const QJsonObject& document)
{
    return PipelineArtifacts::writeJson(projectRoot, taskId, fileName, document);
}

bool PipelineQueue::readArtifactJson(const QString& projectRoot,
                                     qint64 taskId,
                                     const QString& fileName,
                                     QJsonObject& documentOut)
{
    return PipelineArtifacts::readJson(projectRoot, taskId, fileName, documentOut);
}

bool PipelineQueue::writeArtifactText(const QString& projectRoot,
                                      qint64 taskId,
                                      const QString& fileName,
                                      const QString& content)
{
    return PipelineArtifacts::writeText(projectRoot, taskId, fileName, content);
}

bool PipelineQueue::readArtifactText(const QString& projectRoot,
                                     qint64 taskId,
                                     const QString& fileName,
                                     QString& contentOut)
{
    return PipelineArtifacts::readText(projectRoot, taskId, fileName, contentOut);
}

} // namespace VexaraOrchestration
