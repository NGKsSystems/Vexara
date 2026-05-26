#include "VexaraCore/AiderSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcessEnvironment>

namespace VexaraCore {

namespace {

QStringList defaultAiderArgs()
{
    return {QStringLiteral("--model"),
            QStringLiteral("{model}"),
            QStringLiteral("--yes"),
            QStringLiteral("--skip-sanity-check-repo"),
            QStringLiteral("--message"),
            QStringLiteral("{prompt}"),
            QStringLiteral("--exit"),
            QStringLiteral("--no-check-update"),
            QStringLiteral("--no-analytics"),
            QStringLiteral("--no-show-release-notes"),
            QStringLiteral("--no-show-model-warnings"),
            QStringLiteral("--no-browser"),
            QStringLiteral("--no-auto-commits"),
            QStringLiteral("--no-git"),
            QStringLiteral("--subtree-only"),
            QStringLiteral("--map-refresh"),
            QStringLiteral("manual"),
            QStringLiteral("--edit-format"),
            QStringLiteral("diff"),
            QStringLiteral("--no-stream"),
            QStringLiteral("--map-tokens"),
            QStringLiteral("0")};
}

QStringList recommendedAiderSafetyFlags()
{
    return {QStringLiteral("--exit"),
            QStringLiteral("--no-check-update"),
            QStringLiteral("--no-analytics"),
            QStringLiteral("--no-show-release-notes"),
            QStringLiteral("--no-show-model-warnings"),
            QStringLiteral("--no-browser"),
            QStringLiteral("--no-auto-commits"),
            QStringLiteral("--no-git"),
            QStringLiteral("--subtree-only"),
            QStringLiteral("--skip-sanity-check-repo")};
}

bool argsIncludePrompt(const QStringList& args)
{
    for (const QString& arg : args) {
        if (arg.contains(QStringLiteral("{prompt}"))) {
            return true;
        }
    }
    return false;
}

bool argsIncludeFlag(const QStringList& args, const QString& flag)
{
    return args.contains(flag);
}

void appendFlagIfMissing(QStringList& args, const QString& flag)
{
    if (!argsIncludeFlag(args, flag)) {
        args.append(flag);
    }
}

void dedupeFlagArgs(QStringList& args)
{
    QStringList deduped;
    deduped.reserve(args.size());
    for (const QString& arg : args) {
        if (arg.startsWith(QStringLiteral("--")) && deduped.contains(arg)) {
            continue;
        }
        deduped.append(arg);
    }
    args = deduped;
}

QString stripWrappingQuotes(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2) {
        const QChar first = value.front();
        const QChar last = value.back();
        if ((first == QLatin1Char('"') && last == QLatin1Char('"'))
            || (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
            return value.mid(1, value.size() - 2);
        }
    }
    return value;
}

bool isMessageFlag(const QString& arg)
{
    return arg == QStringLiteral("--message") || arg == QStringLiteral("-m");
}

void appendTargetFileArguments(QStringList& arguments,
                               const QStringList& targetFiles,
                               const QString& workingDirectory)
{
    if (targetFiles.isEmpty()) {
        return;
    }

    const QDir projectDir(workingDirectory);
    for (const QString& candidate : targetFiles) {
        const QString trimmed = candidate.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        const QFileInfo fileInfo(projectDir.absoluteFilePath(trimmed));
        if (!fileInfo.isFile()) {
            continue;
        }

        const QString relativePath = projectDir.relativeFilePath(fileInfo.absoluteFilePath());
        arguments.append(relativePath);
    }
}

void appendFlagValueIfMissing(QStringList& args, const QString& flag, const QString& value)
{
    const int flagIndex = args.indexOf(flag);
    if (flagIndex >= 0) {
        if (flagIndex + 1 < args.size() && !args.at(flagIndex + 1).startsWith(QLatin1Char('-'))) {
            return;
        }
        args.insert(flagIndex + 1, value);
        return;
    }
    args.append(flag);
    args.append(value);
}

} // namespace

void AiderSettings::normalizeCommand()
{
    command = command.trimmed();
}

void AiderSettings::normalizeArgs()
{
    for (int i = 0; i < args.size(); ++i) {
        QString& arg = args[i];
        arg = stripWrappingQuotes(arg);

        if (arg == QStringLiteral("--no-sanity-checks")) {
            arg = QStringLiteral("--skip-sanity-check-repo");
            continue;
        }

        if (arg.contains(QStringLiteral("browsernalytics"), Qt::CaseInsensitive)) {
            args.removeAt(i);
            --i;
            continue;
        }
    }

    dedupeFlagArgs(args);
}

QString AiderSettings::resolvedCommand() const
{
    return command.trimmed();
}

QString AiderSettings::resolvedOllamaBaseUrl() const
{
    const QString trimmed = ollamaBaseUrl.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("http://127.0.0.1:11434") : trimmed;
}

bool AiderSettings::usesOllamaModel() const
{
    const QString trimmedModel = model.trimmed();
    return trimmedModel.startsWith(QStringLiteral("ollama/"), Qt::CaseInsensitive)
           || trimmedModel.startsWith(QStringLiteral("ollama:"), Qt::CaseInsensitive);
}

QString AiderSettings::ollamaModelTag() const
{
    QString tag = model.trimmed();
    if (tag.startsWith(QStringLiteral("ollama/"), Qt::CaseInsensitive)) {
        tag = tag.mid(7);
    } else if (tag.startsWith(QStringLiteral("ollama:"), Qt::CaseInsensitive)) {
        tag = tag.mid(6);
    } else if (tag.startsWith(QStringLiteral("ollama_chat/"), Qt::CaseInsensitive)) {
        tag = tag.mid(12);
    } else if (tag.startsWith(QStringLiteral("ollama_chat:"), Qt::CaseInsensitive)) {
        tag = tag.mid(12);
    }
    return tag.trimmed();
}

QString AiderSettings::modelForAiderCli() const
{
    QString resolved = model.trimmed();
    if (resolved.startsWith(QStringLiteral("ollama/"), Qt::CaseInsensitive)
        && !resolved.startsWith(QStringLiteral("ollama_chat/"), Qt::CaseInsensitive)) {
        resolved = QStringLiteral("ollama_chat/") + resolved.mid(7);
    } else if (resolved.startsWith(QStringLiteral("ollama:"), Qt::CaseInsensitive)
               && !resolved.startsWith(QStringLiteral("ollama_chat:"), Qt::CaseInsensitive)) {
        resolved = QStringLiteral("ollama_chat:") + resolved.mid(6);
    }
    return resolved;
}

bool AiderSettings::prepareOllamaCpuConfigFiles(const QString& workingDirectory,
                                                QString* settingsPathOut,
                                                QString* metadataPathOut) const
{
    if (!settingsPathOut || !metadataPathOut) {
        return false;
    }

    const QFileInfo workingDirInfo(workingDirectory.trimmed());
    if (!workingDirInfo.isDir()) {
        return false;
    }

    QDir configDir(
        QDir(workingDirInfo.absoluteFilePath()).absoluteFilePath(QStringLiteral(".vexara/pipeline/aider")));
    if (!configDir.exists() && !QDir().mkpath(configDir.absolutePath())) {
        return false;
    }

    const QString settingsPath =
        configDir.absoluteFilePath(QStringLiteral("ollama-cpu.model.settings.yml"));
    const QString metadataPath =
        configDir.absoluteFilePath(QStringLiteral("ollama-cpu.model.metadata.json"));

    const QString modelName = modelForAiderCli();
    const QString legacyName = model.trimmed();

    const QString settingsYaml = QStringLiteral(
        "- name: aider/extra_params\n"
        "  extra_params:\n"
        "    num_ctx: 8192\n"
        "    num_predict: 768\n"
        "    temperature: 0.2\n"
        "- name: %1\n"
        "  edit_format: diff\n"
        "  use_repo_map: false\n"
        "  streaming: false\n"
        "- name: %2\n"
        "  edit_format: diff\n"
        "  use_repo_map: false\n"
        "  streaming: false\n")
                                     .arg(modelName, legacyName);

    const QString metadataJson = QStringLiteral(
        "{\n"
        "  \"%1\": {\n"
        "    \"max_input_tokens\": 8192,\n"
        "    \"max_output_tokens\": 768,\n"
        "    \"max_tokens\": 768,\n"
        "    \"litellm_provider\": \"ollama\",\n"
        "    \"mode\": \"chat\"\n"
        "  },\n"
        "  \"%2\": {\n"
        "    \"max_input_tokens\": 8192,\n"
        "    \"max_output_tokens\": 768,\n"
        "    \"max_tokens\": 768,\n"
        "    \"litellm_provider\": \"ollama\",\n"
        "    \"mode\": \"chat\"\n"
        "  }\n"
        "}\n")
                                     .arg(modelName, legacyName);

    QFile settingsFile(settingsPath);
    if (!settingsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    settingsFile.write(settingsYaml.toUtf8());
    settingsFile.close();

    QFile metadataFile(metadataPath);
    if (!metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    metadataFile.write(metadataJson.toUtf8());
    metadataFile.close();

    *settingsPathOut = settingsPath;
    *metadataPathOut = metadataPath;
    return true;
}

QProcessEnvironment AiderSettings::processEnvironment() const
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    environment.insert(QStringLiteral("AIDER_STREAM"), QStringLiteral("false"));
    if (!usesOllamaModel()) {
        return environment;
    }

    environment.insert(QStringLiteral("OLLAMA_API_KEY"), ollamaApiKey.trimmed());
    environment.insert(QStringLiteral("OLLAMA_NUM_PARALLEL"), QStringLiteral("1"));
    environment.insert(QStringLiteral("AIDER_TIMEOUT"), QString::number(qBound(60, llmTimeoutSec, 3600)));
    const QString host = resolvedOllamaBaseUrl();
    environment.insert(QStringLiteral("OLLAMA_HOST"), host);
    environment.insert(QStringLiteral("OLLAMA_API_BASE"), host);
    return environment;
}

QStringList AiderSettings::parseArgsField(const QString& text)
{
    QStringList result;
    QString current;
    QChar quoteChar;
    bool inQuote = false;

    for (const QChar ch : text) {
        if (inQuote) {
            if (ch == quoteChar) {
                inQuote = false;
            } else {
                current.append(ch);
            }
            continue;
        }

        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            inQuote = true;
            quoteChar = ch;
            continue;
        }

        if (ch.isSpace()) {
            if (!current.isEmpty()) {
                result.append(current);
                current.clear();
            }
            continue;
        }

        current.append(ch);
    }

    if (!current.isEmpty()) {
        result.append(current);
    }

    return result;
}

QStringList AiderSettings::resolveArguments(const QString& prompt,
                                            const QString& workingDirectory,
                                            const QStringList& targetFiles) const
{
    AiderSettings resolved = *this;
    resolved.ensureDefaults();

    const QString trimmedPrompt = prompt.trimmed();
    const QString resolvedModel =
        resolved.usesOllamaModel() ? resolved.modelForAiderCli() : resolved.model.trimmed();
    const QString resolvedCwd = QFileInfo(workingDirectory.trimmed()).absoluteFilePath();

    QStringList arguments;
    arguments.reserve(resolved.args.size() + targetFiles.size() * 2);

    bool argsReferenceFiles = false;
    for (const QString& configuredArg : resolved.args) {
        if (configuredArg.contains(QStringLiteral("{files}"))) {
            argsReferenceFiles = true;
            break;
        }
    }

    for (QString arg : resolved.args) {
        arg = stripWrappingQuotes(arg);
        if (arg == QStringLiteral("{files}")) {
            appendTargetFileArguments(arguments, targetFiles, resolvedCwd);
            continue;
        }

        arg.replace(QStringLiteral("{prompt}"), trimmedPrompt);
        arg.replace(QStringLiteral("{model}"), resolvedModel);
        arg.replace(QStringLiteral("{cwd}"), resolvedCwd);
        if (arg.contains(QStringLiteral("{files}"))) {
            arg.remove(QStringLiteral("{files}"));
            arg = arg.trimmed();
            if (!arg.isEmpty()) {
                arguments.append(arg);
            }
            appendTargetFileArguments(arguments, targetFiles, resolvedCwd);
            continue;
        }
        arguments.append(arg);
    }

    for (int i = 0; i < arguments.size(); ++i) {
        if (!isMessageFlag(arguments.at(i))) {
            continue;
        }

        const bool missingValue = (i + 1 >= arguments.size())
                                  || arguments.at(i + 1).isEmpty()
                                  || arguments.at(i + 1).startsWith(QLatin1Char('-'));
        if (missingValue) {
            arguments.insert(i + 1, trimmedPrompt);
        }
        break;
    }

    if (!argsReferenceFiles) {
        appendTargetFileArguments(arguments, targetFiles, resolvedCwd);
    }

    if (resolved.usesOllamaModel()) {
        QString settingsPath;
        QString metadataPath;
        if (resolved.prepareOllamaCpuConfigFiles(resolvedCwd, &settingsPath, &metadataPath)) {
            appendFlagValueIfMissing(arguments,
                                     QStringLiteral("--model-settings-file"),
                                     settingsPath);
            appendFlagValueIfMissing(arguments,
                                     QStringLiteral("--model-metadata-file"),
                                     metadataPath);
        }
    }

    return arguments;
}

void AiderSettings::ensureDefaults()
{
    normalizeCommand();
    normalizeArgs();

    if (model.trimmed().isEmpty()) {
        model = QStringLiteral("ollama/qwen2.5-coder:32b");
    }
    if (usesOllamaModel() && llmTimeoutSec > 300) {
        llmTimeoutSec = 180;
    }
    if (args.isEmpty() && !command.isEmpty()) {
        args = defaultAiderArgs();
    } else if (!args.isEmpty() && !argsIncludePrompt(args)) {
        args.append(QStringLiteral("--message"));
        args.append(QStringLiteral("{prompt}"));
    }

    if (!command.isEmpty()) {
        for (const QString& flag : recommendedAiderSafetyFlags()) {
            appendFlagIfMissing(args, flag);
        }
        appendFlagValueIfMissing(args, QStringLiteral("--map-refresh"), QStringLiteral("manual"));
        appendFlagValueIfMissing(args, QStringLiteral("--edit-format"), QStringLiteral("diff"));
        appendFlagIfMissing(args, QStringLiteral("--no-stream"));
        appendFlagValueIfMissing(args, QStringLiteral("--map-tokens"), QStringLiteral("0"));
        dedupeFlagArgs(args);
    }
}

bool AiderSettings::isConfigured() const
{
    const QString executable = resolvedCommand();
    if (executable.isEmpty()) {
        return false;
    }
    if (executable.compare(QStringLiteral("aider"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    return QFileInfo::exists(executable) && !model.trimmed().isEmpty();
}

bool AiderSettings::loadFromJson(const QJsonObject& root)
{
    const QJsonObject aider = root.value(QStringLiteral("aider")).toObject();

    QString loadedCommand = aider.value(QStringLiteral("command")).toString().trimmed();
    if (loadedCommand.isEmpty()) {
        loadedCommand = aider.value(QStringLiteral("executable")).toString().trimmed();
    }
    command = loadedCommand;

    model = aider.value(QStringLiteral("model")).toString(model);
    ollamaApiKey = aider.value(QStringLiteral("ollama_api_key")).toString(ollamaApiKey);
    ollamaBaseUrl = aider.value(QStringLiteral("ollama_base_url")).toString(ollamaBaseUrl);
    timeoutMs = aider.value(QStringLiteral("timeout_ms")).toInt(600000);
    llmTimeoutSec = aider.value(QStringLiteral("llm_timeout_sec")).toInt(llmTimeoutSec);

    args.clear();
    const QJsonValue argsValue = aider.value(QStringLiteral("args"));
    if (argsValue.isArray()) {
        const QJsonArray argArray = argsValue.toArray();
        for (const QJsonValue& value : argArray) {
            args.append(value.toString());
        }
    } else if (argsValue.isString()) {
        args = parseArgsField(argsValue.toString());
    }
    ensureDefaults();
    return true;
}

void AiderSettings::saveToJson(QJsonObject& root) const
{
    QJsonObject aider;
    const QString executable = command.trimmed();
    aider.insert(QStringLiteral("command"), executable);
    aider.insert(QStringLiteral("executable"), executable);
    aider.insert(QStringLiteral("model"), model);
    aider.insert(QStringLiteral("ollama_api_key"), ollamaApiKey);
    aider.insert(QStringLiteral("ollama_base_url"), ollamaBaseUrl);
    aider.insert(QStringLiteral("timeout_ms"), timeoutMs);
    aider.insert(QStringLiteral("llm_timeout_sec"), llmTimeoutSec);

    QJsonArray argArray;
    for (const QString& arg : args) {
        argArray.append(arg);
    }
    aider.insert(QStringLiteral("args"), argArray);
    root.insert(QStringLiteral("aider"), aider);
}

} // namespace VexaraCore
