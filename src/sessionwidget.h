#pragma once

#include "localidentity.h"
#include <QWidget>
#include <functional>
#include <memory>
class CameraImageProvider;

class LoginWidget;
class TerminalWidget;
class QStackedLayout;
struct LoginResult;

class SessionWidget : public QWidget {
public:
    explicit SessionWidget(const QString &worldRoot = {}, QWidget *parent = nullptr,
        std::shared_ptr<CameraImageProvider> cameraImages = {});
    bool loggedIn() const { return m_terminal != nullptr; }
    LocalIdentity identity() const { return m_identity; }
    QString currentDirectory() const;
    TerminalWidget *terminal() const { return m_terminal; }
    LoginWidget *loginPage() const { return m_login; }
    void showAppearanceSettings();
    std::function<void()> onStateChanged;

private:
    QString m_worldRoot;
    std::shared_ptr<CameraImageProvider> m_cameraImages;
    LocalIdentity m_identity;
    QStackedLayout *m_stack;
    LoginWidget *m_login;
    TerminalWidget *m_terminal = nullptr;
    void beginSession(const LoginResult &result);
    void endSession();
};
