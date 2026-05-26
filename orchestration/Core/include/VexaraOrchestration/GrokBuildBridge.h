#pragma once

#include "VexaraCore/GrokBuildSettings.h"

#include <QObject>
#include <QString>

class QProcess;

namespace VexaraOrchestration {

class GrokBuildBridge : public QObject {
    Q_OBJECT

public:
    explicit GrokBuildBridge(QObject* parent = nullptr);

    void configure(const VexaraCore::GrokBuildSettings& settings);
    bool isConfigured() const;
    bool isRunning() const;

    void runTask(const QString& prompt, const QString& workingDirectory);
    void cancelActiveTask();

signals:
    void taskStarted(const QString& prompt);
    void outputChunk(const QString& text);
    void taskFinished(bool success, const QString& summary);

private:
    void finishProcess(bool success, const QString& summary);

    VexaraCore::GrokBuildSettings settings_;
    QProcess* process_ = nullptr;
    QString capturedOutput_;
};

} // namespace VexaraOrchestration
