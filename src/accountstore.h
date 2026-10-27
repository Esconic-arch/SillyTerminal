#pragma once

#include "localidentity.h"
#include <QByteArray>
#include <QDateTime>

struct LoginResult {
    bool authenticated = false;
    bool rejected = false;
    QString error;
    LocalIdentity identity;
    QDateTime previousLogin;
};

class AccountStore {
public:
    enum class State { NeedsSetup, Ready, Unavailable };

    explicit AccountStore(const QString &runtimeDirectory);
    State state() const { return m_state; }
    QString error() const { return m_error; }
    LocalIdentity identity() const { return {}; }
    LoginResult createAndLogin(const QString &password);
    LoginResult login(const QString &userName, const QString &password);

private:
    QString m_directory;
    QString m_path;
    State m_state = State::Unavailable;
    QString m_error;
    QByteArray m_salt;
    QByteArray m_hash;
    QDateTime m_lastLogin;

    void load();
    bool prepareDirectory();
    bool save(const QDateTime &loginTime);
    static QByteArray derive(const QString &password, const QByteArray &salt);
};
