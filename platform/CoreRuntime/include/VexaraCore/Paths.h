#pragma once

#include <QString>

namespace VexaraCore::Paths {

QString appDataDir();
QString globalConfigPath();
QString projectConfigPath(const QString& projectRoot);

} // namespace VexaraCore::Paths
