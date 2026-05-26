#include "VexaraEditor/WorkspaceEditorHost.h"

#include <QDir>

namespace VexaraEditor {

WorkspaceEditorHost::WorkspaceEditorHost(DocumentWorkspace* workspace, QString projectRoot)
    : workspace_(workspace)
    , projectRoot_(std::move(projectRoot))
{
}

void WorkspaceEditorHost::setProjectRoot(const QString& projectRoot)
{
    projectRoot_ = projectRoot;
}

void WorkspaceEditorHost::setWorkspace(DocumentWorkspace* workspace)
{
    workspace_ = workspace;
}

QString WorkspaceEditorHost::projectRoot() const
{
    return projectRoot_;
}

QString WorkspaceEditorHost::absolutePath(const QString& path) const
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    QFileInfo info(trimmed);
    if (info.isAbsolute()) {
        return info.absoluteFilePath();
    }

    if (projectRoot_.trimmed().isEmpty()) {
        return QString();
    }

    return QFileInfo(QDir(projectRoot_).filePath(trimmed)).absoluteFilePath();
}

QString WorkspaceEditorHost::resolvePath(const QString& relativeOrAbsolute) const
{
    return absolutePath(relativeOrAbsolute);
}

bool WorkspaceEditorHost::openFileAt(const QString& path, int line, int column)
{
    if (!workspace_) {
        return false;
    }
    const QString resolved = absolutePath(path);
    if (resolved.isEmpty()) {
        return false;
    }
    return workspace_->openFileAt(resolved, line, column);
}

bool WorkspaceEditorHost::applyReplacement(const QString& path,
                                           int line,
                                           int column,
                                           int replaceLength,
                                           const QString& newText)
{
    if (!workspace_) {
        return false;
    }
    const QString resolved = absolutePath(path);
    if (resolved.isEmpty()) {
        return false;
    }
    return workspace_->applyReplacement(resolved, line, column, replaceLength, newText);
}

bool WorkspaceEditorHost::reloadFromDisk(const QString& path)
{
    if (!workspace_) {
        return false;
    }
    const QString resolved = absolutePath(path);
    if (resolved.isEmpty()) {
        return false;
    }
    return workspace_->reloadFromDisk(resolved);
}

} // namespace VexaraEditor
