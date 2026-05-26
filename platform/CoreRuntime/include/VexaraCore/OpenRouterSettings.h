#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace VexaraCore {

enum class OpenRouterRoleUse {
    Planner,
    Supervisor,
    Builder,
};

struct OpenRouterModelEntry {
    QString id;
    QString name;
    double promptPricePerMillion = 0.0;
    double completionPricePerMillion = 0.0;
    bool isFree = false;
    int roleFitScore = 0;
};

struct OpenRouterPickerCatalog {
    QVector<OpenRouterModelEntry> entries;
    QStringList profileIds;
    QString detail;
};

class OpenRouterSettings {
public:
    static OpenRouterPickerCatalog discoverPickerModels(const QString& apiKey = QString());
    static QString profileIdForModelSlug(const QString& modelSlug);
    static QString displayNameForEntry(const OpenRouterModelEntry& entry);
    static QString comboLabelForEntry(const OpenRouterModelEntry& entry, OpenRouterRoleUse roleUse);
    static int roleFitScore(const QString& modelId, OpenRouterRoleUse roleUse);
    static QString roleRecommendationHint(OpenRouterRoleUse roleUse);
    static QStringList defaultFreeModelPresets();
};

} // namespace VexaraCore
