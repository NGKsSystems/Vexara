#include "VexaraEditor/LanguageRegistry.h"

#include <QFileInfo>

namespace VexaraEditor {

QString LanguageRegistry::languageForPath(const QString& filePath)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QStringLiteral("cpp") || ext == QStringLiteral("cc") || ext == QStringLiteral("cxx")
        || ext == QStringLiteral("h") || ext == QStringLiteral("hpp") || ext == QStringLiteral("hxx")) {
        return QStringLiteral("cpp");
    }
    if (ext == QStringLiteral("json")) {
        return QStringLiteral("json");
    }
    if (ext == QStringLiteral("md")) {
        return QStringLiteral("markdown");
    }
    if (ext == QStringLiteral("py")) {
        return QStringLiteral("python");
    }
    if (ext == QStringLiteral("toml")) {
        return QStringLiteral("toml");
    }
    if (ext == QStringLiteral("xml") || ext == QStringLiteral("html") || ext == QStringLiteral("htm")) {
        return QStringLiteral("markup");
    }
    return QStringLiteral("plain");
}

} // namespace VexaraEditor
