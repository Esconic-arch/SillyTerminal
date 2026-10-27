#include "aiservices.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <wincred.h>
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace {
class NativeSecrets final : public AiSecretStore {
public:
    bool read(const QString &id, QString *secret, QString *error) override {
        secret->clear();
#ifdef Q_OS_WIN
        const QString target = prefix() + id;
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &credential))
            return fail(GetLastError(), error);
        *secret = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
            credential->CredentialBlobSize);
        CredFree(credential);
        return true;
#elif defined(Q_OS_MACOS)
        auto query = macQuery(id);
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
        CFTypeRef result = nullptr;
        const OSStatus status = SecItemCopyMatching(query, &result);
        CFRelease(query);
        if (status != errSecSuccess) return fail(status, error);
        if (!result || CFGetTypeID(result) != CFDataGetTypeID()) {
            if (result) CFRelease(result);
            return fail(errSecDecode, error);
        }
        const auto data = static_cast<CFDataRef>(result);
        *secret = QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
            CFDataGetLength(data));
        CFRelease(result);
        return true;
#else
        Q_UNUSED(id);
        return fail(-1, error);
#endif
    }

    bool write(const QString &id, const QString &secret, QString *error) override {
#ifdef Q_OS_WIN
        const QString target = prefix() + id;
        QByteArray data = secret.toUtf8();
        if (data.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
            if (error) *error = QStringLiteral("API key is too long for the credential store.");
            return false;
        }
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
        credential.UserName = const_cast<LPWSTR>(L"SillyTerminal");
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.CredentialBlobSize = DWORD(data.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(data.data());
        const bool ok = CredWriteW(&credential, 0);
        const auto code = ok ? 0 : GetLastError();
        data.fill('\0');
        return ok || fail(code, error);
#elif defined(Q_OS_MACOS)
        auto query = macQuery(id);
        const QByteArray bytes = secret.toUtf8();
        auto data = CFDataCreate(kCFAllocatorDefault,
            reinterpret_cast<const UInt8 *>(bytes.constData()), bytes.size());
        CFDictionarySetValue(query, kSecValueData, data);
        const OSStatus status = SecItemAdd(query, nullptr);
        CFRelease(data);
        CFRelease(query);
        return status == errSecSuccess || fail(status, error);
#else
        Q_UNUSED(id); Q_UNUSED(secret);
        return fail(-1, error);
#endif
    }

    bool remove(const QString &id, QString *error) override {
#ifdef Q_OS_WIN
        const QString target = prefix() + id;
        if (CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0)) return true;
        const auto code = GetLastError();
        return code == ERROR_NOT_FOUND || fail(code, error);
#elif defined(Q_OS_MACOS)
        auto query = macQuery(id);
        const OSStatus status = SecItemDelete(query);
        CFRelease(query);
        return status == errSecSuccess || status == errSecItemNotFound || fail(status, error);
#else
        Q_UNUSED(id);
        return fail(-1, error);
#endif
    }
private:
    static QString prefix() { return QStringLiteral("Esconic/SillyTerminal/AI/"); }
    static bool fail(qint64 code, QString *error) {
        if (error) *error = QStringLiteral("System credential storage failed (code %1). The key was not saved to a plain-text file.").arg(code);
        return false;
    }
#ifdef Q_OS_MACOS
    static CFMutableDictionaryRef macQuery(const QString &id) {
        auto query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        const auto account = CFStringCreateWithCharacters(kCFAllocatorDefault,
            reinterpret_cast<const UniChar *>(id.utf16()), id.size());
        CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query, kSecAttrService, CFSTR("io.esconic.sillyterminal.ai"));
        CFDictionarySetValue(query, kSecAttrAccount, account);
        CFRelease(account);
        return query;
    }
#endif
};
}

std::shared_ptr<AiSecretStore> nativeAiSecretStore() {
    return std::make_shared<NativeSecrets>();
}
