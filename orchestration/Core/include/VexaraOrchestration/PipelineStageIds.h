#pragma once

#include <QString>

namespace VexaraOrchestration::PipelineStages {

inline QString planning()
{
    return QStringLiteral("planning");
}

inline QString supervisorReview()
{
    return QStringLiteral("supervisor_review");
}

inline QString workerExecution()
{
    return QStringLiteral("worker");
}

inline QString testing()
{
    return QStringLiteral("testing");
}

inline QString review()
{
    return QStringLiteral("review");
}

} // namespace VexaraOrchestration::PipelineStages

namespace VexaraOrchestration::PipelineQueuePriority {

// Root planning tasks use the default priority so multiple pipelines can be queued.
constexpr int kRootPipeline = 0;
// Intra-pipeline stages must run before the next root planning task is claimed.
constexpr int kIntraPipelineStage = 100;

} // namespace VexaraOrchestration::PipelineQueuePriority
