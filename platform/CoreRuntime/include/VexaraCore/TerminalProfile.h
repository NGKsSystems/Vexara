#pragma once

#include <QString>
#include <QStringList>

namespace VexaraCore {

struct TerminalProfile {
    QString id;
    QString displayName;
    QString program;
    QStringList args;
};

} // namespace VexaraCore
