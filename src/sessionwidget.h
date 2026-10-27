#pragma once

#include "localidentity.h"
#include <QWidget>
#include <functional>

class LoginWidget;
class TerminalWidget;
class QStackedLayout;
struct LoginResult;

class SessionWidget : public QWidget {
public:
    explicit SessionWidget(const QString &worldRoot = {}, QWidget *parent = nullptr);
    bool loggedIn() const { return m_terminal != nullptr; }
    LocalIdentity identity() const { return m_identity; }
    QString currentDirectory() const;
    TerminalWidget *terminal() const { return m_terminal; }
    LoginWidget *loginPage() const { return m_login; }
    void showAppearanceSettings();
    std::function<void()> onStateChanged;

private:
    QString m_worldRoot;
    LocalIdentity m_identity;
    QStackedLayout *m_stack;
    LoginWidget *m_login;
    TerminalWidget *m_terminal = nullptr;
    void beginSession(const LoginResult &result);
    void endSession();
};
