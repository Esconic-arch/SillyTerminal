#include "sessionwidget.h"
#include "loginwidget.h"
#include "terminalwidget.h"
#include "browsercore.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <functional>

namespace {
void enter(QLineEdit *input, const QString &text) {
    input->clear();
    QInputMethodEvent edit;
    edit.setCommitString(text);
    QCoreApplication::sendEvent(input, &edit);
    QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(input, &key);
}
bool until(const std::function<bool()> &condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 12000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return condition();
}
QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SillyTerminalTests");
    QCoreApplication::setApplicationName("LocalSession");
    QTemporaryDir temp(QDir::current().filePath("session-tests-XXXXXX"));
    if (!temp.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path() + "/settings");
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    const QString world = temp.path() + "/runtime/world";
    const QString password = QString::fromUtf8("sample 密码 42");
    {
        SessionWidget session(world);
        session.resize(800, 480);
        session.show();
        QCoreApplication::processEvents();
        auto *login = session.loginPage();
        auto *input = login->findChild<QLineEdit *>("loginInput");
        auto *prompt = login->findChild<QLabel *>("loginPrompt");
        auto *transcript = login->findChild<QTextEdit *>("loginTranscript");
        check(!session.loggedIn() && !session.terminal() && !QFileInfo::exists(world),
            "startup presents login without constructing a shell or seeding world files");
        check(prompt->text() == "New password: " && input->echoMode() == QLineEdit::NoEcho,
            "first run asks for a hidden password");
        session.grab().save(QCoreApplication::applicationDirPath() + "/login-setup-preview.png");
        enter(input, "");
        check(prompt->text() == "New password: " && transcript->toPlainText().contains("Password cannot be empty"),
            "empty setup password remains at setup");
        enter(input, password);
        enter(input, "different");
        check(!session.loggedIn() && prompt->text() == "New password: "
            && !QFileInfo::exists(temp.path() + "/runtime/account.json"),
            "mismatched confirmation cannot create an account or enter the shell");
        enter(input, password);
        input->setText(password);
        QApplication::clipboard()->setText("unchanged clipboard");
        input->selectAll();
        input->copy();
        check(input->displayText().isEmpty() && QApplication::clipboard()->text() == "unchanged clipboard",
            "password is invisible and cannot be copied from the hidden input");
        check(!transcript->toPlainText().contains(password), "passwords never enter the login transcript");
        bool responsive = false;
        QTimer::singleShot(0, &session, [&] { responsive = true; });
        enter(input, password);
        check(input->parentWidget()->isHidden() && !session.terminal(),
            "verification hides the completed password prompt and does not expose a shell early");
        check(until([&] { return session.loggedIn(); }), "matching confirmation opens an authenticated shell");
        check(responsive, "password derivation leaves the UI event loop responsive");
        if (!session.terminal()) return 3;
        auto *terminal = session.terminal();
        check(terminal->toPlainText() == "guest@silly:~$ ", "first login does not invent a Last login banner");
        terminal->runCommand("whoami");
        check(terminal->toPlainText().contains("\nguest\n"), "shell identity agrees with the login account");
        terminal->runCommand("echo retained > login-state.txt");
        terminal->runCommand("cd documents");
        const QString browserFile = temp.path() + "/runtime/browser.json";
        BrowserCore browser(browserFile);
        browser.navigate("www.hcmetro.com");
        check(!browser.isBookmarked() && browser.toggleBookmark(), "save a browser bookmark before logout");
        const QByteArray browserBefore = read(browserFile);
        terminal->runCommand("exit | cat");
        check(session.loggedIn() && terminal->toPlainText().contains("cannot be used in a pipeline"),
            "exit in an unsupported pipeline cannot end the active session");
        terminal->runCommand("logout");
        check(!session.loggedIn() && session.terminal() == nullptr && prompt->text() == "silly login: ",
            "logout destroys the shell session and returns to username entry");
        const QByteArray accountBefore = read(temp.path() + "/runtime/account.json");
        enter(input, "guest");
        enter(input, "wrong");
        check(until([&] { return transcript->toPlainText().contains("Login incorrect")
            && !input->isReadOnly(); }) && !session.loggedIn(), "bad password retries without exposing a shell");
        check(read(temp.path() + "/runtime/account.json") == accountBefore,
            "bad password does not alter the previous successful login time");
        enter(input, "root");
        enter(input, password);
        check(until([&] { return !input->isReadOnly(); }) && !session.loggedIn(),
            "unknown user is rejected even with the correct guest password");
        enter(input, "guest");
        enter(input, password);
        check(until([&] { return session.loggedIn(); }), "valid credentials work after failed attempts");
        if (!session.terminal()) return 4;
        terminal = session.terminal();
        check(terminal->toPlainText().startsWith("Last login: ") && session.currentDirectory() == "~/documents",
            "relogin reports a real preceding login and restores the saved directory");
        terminal->runCommand("cat ../login-state.txt");
        check(terminal->toPlainText().contains("\nretained\n") && read(browserFile) == browserBefore,
            "logout and login preserve edited files and browser state");
        terminal->runCommand("history");
        check(terminal->toPlainText().contains("echo retained > login-state.txt")
            && !read(temp.path() + "/runtime/history.json").contains(password.toUtf8()),
            "command history survives login and never contains a login password");
        session.grab().save(QCoreApplication::applicationDirPath() + "/login-session-preview.png");
        terminal->runCommand("exit");
        check(!session.loggedIn(), "exit also returns to the login screen");
    }
    {
        SessionWidget reopened(world);
        auto *input = reopened.loginPage()->findChild<QLineEdit *>("loginInput");
        auto *prompt = reopened.loginPage()->findChild<QLabel *>("loginPrompt");
        check(!reopened.loggedIn() && prompt->text() == "silly login: ",
            "a fresh application session always requires login");
        enter(input, "guest");
        enter(input, password);
        check(until([&] { return reopened.loggedIn(); }) && reopened.currentDirectory() == "~/documents",
            "stored credentials and world state survive recreating the application session");
    }
    {
        const QString legacyRoot = temp.path() + "/upgrade";
        TerminalCore legacy(legacyRoot + "/world");
        legacy.execute("echo existing save > upgrade.txt");
        legacy.execute("cd documents");
        const QByteArray fileBefore = read(legacyRoot + "/world/home/guest/upgrade.txt");
        const QByteArray historyBefore = read(legacyRoot + "/history.json");
        const QByteArray directoryBefore = read(legacyRoot + "/session.json");
        SessionWidget upgraded(legacyRoot + "/world");
        auto *input = upgraded.loginPage()->findChild<QLineEdit *>("loginInput");
        enter(input, password);
        enter(input, password);
        check(until([&] { return upgraded.loggedIn(); }) && upgraded.currentDirectory() == "~/documents"
            && read(legacyRoot + "/world/home/guest/upgrade.txt") == fileBefore
            && read(legacyRoot + "/history.json") == historyBefore
            && read(legacyRoot + "/session.json") == directoryBefore,
            "adding login to a pre-existing computer preserves files, history, and saved directory byte for byte");
    }
    const QString corruptRoot = temp.path() + "/damaged";
    QDir().mkpath(corruptRoot);
    QFile corrupt(corruptRoot + "/account.json");
    if (!corrupt.open(QIODevice::WriteOnly)) return 5;
    corrupt.write("damaged account");
    corrupt.close();
    SessionWidget damaged(corruptRoot + "/world");
    auto *retry = damaged.loginPage()->findChild<QLineEdit *>("loginInput");
    enter(retry, "");
    check(!damaged.loggedIn() && read(corrupt.fileName()) == "damaged account" && !QFileInfo::exists(corruptRoot + "/world"),
        "damaged account cannot bypass login or erase existing account data");
    std::fprintf(stderr, "%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
