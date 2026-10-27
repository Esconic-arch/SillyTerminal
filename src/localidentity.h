#pragma once

#include <QString>

// The first version has one local account. Keep its identity shared by login,
// the shell, and the session header rather than duplicating display strings.
struct LocalIdentity {
    QString userName = QStringLiteral("guest");
    QString hostName = QStringLiteral("silly");
    QString homePath = QStringLiteral("/home/guest");

    QString label() const { return userName + '@' + hostName; }
};
