#pragma once

#include <QString>

namespace VexaraEditor {

class LanguageRegistry {
public:
    static QString languageForPath(const QString& filePath);
};

} // namespace VexaraEditor
