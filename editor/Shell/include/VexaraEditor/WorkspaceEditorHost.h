#pragma once

#include "VexaraEditor/DocumentWorkspace.h"
#include "VexaraOrchestration/IWorkerEditorHost.h"

#include <QFileInfo>

namespace VexaraEditor {

class WorkspaceEditorHost : public VexaraOrchestration::IWorkerEditorHost {
public:
    WorkspaceEditorHost() = default;
    WorkspaceEditorHost(DocumentWorkspace* workspace, QString projectRoot);

    void setProjectRoot(const QString& projectRoot);
    void setWorkspace(DocumentWorkspace* workspace);

    QString projectRoot() const override;
    QString resolvePath(const QString& relativeOrAbsolute) const override;
    bool openFileAt(const QString& path, int line = 1, int column = 1) override;
    bool applyReplacement(const QString& path,
                          int line,
                          int column,
                          int replaceLength,
                          const QString& newText) override;
    bool reloadFromDisk(const QString& path) override;

private:
    QString absolutePath(const QString& path) const;

    DocumentWorkspace* workspace_ = nullptr;
    QString projectRoot_;
};

} // namespace VexaraEditor
