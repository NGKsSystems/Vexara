#include "VexaraCore/OpenRouterSettings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <algorithm>

namespace VexaraCore {

namespace {

double priceFromJson(const QJsonValue& value)
{
    if (value.isDouble()) {
        return value.toDouble();
    }
    const QString text = value.toString().trimmed();
    bool ok = false;
    const double parsed = text.toDouble(&ok);
    return ok ? parsed : 0.0;
}

QString fetchModelsJson(const QString& apiKey)
{
    QString ps = QStringLiteral(
        "try { "
        "$h = @{}; "
        "if ('%1' -ne '') { $h['Authorization'] = 'Bearer %1' } "
        "$r = Invoke-RestMethod -Uri 'https://openrouter.ai/api/v1/models' -Headers $h "
        "-UseBasicParsing -TimeoutSec 20; "
        "$r | ConvertTo-Json -Depth 8 -Compress "
        "} catch { '' }")
                     .arg(QString(apiKey).replace(QLatin1Char('\''), QStringLiteral("''")));

    QProcess proc;
    proc.setProgram(QStringLiteral("powershell"));
    proc.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-Command"), ps});
    proc.start();
    if (!proc.waitForFinished(25000) || proc.exitCode() != 0) {
        return QString();
    }
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

OpenRouterModelEntry entryFromJson(const QJsonObject& object)
{
    OpenRouterModelEntry entry;
    entry.id = object.value(QStringLiteral("id")).toString().trimmed();
    entry.name = object.value(QStringLiteral("name")).toString(entry.id).trimmed();
    const QJsonObject pricing = object.value(QStringLiteral("pricing")).toObject();
    const double promptPerToken = priceFromJson(pricing.value(QStringLiteral("prompt")));
    const double completionPerToken = priceFromJson(pricing.value(QStringLiteral("completion")));
    entry.promptPricePerMillion = promptPerToken * 1000000.0;
    entry.completionPricePerMillion = completionPerToken * 1000000.0;
    entry.isFree = (promptPerToken + completionPerToken) <= 0.0
                   || entry.id.endsWith(QStringLiteral(":free"), Qt::CaseInsensitive);
    return entry;
}

bool betterForRole(const OpenRouterModelEntry& a,
                   const OpenRouterModelEntry& b,
                   OpenRouterRoleUse roleUse)
{
    if (a.isFree != b.isFree) {
        return a.isFree > b.isFree;
    }
    if (a.roleFitScore != b.roleFitScore) {
        return a.roleFitScore > b.roleFitScore;
    }
    const double costA = a.promptPricePerMillion + a.completionPricePerMillion;
    const double costB = b.promptPricePerMillion + b.completionPricePerMillion;
    if (!qFuzzyCompare(costA, costB)) {
        return costA < costB;
    }
    Q_UNUSED(roleUse)
    return a.id < b.id;
}

} // namespace

QStringList OpenRouterSettings::defaultFreeModelPresets()
{
    return {QStringLiteral("google/gemini-2.0-flash-exp:free"),
            QStringLiteral("meta-llama/llama-3.3-70b-instruct:free"),
            QStringLiteral("qwen/qwen-2.5-72b-instruct:free"),
            QStringLiteral("mistralai/mistral-small-3.1-24b-instruct:free"),
            QStringLiteral("deepseek/deepseek-r1-distill-llama-70b:free"),
            QStringLiteral("microsoft/phi-3-medium-128k-instruct:free")};
}

QString OpenRouterSettings::profileIdForModelSlug(const QString& modelSlug)
{
    QString slug = modelSlug.trimmed();
    slug.replace(QLatin1Char('/'), QLatin1Char('-'));
    slug.replace(QLatin1Char(':'), QLatin1Char('-'));
    return QStringLiteral("or-%1").arg(slug);
}

int OpenRouterSettings::roleFitScore(const QString& modelId, OpenRouterRoleUse roleUse)
{
    const QString lower = modelId.toLower();
    int score = 0;

    if (roleUse == OpenRouterRoleUse::Builder) {
        if (lower.contains(QStringLiteral("coder")) || lower.contains(QStringLiteral("code"))) {
            score += 40;
        }
        if (lower.contains(QStringLiteral("deepseek"))) {
            score += 20;
        }
        return score;
    }

    if (lower.contains(QStringLiteral(":free"))) {
        score += 80;
    }
    if (lower.contains(QStringLiteral("gemini")) && lower.contains(QStringLiteral("flash"))) {
        score += 70;
    }
    if (lower.contains(QStringLiteral("llama-3"))) {
        score += 60;
    }
    if (lower.contains(QStringLiteral("mistral"))) {
        score += 55;
    }
    if (lower.contains(QStringLiteral("qwen")) && !lower.contains(QStringLiteral("coder"))) {
        score += 50;
    }
    if (lower.contains(QStringLiteral("deepseek-r1"))) {
        score += 65;
    }
    if (lower.contains(QStringLiteral("gpt-4o")) && !lower.contains(QStringLiteral("mini"))) {
        score += 30;
    }
    if (lower.contains(QStringLiteral("coder")) || lower.contains(QStringLiteral("code"))) {
        score -= 25;
    }
    if (lower.contains(QStringLiteral("vision")) || lower.contains(QStringLiteral("image"))) {
        score -= 15;
    }
    return score;
}

QString OpenRouterSettings::roleRecommendationHint(OpenRouterRoleUse roleUse)
{
    switch (roleUse) {
    case OpenRouterRoleUse::Planner:
        return QStringLiteral(
            "Planner: pick a FREE reasoning model first (Gemini Flash, Llama 3.3, DeepSeek R1). "
            "Avoid coder-tuned models.");
    case OpenRouterRoleUse::Supervisor:
        return QStringLiteral(
            "Supervisor: same as Planner — free JSON/reasoning models. "
            "Paid fallbacks: inexpensive instruct models.");
    case OpenRouterRoleUse::Builder:
        return QStringLiteral(
            "Builder: keep Aider + Ollama for coding. OpenRouter here is optional / not recommended.");
    }
    return QString();
}

QString OpenRouterSettings::displayNameForEntry(const OpenRouterModelEntry& entry)
{
    if (!entry.name.isEmpty() && entry.name != entry.id) {
        return entry.name;
    }
    return entry.id;
}

QString OpenRouterSettings::comboLabelForEntry(const OpenRouterModelEntry& entry,
                                               OpenRouterRoleUse roleUse)
{
    const QString priceTag =
        entry.isFree
            ? QStringLiteral("FREE")
            : QStringLiteral("$%1/M")
                  .arg(QString::number(entry.promptPricePerMillion + entry.completionPricePerMillion,
                                      'f', 2));

    QString fit;
    if (roleUse == OpenRouterRoleUse::Builder) {
        fit = entry.roleFitScore >= 30 ? QStringLiteral("coding") : QStringLiteral("not for build");
    } else {
        fit = entry.roleFitScore >= 50 ? QStringLiteral("plan/review") : QStringLiteral("ok");
    }

    return QStringLiteral("%1 · %2 · %3 — %4")
        .arg(priceTag, fit, entry.id, displayNameForEntry(entry));
}

OpenRouterPickerCatalog OpenRouterSettings::discoverPickerModels(const QString& apiKey)
{
    OpenRouterPickerCatalog catalog;
    QVector<OpenRouterModelEntry> entries;

    const QString jsonText = fetchModelsJson(apiKey.trimmed());
    if (!jsonText.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
        QJsonArray models;
        if (doc.isObject()) {
            models = doc.object().value(QStringLiteral("data")).toArray();
        } else if (doc.isArray()) {
            models = doc.array();
        }
        for (const QJsonValue& value : models) {
            if (!value.isObject()) {
                continue;
            }
            OpenRouterModelEntry entry = entryFromJson(value.toObject());
            if (entry.id.isEmpty()) {
                continue;
            }
            entries.append(entry);
        }
    }

    if (entries.isEmpty()) {
        for (const QString& preset : defaultFreeModelPresets()) {
            OpenRouterModelEntry entry;
            entry.id = preset;
            entry.name = preset;
            entry.isFree = true;
            entries.append(entry);
        }
        catalog.detail = QStringLiteral(
            "Could not reach OpenRouter API — showing built-in free presets. "
            "Add OPENROUTER_API_KEY and click Refresh model lists.");
    } else {
        catalog.detail =
            QStringLiteral("%1 OpenRouter models (free first, then by cost).").arg(entries.size());
    }

    for (OpenRouterModelEntry& entry : entries) {
        entry.roleFitScore = qMax(roleFitScore(entry.id, OpenRouterRoleUse::Planner),
                                  roleFitScore(entry.id, OpenRouterRoleUse::Supervisor));
    }

    std::sort(entries.begin(), entries.end(), [](const OpenRouterModelEntry& a,
                                                 const OpenRouterModelEntry& b) {
        return betterForRole(a, b, OpenRouterRoleUse::Planner);
    });

    catalog.entries = entries;
    for (const OpenRouterModelEntry& entry : entries) {
        catalog.profileIds.append(profileIdForModelSlug(entry.id));
    }
    return catalog;
}

} // namespace VexaraCore
