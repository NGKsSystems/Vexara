#pragma once

#include "VexaraCore/AiderSettings.h"

#include <QObject>
#include <QString>

class QProcess;

namespace VexaraOrchestration {

class AiderBridge : public QObject {
    Q_OBJECT

public:
    explicit AiderBridge(QObject* parent = nullptr);

    void configure(const VexaraCore::AiderSettings& settings);
    bool isConfigured() const;
    bool isRunning() const;

    void runTask(const QString& prompt,
                 const QString& workingDirectory,
                 const QString& programOverride = QString(),
                 const QStringList& targetFiles = QStringList());
    void cancelActiveTask();

signals:
    void taskStarted(const QString& prompt);
    void outputChunk(const QString& text);
    void taskFinished(bool success, const QString& summary);

private:
    void finishProcess(bool success, const QString& summary);

    VexaraCore::AiderSettings settings_;
    QProcess* process_ = nullptr;
    QString capturedOutput_;
};

} // namespace VexaraOrchestration
