#pragma once

#include "VexaraCore/TerminalProfile.h"

#include <QJsonObject>
#include <QVector>

namespace VexaraCore {

class TerminalSettings {
public:
    QString defaultProfileId;

    QVector<TerminalProfile> profiles() const;
    QVector<TerminalProfile> runnableProfiles() const;
    TerminalProfile profileById(const QString& id) const;
    bool hasProfile(const QString& id) const;

    void setProfiles(const QVector<TerminalProfile>& profiles);
    void setDefaultProfileId(const QString& id);

    void ensureDefaults();
    bool loadFromJson(const QJsonObject& root);
    void saveToJson(QJsonObject& root) const;

private:
    QVector<TerminalProfile> profiles_;
};

} // namespace VexaraCore
