#include "sessionwidget.h"
#include "loginwidget.h"
#include "terminalcore.h"
#include "terminalwidget.h"

#include <QDir>
#include <QFileInfo>
#include <QStackedLayout>

SessionWidget::SessionWidget(const QString &worldRoot, QWidget *parent) : QWidget(parent) {
    m_worldRoot = QDir::cleanPath(QFileInfo(worldRoot.isEmpty()
        ? TerminalCore::defaultWorldRoot() : worldRoot).absoluteFilePath());
    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_login = new LoginWidget(QFileInfo(m_worldRoot).absolutePath(), this);
    m_login->onAuthenticated = [this](const LoginResult &result) { beginSession(result); };
    m_stack->addWidget(m_login);
    setFocusProxy(m_login);
}

QString SessionWidget::currentDirectory() const {
    return m_terminal ? m_terminal->currentDirectory() : QString();
}

void SessionWidget::beginSession(const LoginResult &result) {
    if (m_terminal || !result.authenticated) return;
    m_identity = result.identity;
    m_terminal = new TerminalWidget(this, m_worldRoot, m_identity, result.previousLogin);
    m_terminal->onLogout = [this] { endSession(); };
    connect(m_terminal, &QTextEdit::textChanged, this, [this] {
        if (onStateChanged) onStateChanged();
    });
    m_stack->addWidget(m_terminal);
    m_stack->setCurrentWidget(m_terminal);
    setFocusProxy(m_terminal);
    m_terminal->setFocus();
    if (onStateChanged) onStateChanged();
}

void SessionWidget::endSession() {
    if (!m_terminal) return;
    auto *previous = m_terminal;
    m_terminal = nullptr;
    m_stack->setCurrentWidget(m_login);
    m_stack->removeWidget(previous);
    previous->hide();
    previous->deleteLater();
    m_login->reset();
    setFocusProxy(m_login);
    m_login->focusInput();
    if (onStateChanged) onStateChanged();
}

void SessionWidget::showAppearanceSettings() {
    if (m_terminal) m_terminal->showAppearanceSettings();
}
