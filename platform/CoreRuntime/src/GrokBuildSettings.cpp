#include "VexaraCore/GrokBuildSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
namespace VexaraCore {

QString GrokBuildSettings::defaultInstallPath()
{
    const QString candidate =
        QDir::homePath() + QStringLiteral("/.grok/bin/grok.exe");
    if (QFileInfo::exists(candidate)) {
        return QFileInfo(candidate).absoluteFilePath();
    }
    return QString();
}

void GrokBuildSettings::ensureDefaults()
{
    if (command.trimmed().isEmpty()) {
        command = defaultInstallPath();
    }
    if (args.isEmpty() && isConfigured()) {
        args = {QStringLiteral("-p"), QStringLiteral("{prompt}")};
    }
    if (promptTemplate.trimmed().isEmpty()) {
        promptTemplate = defaultGrokPromptTemplate();
    }
}

QString GrokBuildSettings::composePrompt(const QString& userTask, const GrokTaskContext& context) const
{
    QString templateText = promptTemplate.trimmed().isEmpty() ? defaultGrokPromptTemplate()
                                                              : promptTemplate;

    const QString projectPath = context.projectPath.trimmed().isEmpty()
                                    ? QStringLiteral("(no project open)")
                                    : context.projectPath;
    const QString projectType = context.detectedProjectType.trimmed().isEmpty()
                                    ? detectProjectType(context.projectPath)
                                    : context.detectedProjectType;
    const QString filePath = context.currentFilePath.trimmed().isEmpty()
                                 ? QStringLiteral("(none)")
                                 : context.currentFilePath;
    const QString selection = context.selectedText.trimmed().isEmpty() ? QStringLiteral("none")
                                                                       : context.selectedText;

    auto replaceAll = [&templateText](const QString& token, const QString& value) {
        templateText.replace(token, value);
    };

    replaceAll(QStringLiteral("{current_project_path}"), projectPath);
    replaceAll(QStringLiteral("{cwd}"), projectPath);
    replaceAll(QStringLiteral("{detected_type}"), projectType);
    replaceAll(QStringLiteral("{current_file_path}"), filePath);
    replaceAll(QStringLiteral("{selected_text}"), selection);
    replaceAll(QStringLiteral("{prompt}"), userTask);
    replaceAll(QStringLiteral("{task}"), userTask);
    replaceAll(QStringLiteral("{user_task}"), userTask);

    return templateText;
}

bool GrokBuildSettings::isConfigured() const
{
    return !command.trimmed().isEmpty();
}

bool GrokBuildSettings::loadFromJson(const QJsonObject& root)
{
    const QJsonObject grok = root.value(QStringLiteral("grok_build")).toObject();
    command = grok.value(QStringLiteral("command")).toString();
    promptTemplate = grok.value(QStringLiteral("prompt_template")).toString();
    timeoutMs = grok.value(QStringLiteral("timeout_ms")).toInt(600000);

    args.clear();
    const QJsonArray argArray = grok.value(QStringLiteral("args")).toArray();
    for (const QJsonValue& value : argArray) {
        args.append(value.toString());
    }
    return true;
}

void GrokBuildSettings::saveToJson(QJsonObject& root) const
{
    QJsonObject grok;
    grok.insert(QStringLiteral("command"), command);
    if (!promptTemplate.isEmpty()) {
        grok.insert(QStringLiteral("prompt_template"), promptTemplate);
    }
    grok.insert(QStringLiteral("timeout_ms"), timeoutMs);
    QJsonArray argArray;
    for (const QString& arg : args) {
        argArray.append(arg);
    }
    grok.insert(QStringLiteral("args"), argArray);
    root.insert(QStringLiteral("grok_build"), grok);
}

} // namespace VexaraCore
