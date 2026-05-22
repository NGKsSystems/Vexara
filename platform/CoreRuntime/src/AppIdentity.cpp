#include "VexaraCore/AppIdentity.h"

namespace VexaraCore::AppIdentity {

QString applicationName()
{
    return QStringLiteral("Vexara");
}

QString organizationName()
{
    return QStringLiteral("NGKsSystems");
}

QString versionLabel()
{
    return QStringLiteral("0.1.0-foundation");
}

} // namespace VexaraCore::AppIdentity
