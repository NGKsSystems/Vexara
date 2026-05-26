#pragma once

#include <QString>
#include <QStringList>

namespace VexaraCore {

struct GrokTaskContext {
    QString projectPath;
    QString detectedProjectType;
    QString currentFilePath;
    QString selectedText;
    QStringList targetFiles;
};

QString detectProjectType(const QString& projectRoot);
QString defaultGrokPromptTemplate();

} // namespace VexaraCore
