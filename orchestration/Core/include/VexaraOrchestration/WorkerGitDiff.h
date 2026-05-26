#pragma once

#include <QString>
#include <QStringList>

namespace VexaraOrchestration {

class WorkerGitDiff {
public:
    static QString captureDiff(const QString& projectRoot);
    static QStringList changedFiles(const QString& projectRoot);
    static bool isGitRepository(const QString& projectRoot);
};

} // namespace VexaraOrchestration
