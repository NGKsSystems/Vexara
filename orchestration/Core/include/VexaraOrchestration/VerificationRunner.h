#pragma once

#include "VexaraCore/VerificationSettings.h"

#include <QObject>
#include <QString>

class QProcess;

namespace VexaraOrchestration {

class VerificationRunner : public QObject {
    Q_OBJECT

public:
    explicit VerificationRunner(QObject* parent = nullptr);

    void configure(const VexaraCore::VerificationSettings& settings);
    bool isRunning() const;
    void run(const QString& workingDirectory);

signals:
    void verificationStarted();
    void outputChunk(const QString& text);
    void verificationFinished(bool success, const QString& summary);

private:
    void finishProcess(bool success, const QString& summary);

    VexaraCore::VerificationSettings settings_;
    QProcess* process_ = nullptr;
    QString capturedOutput_;
};

} // namespace VexaraOrchestration
