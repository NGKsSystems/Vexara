#pragma once

#include "VexaraCore/AgentServiceKind.h"
#include "VexaraOrchestration/AgentSnapshot.h"
#include "VexaraOrchestration/TaskContext.h"

#include <QFlags>
#include <QObject>
#include <QString>

namespace VexaraOrchestration {

enum class TaskBackendCapability {
    Execution = 1,
    Planning = 2,
    Review = 4,
};

Q_DECLARE_FLAGS(TaskBackendCapabilities, TaskBackendCapability)
Q_DECLARE_OPERATORS_FOR_FLAGS(TaskBackendCapabilities)

class ITaskBackend : public QObject {
    Q_OBJECT

public:
    explicit ITaskBackend(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    virtual VexaraCore::AgentServiceKind serviceKind() const = 0;
    virtual TaskBackendCapabilities capabilities() const = 0;
    virtual bool isConfigured() const = 0;
    virtual bool isRunning() const = 0;
    virtual void executeTask(AgentRole role,
                             const QString& composedPrompt,
                             const TaskContext& context) = 0;

signals:
    void taskStarted(AgentRole role, const QString& prompt);
    void outputChunk(AgentRole role, const QString& text);
    void taskFinished(AgentRole role, bool success, const QString& summary);
};

} // namespace VexaraOrchestration
