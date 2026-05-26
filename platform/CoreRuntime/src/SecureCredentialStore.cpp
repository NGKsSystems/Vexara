#include "VexaraCore/SecureCredentialStore.h"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>
#pragma comment(lib, "advapi32.lib")
#endif

namespace VexaraCore {

namespace {

QString credentialTarget(const QString& profileId)
{
    return QStringLiteral("Vexara/ModelApiKey/%1").arg(profileId);
}

} // namespace

bool SecureCredentialStore::isAvailable()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

QString SecureCredentialStore::storageLabel()
{
#ifdef _WIN32
    return QStringLiteral("Windows Credential Manager");
#else
    return QStringLiteral("secure storage (unavailable on this platform)");
#endif
}

bool SecureCredentialStore::exists(const QString& profileId)
{
    return !load(profileId).isEmpty();
}

bool SecureCredentialStore::save(const QString& profileId, const QString& secret)
{
#ifdef _WIN32
    const QString trimmed = secret.trimmed();
    if (profileId.isEmpty() || trimmed.isEmpty()) {
        return false;
    }

    const std::wstring target = credentialTarget(profileId).toStdWString();
    const QByteArray blob = trimmed.toUtf8();
    if (blob.isEmpty()) {
        return false;
    }

    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t*>(target.c_str());
    cred.UserName = const_cast<wchar_t*>(L"vexara");
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(blob.data()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.Comment = const_cast<wchar_t*>(L"Vexara model API key");

    return CredWriteW(&cred, 0) != FALSE;
#else
    Q_UNUSED(profileId);
    Q_UNUSED(secret);
    return false;
#endif
}

QString SecureCredentialStore::load(const QString& profileId)
{
#ifdef _WIN32
    if (profileId.isEmpty()) {
        return QString();
    }

    const std::wstring target = credentialTarget(profileId).toStdWString();
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) || credential == nullptr) {
        return QString();
    }

    QString secret;
    if (credential->CredentialBlobSize > 0 && credential->CredentialBlob != nullptr) {
        secret = QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob),
                                   static_cast<int>(credential->CredentialBlobSize));
    }
    CredFree(credential);
    return secret.trimmed();
#else
    Q_UNUSED(profileId);
    return QString();
#endif
}

bool SecureCredentialStore::remove(const QString& profileId)
{
#ifdef _WIN32
    if (profileId.isEmpty()) {
        return false;
    }
    const std::wstring target = credentialTarget(profileId).toStdWString();
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE;
#else
    Q_UNUSED(profileId);
    return false;
#endif
}

} // namespace VexaraCore
