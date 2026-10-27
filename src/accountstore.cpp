#include "accountstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <cstring>

namespace {
constexpr int iterations = 600000;
constexpr int keySize = 32;
bool isLink(const QFileInfo &info) { return info.isSymLink() || info.isJunction(); }
bool validPassword(const QString &password) {
    if (password.isEmpty() || password.size() > 1024) return false;
    for (const auto ch : password)
        if (ch.isNull() || ch == '\r' || ch == '\n') return false;
    return true;
}
LoginResult storageError(const QString &error) {
    LoginResult result;
    result.error = error;
    return result;
}
}

AccountStore::AccountStore(const QString &runtimeDirectory)
    : m_directory(QDir::cleanPath(QFileInfo(runtimeDirectory).absoluteFilePath())),
      m_path(QDir(m_directory).filePath("account.json")) {
    load();
}

void AccountStore::load() {
    m_state = State::Unavailable;
    m_error.clear();
    m_salt.clear();
    m_hash.clear();
    m_lastLogin = {};
    const QFileInfo directory(m_directory), info(m_path);
    if (isLink(directory) || isLink(info)) {
        m_error = "account data cannot be a symbolic link or junction";
        return;
    }
    if (directory.exists() && !directory.isDir()) {
        m_error = "runtime path is not a directory";
        return;
    }
    if (!info.exists()) { m_state = State::NeedsSetup; return; }
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32768) {
        m_error = "cannot read account.json";
        return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto object = document.object();
    const auto password = object.value("password").toObject();
    const QString salt = password.value("salt").toString();
    const QString hash = password.value("hash").toString();
    const QString lastLogin = object.value("lastLogin").toString();
    static const QRegularExpression hex("^[0-9a-f]{64}$");
    if (file.error() != QFileDevice::NoError || parseError.error != QJsonParseError::NoError
        || !document.isObject() || object.value("version").toInt() != 1
        || object.value("userName").toString() != identity().userName
        || password.value("algorithm").toString() != "pbkdf2-sha256"
        || password.value("iterations").toInt() != iterations
        || !hex.match(salt).hasMatch() || !hex.match(hash).hasMatch()
        || (!object.value("lastLogin").isString() && object.contains("lastLogin"))) {
        m_error = "invalid account.json; existing file preserved";
        return;
    }
    if (!lastLogin.isEmpty()) {
        m_lastLogin = QDateTime::fromString(lastLogin, Qt::ISODateWithMs);
        if (!m_lastLogin.isValid()) {
            m_error = "invalid last login time in account.json; existing file preserved";
            return;
        }
    }
    m_salt = QByteArray::fromHex(salt.toLatin1());
    m_hash = QByteArray::fromHex(hash.toLatin1());
    m_state = State::Ready;
}

bool AccountStore::prepareDirectory() {
    if (isLink(QFileInfo(m_directory)) || isLink(QFileInfo(m_path))
        || isLink(QFileInfo(m_path + ".lock"))) {
        m_error = "account data cannot be a symbolic link or junction";
        return false;
    }
    if (!QDir().mkpath(m_directory)) {
        m_error = "cannot create the runtime directory";
        return false;
    }
    return true;
}

QByteArray AccountStore::derive(const QString &password, const QByteArray &salt) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
        password.toUtf8(), salt, iterations, keySize);
}

bool AccountStore::save(const QDateTime &loginTime) {
    if (isLink(QFileInfo(m_directory)) || isLink(QFileInfo(m_path))) {
        m_error = "account data cannot be a symbolic link or junction";
        return false;
    }
    const QJsonObject password{{"algorithm", "pbkdf2-sha256"}, {"iterations", iterations},
        {"salt", QString::fromLatin1(m_salt.toHex())}, {"hash", QString::fromLatin1(m_hash.toHex())}};
    const QJsonObject account{{"version", 1}, {"userName", identity().userName}, {"password", password},
        {"lastLogin", loginTime.toUTC().toString(Qt::ISODateWithMs)}};
    const QByteArray data = QJsonDocument(account).toJson();
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(data) != data.size() || !file.commit()) {
        m_error = "cannot save account.json";
        return false;
    }
    m_lastLogin = loginTime;
    m_state = State::Ready;
    return true;
}

LoginResult AccountStore::createAndLogin(const QString &password) {
    if (!validPassword(password)) return storageError("password must contain 1 to 1024 characters without line breaks");
    if (!prepareDirectory()) return storageError(m_error);
    QLockFile lock(m_path + ".lock");
    if (!lock.tryLock(0)) return storageError("account database is busy; try again");
    load();
    if (m_state != State::NeedsSetup)
        return storageError(m_state == State::Ready ? "account is already configured" : m_error);
    m_salt.resize(keySize);
    for (int i = 0; i < keySize; i += sizeof(quint32)) {
        const quint32 random = QRandomGenerator::system()->generate();
        std::memcpy(m_salt.data() + i, &random, sizeof(random));
    }
    m_hash = derive(password, m_salt);
    if (m_hash.size() != keySize) return storageError("password derivation failed");
    if (!save(QDateTime::currentDateTimeUtc())) return storageError(m_error);
    return {true, false, {}, identity(), {}};
}

LoginResult AccountStore::login(const QString &userName, const QString &password) {
    if (!prepareDirectory()) return storageError(m_error);
    QLockFile lock(m_path + ".lock");
    if (!lock.tryLock(0)) return storageError("account database is busy; try again");
    load();
    if (m_state != State::Ready)
        return storageError(m_state == State::NeedsSetup ? "account is not configured" : m_error);
    // Derive for unknown names as well; rejected attempts never update lastLogin.
    const QByteArray candidate = derive(password.left(1024), m_salt);
    unsigned int difference = candidate.size() == m_hash.size() ? 0 : 1;
    for (int i = 0; i < qMin(candidate.size(), m_hash.size()); ++i)
        difference |= static_cast<unsigned char>(candidate.at(i)) ^ static_cast<unsigned char>(m_hash.at(i));
    if (difference != 0 || userName != identity().userName || !validPassword(password))
        return {false, true, {}, identity(), {}};
    const QDateTime previous = m_lastLogin;
    if (!save(QDateTime::currentDateTimeUtc())) return storageError(m_error);
    return {true, false, {}, identity(), previous};
}
