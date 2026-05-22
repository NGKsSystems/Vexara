#include "VexaraCore/TerminalSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>

namespace VexaraCore {

namespace {

TerminalProfile makeProfile(const QString& id,
                            const QString& displayName,
                            const QString& program,
                            const QStringList& args)
{
    TerminalProfile profile;
    profile.id = id;
    profile.displayName = displayName;
    profile.program = program;
    profile.args = args;
    return profile;
}

QString resolveExecutable(const QString& program)
{
    if (QFileInfo::exists(program)) {
        return QFileInfo(program).absoluteFilePath();
    }

    const QString found = QStandardPaths::findExecutable(program);
    if (!found.isEmpty()) {
        return found;
    }

    return program;
}

} // namespace

QVector<TerminalProfile> TerminalSettings::profiles() const
{
    return profiles_;
}

TerminalProfile TerminalSettings::profileById(const QString& id) const
{
    for (const TerminalProfile& profile : profiles_) {
        if (profile.id == id) {
            return profile;
        }
    }
    return TerminalProfile{};
}

bool TerminalSettings::hasProfile(const QString& id) const
{
    return !profileById(id).id.isEmpty();
}

void TerminalSettings::setProfiles(const QVector<TerminalProfile>& profiles)
{
    profiles_ = profiles;
}

void TerminalSettings::setDefaultProfileId(const QString& id)
{
    defaultProfileId = id;
}

void TerminalSettings::ensureDefaults()
{
    bool hasCmd = hasProfile(QStringLiteral("cmd"));
    bool hasPwsh = hasProfile(QStringLiteral("pwsh"));
    bool hasPs = hasProfile(QStringLiteral("powershell"));
    bool hasGitBash = hasProfile(QStringLiteral("git-bash"));

    if (!profiles_.isEmpty()) {
        if (!hasCmd) {
            const QString cmdPath = resolveExecutable(QStringLiteral("C:/Windows/System32/cmd.exe"));
            profiles_.append(makeProfile(
                QStringLiteral("cmd"),
                QStringLiteral("Command Prompt"),
                cmdPath,
                {QStringLiteral("/Q"), QStringLiteral("/K")}));
        }
        if (!hasPwsh) {
            const QString pwshPath = resolveExecutable(QStringLiteral("pwsh"));
            if (!pwshPath.isEmpty() && QFileInfo::exists(pwshPath)) {
                profiles_.append(makeProfile(
                    QStringLiteral("pwsh"),
                    QStringLiteral("PowerShell 7"),
                    pwshPath,
                    {QStringLiteral("-NoLogo"), QStringLiteral("-NoExit")}));
            }
        }
        if (!hasPs) {
            const QString psPath = resolveExecutable(
                QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"));
            if (QFileInfo::exists(psPath)) {
                profiles_.append(makeProfile(
                    QStringLiteral("powershell"),
                    QStringLiteral("PowerShell"),
                    psPath,
                    {QStringLiteral("-NoLogo"), QStringLiteral("-NoExit")}));
            }
        }
        if (!hasGitBash) {
            const QString gitBash = resolveExecutable(
                QStringLiteral("C:/Program Files/Git/bin/bash.exe"));
            if (QFileInfo::exists(gitBash)) {
                profiles_.append(makeProfile(
                    QStringLiteral("git-bash"),
                    QStringLiteral("Git Bash"),
                    gitBash,
                    {QStringLiteral("-i")}));
            }
        }

        if (defaultProfileId.isEmpty() || !hasProfile(defaultProfileId)) {
            if (hasProfile(QStringLiteral("pwsh"))) {
                defaultProfileId = QStringLiteral("pwsh");
            } else if (hasProfile(QStringLiteral("powershell"))) {
                defaultProfileId = QStringLiteral("powershell");
            } else if (hasProfile(QStringLiteral("cmd"))) {
                defaultProfileId = QStringLiteral("cmd");
            } else {
                defaultProfileId = profiles_.first().id;
            }
        }
        return;
    }

    const QString cmdPath = resolveExecutable(
        QStringLiteral("C:/Windows/System32/cmd.exe"));
    profiles_.append(makeProfile(
        QStringLiteral("cmd"),
        QStringLiteral("Command Prompt"),
        cmdPath,
        {QStringLiteral("/Q"), QStringLiteral("/K")}));

    const QString pwshPath = resolveExecutable(QStringLiteral("pwsh"));
    if (!pwshPath.isEmpty() && QFileInfo::exists(pwshPath)) {
        profiles_.append(makeProfile(
            QStringLiteral("pwsh"),
            QStringLiteral("PowerShell 7"),
            pwshPath,
            {QStringLiteral("-NoLogo"), QStringLiteral("-NoExit")}));
    }

    const QString psPath = resolveExecutable(
        QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"));
    if (QFileInfo::exists(psPath)) {
        profiles_.append(makeProfile(
            QStringLiteral("powershell"),
            QStringLiteral("PowerShell"),
            psPath,
            {QStringLiteral("-NoLogo"), QStringLiteral("-NoExit")}));
    }

    const QString gitBash = resolveExecutable(
        QStringLiteral("C:/Program Files/Git/bin/bash.exe"));
    if (QFileInfo::exists(gitBash)) {
        profiles_.append(makeProfile(
            QStringLiteral("git-bash"),
            QStringLiteral("Git Bash"),
            gitBash,
            {QStringLiteral("-i")}));
    }

    if (defaultProfileId.isEmpty()) {
        if (hasProfile(QStringLiteral("pwsh"))) {
            defaultProfileId = QStringLiteral("pwsh");
        } else if (hasProfile(QStringLiteral("powershell"))) {
            defaultProfileId = QStringLiteral("powershell");
        } else {
            defaultProfileId = QStringLiteral("cmd");
        }
    }
}

bool TerminalSettings::loadFromJson(const QJsonObject& root)
{
    const QJsonObject terminal = root.value(QStringLiteral("terminal")).toObject();
    defaultProfileId = terminal.value(QStringLiteral("default_profile")).toString();

    profiles_.clear();
    const QJsonObject profileMap = terminal.value(QStringLiteral("profiles")).toObject();
    for (auto it = profileMap.begin(); it != profileMap.end(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject entry = it.value().toObject();
        TerminalProfile profile;
        profile.id = it.key();
        profile.displayName = entry.value(QStringLiteral("display_name")).toString(profile.id);
        profile.program = entry.value(QStringLiteral("program")).toString();
        const QJsonArray args = entry.value(QStringLiteral("args")).toArray();
        for (const QJsonValue& arg : args) {
            profile.args.append(arg.toString());
        }
        if (!profile.program.isEmpty()) {
            profiles_.append(profile);
        }
    }

    ensureDefaults();
    if (!hasProfile(defaultProfileId)) {
        if (hasProfile(QStringLiteral("cmd"))) {
            defaultProfileId = QStringLiteral("cmd");
        } else if (!profiles_.isEmpty()) {
            defaultProfileId = profiles_.first().id;
        }
    }
    return true;
}

void TerminalSettings::saveToJson(QJsonObject& root) const
{
    QJsonObject terminal;
    terminal.insert(QStringLiteral("default_profile"), defaultProfileId);

    QJsonObject profileMap;
    for (const TerminalProfile& profile : profiles_) {
        QJsonObject entry;
        entry.insert(QStringLiteral("display_name"), profile.displayName);
        entry.insert(QStringLiteral("program"), profile.program);
        QJsonArray args;
        for (const QString& arg : profile.args) {
            args.append(arg);
        }
        entry.insert(QStringLiteral("args"), args);
        profileMap.insert(profile.id, entry);
    }
    terminal.insert(QStringLiteral("profiles"), profileMap);
    root.insert(QStringLiteral("terminal"), terminal);
}

} // namespace VexaraCore
