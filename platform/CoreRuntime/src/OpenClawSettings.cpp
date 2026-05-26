#include "VexaraCore/OpenClawSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QProcessEnvironment>

namespace VexaraCore {

namespace {

QString defaultOpenClawPromptTemplate()
{
    return QStringLiteral(
        "You are the OpenClaw planner inside Vexara.\n"
        "Project: {current_project_path}\n"
        "Task: {user_task}\n"
        "Produce a concise execution plan with ordered steps. Do not write code unless asked.");
}

QStringList defaultOpenClawArgs()
{
    return {QStringLiteral("agent"),
            QStringLiteral("--agent"),
            QStringLiteral("{agent_id}"),
            QStringLiteral("--local"),
            QStringLiteral("--model"),
            QStringLiteral("{model}"),
            QStringLiteral("--message"),
            QStringLiteral("{prompt}"),
            QStringLiteral("--json")};
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

bool argsIncludeSessionSelector(const QStringList& args)
{
    for (int i = 0; i < args.size(); ++i) {
        const QString& arg = args.at(i);
        if (arg == QStringLiteral("--agent") || arg == QStringLiteral("--session-id")
            || arg == QStringLiteral("--to") || arg == QStringLiteral("-t")) {
            return true;
        }
    }
    return false;
}

void injectSessionSelector(QStringList& args, const QString& agentId)
{
    if (argsIncludeSessionSelector(args)) {
        return;
    }
    const QString resolvedAgent = agentId.trimmed().isEmpty() ? QStringLiteral("main") : agentId.trimmed();
    QStringList migrated;
    if (!args.isEmpty() && args.first() == QStringLiteral("agent")) {
        migrated.append(args.first());
        migrated.append(QStringLiteral("--agent"));
        migrated.append(resolvedAgent);
        for (int i = 1; i < args.size(); ++i) {
            migrated.append(args.at(i));
        }
    } else {
        migrated.append(QStringLiteral("agent"));
        migrated.append(QStringLiteral("--agent"));
        migrated.append(resolvedAgent);
        migrated.append(args);
    }
    args = migrated;
}

bool runOpenClawCommand(const QString& command,
                        const QStringList& arguments,
                        const QProcessEnvironment& environment,
                        int timeoutMs,
                        QString* combinedOutput)
{
    if (command.trimmed().isEmpty()) {
        return false;
    }

    QProcess process;
    process.setProgram(command);
    process.setArguments(arguments);
    process.setProcessEnvironment(environment);
    process.start();
    if (!process.waitForStarted(5000)) {
        if (combinedOutput) {
            *combinedOutput = QStringLiteral("Failed to start OpenClaw CLI.");
        }
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(3000);
        if (combinedOutput) {
            *combinedOutput = QStringLiteral("OpenClaw CLI timed out.");
        }
        return false;
    }

    const QString stdoutText = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError());
    if (combinedOutput) {
        *combinedOutput = (stdoutText + QStringLiteral("\n") + stderrText).trimmed();
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

} // namespace

QString OpenClawSettings::defaultInstallPath()
{
    QStringList candidates;
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        candidates.append(QDir(localAppData).filePath(QStringLiteral("Volta/bin/openclaw.cmd")));
    }
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        candidates.append(QDir(appData).filePath(QStringLiteral("npm/openclaw.cmd")));
    }
    candidates.append(QDir::homePath() + QStringLiteral("/AppData/Roaming/npm/openclaw.cmd"));
    for (const QString& candidate : candidates) {
        const QString normalized = QFileInfo(candidate).absoluteFilePath();
        if (QFileInfo::exists(normalized)) {
            return normalized;
        }
    }
    return QString();
}

void OpenClawSettings::normalizeArgs()
{
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--no-sanity-checks")) {
            args[i] = QStringLiteral("--skip-sanity-check-repo");
        }
    }
}

void OpenClawSettings::ensureDefaults()
{
    if (command.trimmed().isEmpty()) {
        command = defaultInstallPath();
    }
    if (agentId.trimmed().isEmpty()) {
        agentId = QStringLiteral("main");
    }
    if (model.trimmed().isEmpty()) {
        model = QStringLiteral("ollama/qwen2.5-coder:14b");
    }
    if (ollamaApiKey.trimmed().isEmpty()) {
        ollamaApiKey = QStringLiteral("ollama-local");
    }
    if (ollamaBaseUrl.trimmed().isEmpty()) {
        ollamaBaseUrl = QStringLiteral("http://127.0.0.1:11434");
    }
    if (args.isEmpty() && isConfigured()) {
        args = defaultOpenClawArgs();
    }
    normalizeArgs();
    if (!args.isEmpty()) {
        injectSessionSelector(args, agentId);
        if (usesLocalOllama()) {
            appendFlagIfMissing(args, QStringLiteral("--local"));
            appendFlagValueIfMissing(args, QStringLiteral("--model"), model.trimmed());
        }
    }
    if (promptTemplate.trimmed().isEmpty()) {
        promptTemplate = defaultOpenClawPromptTemplate();
    }
}

bool OpenClawSettings::usesLocalOllama() const
{
    if (!localOllamaMode) {
        return false;
    }
    const QString trimmedModel = model.trimmed().toLower();
    return trimmedModel.startsWith(QStringLiteral("ollama/"))
           || trimmedModel.startsWith(QStringLiteral("ollama:"));
}

QStringList OpenClawSettings::parseArgsField(const QString& text)
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

QString OpenClawSettings::composePrompt(const QString& userTask, const QString& projectPath) const
{
    QString templateText =
        promptTemplate.trimmed().isEmpty() ? defaultOpenClawPromptTemplate() : promptTemplate;

    const QString resolvedProject = projectPath.trimmed().isEmpty()
                                        ? QStringLiteral("(no project open)")
                                        : projectPath;

    templateText.replace(QStringLiteral("{current_project_path}"), resolvedProject);
    templateText.replace(QStringLiteral("{cwd}"), resolvedProject);
    templateText.replace(QStringLiteral("{prompt}"), userTask);
    templateText.replace(QStringLiteral("{task}"), userTask);
    templateText.replace(QStringLiteral("{user_task}"), userTask);
    return templateText;
}

QStringList OpenClawSettings::defaultOllamaModelPresets()
{
    return {QStringLiteral("ollama/qwen2.5-coder:7b"),
            QStringLiteral("ollama/qwen2.5-coder:14b"),
            QStringLiteral("ollama/qwen2.5-coder:32b"),
            QStringLiteral("ollama/deepseek-coder-v2"),
            QStringLiteral("ollama/deepseek-coder-v2:16b"),
            QStringLiteral("ollama/deepseek-r1:14b"),
            QStringLiteral("ollama/codellama"),
            QStringLiteral("ollama/codellama:13b"),
            QStringLiteral("ollama/llama3.2"),
            QStringLiteral("ollama/llama3.3"),
            QStringLiteral("ollama/mistral"),
            QStringLiteral("ollama/mixtral"),
            QStringLiteral("ollama/phi3"),
            QStringLiteral("ollama/gemma2"),
            QStringLiteral("ollama/starcoder2")};
}

namespace {

QStringList sortedUniqueIds(const QSet<QString>& ids)
{
    QStringList list(ids.cbegin(), ids.cend());
    list.sort(Qt::CaseInsensitive);
    return list;
}

OpenClawSettings::ModelCatalogResult finalizeCatalog(OpenClawSettings::ModelCatalogResult result)
{
    auto dedupe = [](QStringList& list) {
        QSet<QString> seen;
        QStringList unique;
        for (const QString& id : list) {
            if (seen.contains(id)) {
                continue;
            }
            seen.insert(id);
            unique.append(id);
        }
        list = unique;
        list.sort(Qt::CaseInsensitive);
    };

    dedupe(result.installedIds);
    dedupe(result.configuredIds);
    dedupe(result.suggestedIds);

    QSet<QString> seen;
    QStringList merged;
    const auto appendUnique = [&seen, &merged](const QStringList& ids) {
        for (const QString& id : ids) {
            if (seen.contains(id)) {
                continue;
            }
            seen.insert(id);
            merged.append(id);
        }
    };
    appendUnique(result.configuredIds);
    appendUnique(result.installedIds);
    appendUnique(result.suggestedIds);
    merged.sort(Qt::CaseInsensitive);
    result.modelIds = merged;
    return result;
}

QStringList parseOpenClawListOutput(const QByteArray& output)
{
    QStringList ids;
    const QString text = QString::fromUtf8(output).trimmed();
    if (text.isEmpty() || text == QStringLiteral("No models found.")) {
        return ids;
    }

    const QJsonDocument jsonDoc = QJsonDocument::fromJson(output);
    if (jsonDoc.isArray()) {
        for (const QJsonValue& entry : jsonDoc.array()) {
            if (entry.isString()) {
                ids.append(entry.toString().trimmed());
            } else if (entry.isObject()) {
                const QJsonObject obj = entry.toObject();
                const QString id = obj.value(QStringLiteral("id"))
                                       .toString(obj.value(QStringLiteral("model")).toString())
                                       .trimmed();
                if (!id.isEmpty()) {
                    ids.append(id);
                }
            }
        }
        return ids;
    }
    if (jsonDoc.isObject()) {
        const QJsonArray models = jsonDoc.object().value(QStringLiteral("models")).toArray();
        for (const QJsonValue& entry : models) {
            if (entry.isString()) {
                ids.append(entry.toString().trimmed());
            } else if (entry.isObject()) {
                const QString id = entry.toObject()
                                       .value(QStringLiteral("id"))
                                       .toString(entry.toObject().value(QStringLiteral("model")).toString())
                                       .trimmed();
                if (!id.isEmpty()) {
                    ids.append(id);
                }
            }
        }
        if (!ids.isEmpty()) {
            return ids;
        }
    }

    for (const QString& line : text.split(QLatin1Char('\n'))) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (trimmed.contains(QLatin1Char('\t'))) {
            trimmed = trimmed.section(QLatin1Char('\t'), 0, 0).trimmed();
        }
        if (trimmed.contains(QLatin1Char(' '))) {
            const QString firstToken = trimmed.section(QLatin1Char(' '), 0, 0).trimmed();
            if (firstToken.contains(QLatin1Char('/'))) {
                trimmed = firstToken;
            }
        }
        if (trimmed.contains(QLatin1Char('/'))) {
            ids.append(trimmed);
        }
    }
    return ids;
}

} // namespace

QString OpenClawSettings::ollamaModelTagFromId(const QString& modelId)
{
    QString tag = modelId.trimmed();
    if (tag.startsWith(QStringLiteral("ollama/"), Qt::CaseInsensitive)) {
        tag = tag.mid(7);
    } else if (tag.startsWith(QStringLiteral("ollama:"), Qt::CaseInsensitive)) {
        tag = tag.mid(6);
    }
    return tag.trimmed();
}

OpenClawSettings::OllamaRuntimeStatus OpenClawSettings::probeOllamaRuntime(const QString& baseUrl,
                                                                          const QString& modelId)
{
    OllamaRuntimeStatus status;
    const QString trimmedBase = baseUrl.trimmed().isEmpty() ? QStringLiteral("http://127.0.0.1:11434")
                                                            : baseUrl.trimmed();
    const QString modelTag = ollamaModelTagFromId(modelId);

    QProcess probe;
    probe.setProgram(QStringLiteral("powershell"));
    probe.setArguments(
        {QStringLiteral("-NoProfile"),
         QStringLiteral("-Command"),
         QStringLiteral("try { (Invoke-WebRequest -Uri '%1/api/ps' -UseBasicParsing -TimeoutSec 5).Content } catch { '' }")
             .arg(trimmedBase)});
    probe.start();
    if (!probe.waitForFinished(8000) || probe.exitCode() != 0) {
        status.detail =
            QStringLiteral("Ollama not reachable at %1 (is the Ollama app running?)").arg(trimmedBase);
        return status;
    }

    status.reachable = true;
    const QJsonDocument doc =
        QJsonDocument::fromJson(QString::fromLocal8Bit(probe.readAllStandardOutput()).trimmed().toUtf8());
    const QJsonArray models = doc.object().value(QStringLiteral("models")).toArray();
    for (const QJsonValue& entry : models) {
        const QJsonObject modelObject = entry.toObject();
        const QString name = modelObject.value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty() && name.compare(modelTag, Qt::CaseInsensitive) != 0) {
            continue;
        }
        status.modelLoaded = true;
        status.vramBytes = static_cast<qint64>(modelObject.value(QStringLiteral("size_vram")).toDouble());
        break;
    }

    if (!status.modelLoaded) {
        status.detail =
            QStringLiteral("Ollama is up at %1 but %2 is not loaded yet (first request will load it).")
                .arg(trimmedBase, modelTag.isEmpty() ? modelId.trimmed() : modelTag);
        return status;
    }

    if (status.vramBytes <= 0) {
        status.detail =
            QStringLiteral(
                "Ollama loaded %1 on CPU (no GPU). Vexara caps context for Aider; simple edits "
                "should take 1–3 minutes, not 10+. If it hangs longer, stop stuck aider/python "
                "tasks or restart Ollama.")
                .arg(modelTag);
    } else {
        const double vramGb = static_cast<double>(status.vramBytes) / (1024.0 * 1024.0 * 1024.0);
        status.detail = QStringLiteral("Ollama has %1 on GPU (~%2 GB VRAM).")
                            .arg(modelTag)
                            .arg(QString::number(vramGb, 'f', 1));
    }
    return status;
}

bool OpenClawSettings::warmOllamaModel(const QString& baseUrl,
                                       const QString& modelId,
                                       QString* detail,
                                       int timeoutSec)
{
    const QString trimmedBase = baseUrl.trimmed().isEmpty() ? QStringLiteral("http://127.0.0.1:11434")
                                                            : baseUrl.trimmed();
    const QString modelTag = ollamaModelTagFromId(modelId);
    if (modelTag.isEmpty()) {
        if (detail) {
            *detail = QStringLiteral("Ollama model id is empty.");
        }
        return false;
    }

    const int boundedTimeout = qBound(15, timeoutSec, 300);
    QProcess warm;
    warm.setProgram(QStringLiteral("powershell"));
    warm.setArguments(
        {QStringLiteral("-NoProfile"),
         QStringLiteral("-Command"),
         QStringLiteral(
             "$body = @{model='%2'; prompt='ok'; stream=$false; options=@{num_predict=1}} | "
             "ConvertTo-Json -Compress; "
             "try { $r = Invoke-RestMethod -Uri '%1/api/generate' -Method Post -Body $body "
             "-ContentType 'application/json' -TimeoutSec %3; if ($r.response) { 'ok' } else { 'empty' } } "
             "catch { $_.Exception.Message }")
             .arg(trimmedBase, modelTag)
             .arg(boundedTimeout)});
    warm.start();
    if (!warm.waitForFinished((boundedTimeout + 15) * 1000) || warm.exitCode() != 0) {
        if (detail) {
            *detail = QStringLiteral("Ollama warmup timed out after %1s at %2.")
                          .arg(boundedTimeout)
                          .arg(trimmedBase);
        }
        return false;
    }

    const QString output = QString::fromLocal8Bit(warm.readAllStandardOutput()).trimmed();
    if (output.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
        if (detail) {
            *detail = output.isEmpty()
                          ? QStringLiteral("Ollama warmup returned no response.")
                          : QStringLiteral("Ollama warmup failed: %1").arg(output.left(400));
        }
        return false;
    }

    if (detail) {
        *detail = QStringLiteral("Model %1 responded at %2.").arg(modelTag, trimmedBase);
    }
    return true;
}

OpenClawSettings::ModelCatalogResult OpenClawSettings::discoverOllamaInstalledModels(
    const QString& baseUrl)
{
    ModelCatalogResult result;
    const QString trimmedBase = baseUrl.trimmed().isEmpty() ? QStringLiteral("http://127.0.0.1:11434")
                                                            : baseUrl.trimmed();

    QProcess probe;
    probe.setProgram(QStringLiteral("powershell"));
    probe.setArguments(
        {QStringLiteral("-NoProfile"),
         QStringLiteral("-Command"),
         QStringLiteral("try { (Invoke-WebRequest -Uri '%1/api/tags' -UseBasicParsing -TimeoutSec 8).Content } catch { '' }")
             .arg(trimmedBase)});
    probe.start();
    if (!probe.waitForFinished(8000) || probe.exitCode() != 0) {
        result.detail = QStringLiteral("Ollama not reachable at %1").arg(trimmedBase);
        return result;
    }

    const QJsonDocument doc =
        QJsonDocument::fromJson(QString::fromLocal8Bit(probe.readAllStandardOutput()).trimmed().toUtf8());
    const QJsonArray models = doc.object().value(QStringLiteral("models")).toArray();
    QSet<QString> installed;
    for (const QJsonValue& entry : models) {
        const QString name = entry.toObject().value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) {
            continue;
        }
        const QString modelId =
            name.startsWith(QStringLiteral("ollama/"), Qt::CaseInsensitive)
                ? name
                : QStringLiteral("ollama/%1").arg(name);
        installed.insert(modelId);
    }

    result.installedIds = sortedUniqueIds(installed);
    result.detail = result.installedIds.isEmpty()
                        ? QStringLiteral("No models installed in Ollama at %1").arg(trimmedBase)
                        : QStringLiteral("%1 installed in Ollama").arg(result.installedIds.size());
    return finalizeCatalog(result);
}

OpenClawSettings::ModelCatalogResult OpenClawSettings::discoverOllamaPickerModels(const QString& baseUrl)
{
    ModelCatalogResult result = discoverOllamaInstalledModels(baseUrl);
    QSet<QString> installed(result.installedIds.cbegin(), result.installedIds.cend());
    QSet<QString> suggested;
    for (const QString& preset : defaultOllamaModelPresets()) {
        if (!installed.contains(preset)) {
            suggested.insert(preset);
        }
    }
    result.suggestedIds = sortedUniqueIds(suggested);
    result.detail = QStringLiteral("%1 installed · %2 suggested (ollama pull …)")
                        .arg(result.installedIds.size())
                        .arg(result.suggestedIds.size());
    return finalizeCatalog(result);
}

OpenClawSettings::ModelCatalogResult OpenClawSettings::discoverOllamaModels(const QString& baseUrl)
{
    return discoverOllamaPickerModels(baseUrl);
}

OpenClawSettings::ModelCatalogResult OpenClawSettings::discoverOpenClawPickerModels(
    bool queryOpenClawCli) const
{
    ModelCatalogResult result;
    QSet<QString> configured;
    QSet<QString> installed;
    QSet<QString> suggested;
    QSet<QString> catalog;

    const auto markConfigured = [&configured](const QStringList& ids) {
        for (const QString& id : ids) {
            const QString trimmed = id.trimmed();
            if (!trimmed.isEmpty()) {
                configured.insert(trimmed);
            }
        }
    };

    if (queryOpenClawCli && isConfigured()) {
        QProcess statusProc;
        statusProc.setProgram(command.trimmed());
        statusProc.setArguments({QStringLiteral("models"),
                                 QStringLiteral("status"),
                                 QStringLiteral("--json")});
        statusProc.setProcessEnvironment(processEnvironment());
        statusProc.start();
        if (statusProc.waitForFinished(20000) && statusProc.exitCode() == 0) {
            const QJsonDocument doc = QJsonDocument::fromJson(statusProc.readAllStandardOutput());
            const QJsonObject root = doc.object();
            markConfigured({root.value(QStringLiteral("defaultModel")).toString()});
            const QJsonArray allowed = root.value(QStringLiteral("allowed")).toArray();
            QStringList allowedIds;
            for (const QJsonValue& entry : allowed) {
                allowedIds.append(entry.toString());
            }
            markConfigured(allowedIds);
            const QJsonArray fallbacks = root.value(QStringLiteral("fallbacks")).toArray();
            QStringList fallbackIds;
            for (const QJsonValue& entry : fallbacks) {
                fallbackIds.append(entry.toString());
            }
            markConfigured(fallbackIds);
        }

        QProcess listProc;
        listProc.setProgram(command.trimmed());
        listProc.setArguments({QStringLiteral("models"),
                               QStringLiteral("list"),
                               QStringLiteral("--all"),
                               QStringLiteral("--plain")});
        listProc.setProcessEnvironment(processEnvironment());
        listProc.start();
        if (listProc.waitForFinished(15000) && listProc.exitCode() == 0) {
            for (const QString& id : parseOpenClawListOutput(listProc.readAllStandardOutput())) {
                catalog.insert(id);
            }
        }
    }

    const ModelCatalogResult ollama = discoverOllamaPickerModels(ollamaBaseUrl);
    for (const QString& id : ollama.installedIds) {
        installed.insert(id);
    }
    for (const QString& id : ollama.suggestedIds) {
        suggested.insert(id);
    }

    for (const QString& preset : defaultOllamaModelPresets()) {
        if (!configured.contains(preset) && !installed.contains(preset)) {
            suggested.insert(preset);
        }
    }
    for (const QString& id : catalog) {
        if (!configured.contains(id) && !installed.contains(id)) {
            suggested.insert(id);
        }
    }

    if (!model.trimmed().isEmpty()) {
        configured.insert(model.trimmed());
    }

    result.configuredIds = sortedUniqueIds(configured);
    result.installedIds = sortedUniqueIds(installed);
    result.suggestedIds = sortedUniqueIds(suggested);
    result = finalizeCatalog(result);
    result.detail =
        QStringLiteral("%1 configured · %2 installed · %3 available")
            .arg(result.configuredIds.size())
            .arg(result.installedIds.size())
            .arg(result.suggestedIds.size());
    if (result.modelIds.isEmpty()) {
        result.detail = QStringLiteral("Set OpenClaw executable and refresh, or start Ollama");
    }
    return result;
}

OpenClawSettings::ModelCatalogResult OpenClawSettings::discoverAllowedModels() const
{
    return discoverOpenClawPickerModels(true);
}

bool OpenClawSettings::isConfigured() const
{
    return !command.trimmed().isEmpty() && QFileInfo::exists(command.trimmed());
}

bool OpenClawSettings::isLocalOllamaReady(QString* detail) const
{
    if (!isConfigured()) {
        if (detail) {
            *detail = QStringLiteral("OpenClaw executable path is not set or not found.");
        }
        return false;
    }
    if (!usesLocalOllama()) {
        if (detail) {
            *detail = QStringLiteral("Local Ollama mode is disabled.");
        }
        return false;
    }
    if (model.trimmed().isEmpty()) {
        if (detail) {
            *detail = QStringLiteral("Set an Ollama model (e.g. ollama/qwen2.5-coder:14b).");
        }
        return false;
    }

    const QString baseUrl = ollamaBaseUrl.trimmed();
    QProcess probe;
    probe.setProgram(QStringLiteral("powershell"));
    probe.setArguments(
        {QStringLiteral("-NoProfile"),
         QStringLiteral("-Command"),
         QStringLiteral("try { (Invoke-WebRequest -Uri '%1/api/tags' -UseBasicParsing -TimeoutSec 3).StatusCode } catch { 0 }")
             .arg(baseUrl)});
    probe.start();
    bool ollamaReachable = false;
    if (probe.waitForFinished(5000) && probe.exitCode() == 0) {
        const QString statusCode = QString::fromLocal8Bit(probe.readAllStandardOutput()).trimmed();
        ollamaReachable = statusCode == QStringLiteral("200");
    }

    if (!ollamaReachable) {
        if (detail) {
            *detail = QStringLiteral("Ollama is not reachable at %1. Start Ollama, then try again.")
                          .arg(ollamaBaseUrl.trimmed());
        }
        return false;
    }

    if (detail) {
        *detail = QStringLiteral("Ready — local model %1 via Ollama.").arg(model.trimmed());
    }
    return true;
}

QStringList OpenClawSettings::resolveArguments(const QString& prompt,
                                               const QString& workingDirectory) const
{
    OpenClawSettings resolved = *this;
    resolved.ensureDefaults();

    const QString trimmedPrompt = prompt.trimmed();
    const QString resolvedAgent =
        resolved.agentId.trimmed().isEmpty() ? QStringLiteral("main") : resolved.agentId.trimmed();
    const QString resolvedModel = resolved.model.trimmed();
    const QString resolvedCwd = QFileInfo(workingDirectory.trimmed()).absoluteFilePath();

    QStringList arguments;
    arguments.reserve(resolved.args.size());

    for (QString arg : resolved.args) {
        arg.replace(QStringLiteral("{prompt}"), trimmedPrompt);
        arg.replace(QStringLiteral("{cwd}"), resolvedCwd);
        arg.replace(QStringLiteral("{agent_id}"), resolvedAgent);
        arg.replace(QStringLiteral("{model}"), resolvedModel);
        arguments.append(arg);
    }

    for (int i = 0; i < arguments.size(); ++i) {
        if (arguments.at(i) != QStringLiteral("--message") && arguments.at(i) != QStringLiteral("-m")) {
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

    return arguments;
}

QProcessEnvironment OpenClawSettings::processEnvironment() const
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (usesLocalOllama()) {
        environment.insert(QStringLiteral("OLLAMA_API_KEY"), ollamaApiKey.trimmed());
        if (!ollamaBaseUrl.trimmed().isEmpty()) {
            environment.insert(QStringLiteral("OLLAMA_HOST"), ollamaBaseUrl.trimmed());
        }
    }
    return environment;
}

QString OpenClawSettings::friendlyErrorMessage(const QString& rawOutput) const
{
    const QString text = rawOutput.trimmed();
    if (text.contains(QStringLiteral("No API key found for provider 'openai'"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("provider \"openai\""), Qt::CaseInsensitive)) {
        return QStringLiteral(
            "OpenClaw tried to use OpenAI instead of local Ollama. Open Agents → "
            "\"Configure for Ollama\", or set Model to ollama/… in Settings → OpenClaw CLI.");
    }
    if (text.contains(QStringLiteral("OLLAMA_API_KEY"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("requires authentication to be registered as a provider"),
                        Qt::CaseInsensitive)) {
        return QStringLiteral(
            "OpenClaw needs Ollama enabled for local models. Click \"Configure for Ollama\" in "
            "the Agents panel (Vexara sets OLLAMA_API_KEY and the default model automatically).");
    }
    if (text.contains(QStringLiteral("Unknown model"), Qt::CaseInsensitive)
        && text.contains(QStringLiteral("ollama"), Qt::CaseInsensitive)) {
        return QStringLiteral(
            "OpenClaw could not find the Ollama model \"%1\". Run `ollama pull %2`, then try "
            "again.")
            .arg(model.trimmed(),
                 model.trimmed().section(QLatin1Char('/'), 1));
    }
    if (text.contains(QStringLiteral("FailoverError"), Qt::CaseInsensitive)) {
        return QStringLiteral("OpenClaw model error: %1").arg(text.left(500));
    }
    if (text.isEmpty()) {
        return QStringLiteral("OpenClaw failed without output.");
    }
    return text.length() > 600 ? text.left(600) + QStringLiteral("…") : text;
}

OpenClawSettings::LocalSetupResult OpenClawSettings::applyLocalOllamaConfiguration() const
{
    LocalSetupResult result;
    if (!isConfigured()) {
        result.message = QStringLiteral("Set the OpenClaw executable path in Settings first.");
        return result;
    }

    const QProcessEnvironment environment = processEnvironment();
    QString cliOutput;

    const bool modelsSetOk =
        runOpenClawCommand(command,
                           {QStringLiteral("models"), QStringLiteral("set"), model.trimmed()},
                           environment,
                           60000,
                           &cliOutput);

    QString configOutput;
    const bool primarySetOk =
        runOpenClawCommand(command,
                           {QStringLiteral("config"),
                            QStringLiteral("set"),
                            QStringLiteral("agents.defaults.model.primary"),
                            model.trimmed()},
                           environment,
                           60000,
                           &configOutput);

    if (modelsSetOk || primarySetOk) {
        result.success = true;
        result.message =
            QStringLiteral("OpenClaw configured for local Ollama (%1). Planner roles can run without OpenAI keys.")
                .arg(model.trimmed());
        return result;
    }

    result.message = friendlyErrorMessage(cliOutput + QStringLiteral("\n") + configOutput);
    if (result.message.isEmpty()) {
        result.message = QStringLiteral("Failed to configure OpenClaw for Ollama.");
    }
    return result;
}

bool OpenClawSettings::loadFromJson(const QJsonObject& root)
{
    const QJsonObject openclaw = root.value(QStringLiteral("openclaw")).toObject();
    command = openclaw.value(QStringLiteral("command")).toString();
    agentId = openclaw.value(QStringLiteral("agent_id")).toString(agentId);
    model = openclaw.value(QStringLiteral("model")).toString(model);
    ollamaApiKey = openclaw.value(QStringLiteral("ollama_api_key")).toString(ollamaApiKey);
    ollamaBaseUrl = openclaw.value(QStringLiteral("ollama_base_url")).toString(ollamaBaseUrl);
    localOllamaMode = openclaw.value(QStringLiteral("local_ollama_mode")).toBool(localOllamaMode);
    promptTemplate = openclaw.value(QStringLiteral("prompt_template")).toString();
    timeoutMs = openclaw.value(QStringLiteral("timeout_ms")).toInt(600000);

    args.clear();
    const QJsonValue argsValue = openclaw.value(QStringLiteral("args"));
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

void OpenClawSettings::saveToJson(QJsonObject& root) const
{
    QJsonObject openclaw;
    openclaw.insert(QStringLiteral("command"), command);
    openclaw.insert(QStringLiteral("agent_id"), agentId);
    openclaw.insert(QStringLiteral("model"), model);
    openclaw.insert(QStringLiteral("ollama_api_key"), ollamaApiKey);
    openclaw.insert(QStringLiteral("ollama_base_url"), ollamaBaseUrl);
    openclaw.insert(QStringLiteral("local_ollama_mode"), localOllamaMode);
    openclaw.insert(QStringLiteral("prompt_template"), promptTemplate);
    openclaw.insert(QStringLiteral("timeout_ms"), timeoutMs);

    QJsonArray argArray;
    for (const QString& arg : args) {
        argArray.append(arg);
    }
    openclaw.insert(QStringLiteral("args"), argArray);
    root.insert(QStringLiteral("openclaw"), openclaw);
}

} // namespace VexaraCore
