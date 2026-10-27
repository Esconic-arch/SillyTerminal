#include "accountstore.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <cstdio>

namespace {
QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
bool write(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp(QDir::current().filePath("account-tests-XXXXXX"));
    if (!temp.isValid()) return 2;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    const QString directory = temp.path() + "/runtime";
    const QString path = directory + "/account.json";
    const QString password = QString::fromUtf8("  local 密码 42  ");
    AccountStore account(directory);
    check(account.state() == AccountStore::State::NeedsSetup && !QFileInfo::exists(directory),
        "inspecting an unconfigured account does not create runtime data");
    check(!account.createAndLogin("").authenticated && !QFileInfo::exists(path), "empty passwords do not create an account");
    const auto created = account.createAndLogin(password);
    check(created.authenticated && !created.previousLogin.isValid() && created.identity.userName == "guest",
        "first setup logs into guest without inventing an earlier login");
    check(!QFileInfo::exists(directory + "/world"), "account setup does not initialize or edit world files");
    const QByteArray original = read(path);
    const auto record = QJsonDocument::fromJson(original).object();
    check(!original.contains(password.toUtf8())
        && record.value("password").toObject().value("algorithm").toString() == "pbkdf2-sha256",
        "account stores a password derivation rather than plaintext");
    AccountStore reopened(directory);
    check(reopened.state() == AccountStore::State::Ready, "account survives reopening the store");
    const auto wrongUser = reopened.login("root", password);
    const auto wrongPassword = reopened.login("guest", "wrong");
    check(wrongUser.rejected && wrongPassword.rejected && !wrongUser.authenticated && !wrongPassword.authenticated,
        "unknown users and wrong passwords both reject authentication");
    check(read(path) == original, "rejected attempts preserve the account and successful login time");
    const auto accepted = reopened.login("guest", password);
    const QDateTime firstLogin = QDateTime::fromString(record.value("lastLogin").toString(), Qt::ISODateWithMs);
    check(accepted.authenticated && accepted.previousLogin == firstLogin,
        "successful login returns the actual preceding login time");
    const QByteArray beforeSetupAgain = read(path);
    check(!AccountStore(directory).createAndLogin("replacement").authenticated && read(path) == beforeSetupAgain,
        "setup cannot overwrite an existing password");
    QLockFile held(path + ".lock");
    check(held.tryLock(0), "hold an account lock");
    const auto busy = AccountStore(directory).login("guest", password);
    check(!busy.authenticated && !busy.error.isEmpty() && read(path) == beforeSetupAgain,
        "busy storage cannot authenticate or lose a concurrent update");
    held.unlock();
    const QString corruptPath = temp.path() + "/corrupt/account.json";
    check(write(corruptPath, "not json"), "prepare a damaged account");
    AccountStore corrupt(QFileInfo(corruptPath).absolutePath());
    check(corrupt.state() == AccountStore::State::Unavailable
        && !corrupt.createAndLogin("replacement").authenticated && read(corruptPath) == "not json",
        "damaged account data is preserved and cannot silently reset the password");
    auto invalid = record;
    auto parameters = invalid.value("password").toObject();
    parameters.insert("iterations", 150000000);
    invalid.insert("password", parameters);
    const QString invalidPath = temp.path() + "/invalid/account.json";
    check(write(invalidPath, QJsonDocument(invalid).toJson()), "prepare unsupported derivation parameters");
    check(AccountStore(QFileInfo(invalidPath).absolutePath()).state() == AccountStore::State::Unavailable,
        "unsupported derivation parameters fail before performing expensive work");
    const QString otherDirectory = temp.path() + "/other";
    check(AccountStore(otherDirectory).createAndLogin(password).authenticated, "create a second independent account");
    const auto otherPassword = QJsonDocument::fromJson(read(otherDirectory + "/account.json"))
        .object().value("password").toObject();
    const auto firstPassword = record.value("password").toObject();
    check(otherPassword.value("salt") != firstPassword.value("salt")
        && otherPassword.value("hash") != firstPassword.value("hash"),
        "independent accounts use distinct random salts for the same password");
    std::fprintf(stderr, "%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
