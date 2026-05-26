#pragma once

#include <QString>

namespace VexaraOrchestration {

class IWorkerEditorHost {
public:
    virtual ~IWorkerEditorHost() = default;

    virtual QString projectRoot() const = 0;
    virtual QString resolvePath(const QString& relativeOrAbsolute) const = 0;
    virtual bool openFileAt(const QString& path, int line = 1, int column = 1) = 0;
    virtual bool applyReplacement(const QString& path,
                                  int line,
                                  int column,
                                  int replaceLength,
                                  const QString& newText) = 0;
    virtual bool reloadFromDisk(const QString& path) = 0;
};

} // namespace VexaraOrchestration
