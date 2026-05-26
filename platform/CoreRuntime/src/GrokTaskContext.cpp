#include "VexaraCore/GrokTaskContext.h"

#include <QDir>
#include <QFileInfo>

namespace VexaraCore {

QString defaultGrokPromptTemplate()
{
    return QStringLiteral(
        "You are Grok Build, an expert coding agent working inside the project at:\n\n"
        "Current working directory: {current_project_path}\n\n"
        "Project type: {detected_type}\n"
        "Open file: {current_file_path}\n"
        "Selected text / cursor context: {selected_text}\n\n"
        "### Task\n"
        "{prompt}\n\n"
        "### Rules\n"
        "- Work inside the current project directory.\n"
        "- Prefer making precise, minimal changes.\n"
        "- If you need to explore the codebase first, do it.\n"
        "- When editing files, use the proper tools (don't just describe changes).\n"
        "- After making changes, briefly explain what you did and why.\n"
        "- If something is unclear, ask for clarification instead of guessing.\n"
        "- Respect existing code style and architecture.\n\n"
        "Begin.");
}

QString detectProjectType(const QString& projectRoot)
{
    if (projectRoot.trimmed().isEmpty()) {
        return QStringLiteral("unknown");
    }

    const QDir root(projectRoot);
    const auto exists = [&root](const QString& name) {
        return QFileInfo(root.filePath(name)).exists();
    };

    if (exists(QStringLiteral("ngksgraph.toml"))) {
        return QStringLiteral("NGKsGraph / Qt C++ (Vexara-style)");
    }
    if (exists(QStringLiteral("CMakeLists.txt"))) {
        return QStringLiteral("CMake C/C++");
    }
    if (exists(QStringLiteral("package.json"))) {
        return QStringLiteral("Node.js / JavaScript");
    }
    if (exists(QStringLiteral("pyproject.toml")) || exists(QStringLiteral("requirements.txt"))) {
        return QStringLiteral("Python");
    }
    if (exists(QStringLiteral("Cargo.toml"))) {
        return QStringLiteral("Rust");
    }
    if (exists(QStringLiteral("go.mod"))) {
        return QStringLiteral("Go");
    }
    if (root.entryList(QStringList{QStringLiteral("*.sln")}, QDir::Files).isEmpty() == false) {
        return QStringLiteral("Visual Studio / MSBuild");
    }
    return QStringLiteral("unknown");
}

} // namespace VexaraCore
