#include "terminalwidget.h"
#include "textpager.h"

#include <QApplication>
#include <QDir>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <cstdio>

namespace {
void key(QWidget *target, int code, const QString &text = {}, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::KeyPress, code, modifiers, text);
    QCoreApplication::sendEvent(target, &event);
}
void type(QWidget *target, const QString &text) {
    QInputMethodEvent event;
    event.setCommitString(text);
    QCoreApplication::sendEvent(target, &event);
}
QString input(const TerminalWidget &terminal) {
    const QString text = terminal.toPlainText();
    return text.mid(text.lastIndexOf("$ ") + 2);
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SillyTerminalTests");
    QCoreApplication::setApplicationName("TerminalInteraction");
    QTemporaryDir temp(QDir::current().filePath("interaction-world-XXXXXX"));
    if (!temp.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path() + "/settings");
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    const QString world = temp.path() + "/world";
    TerminalWidget terminal(nullptr, world);
    terminal.resize(800, 480);
    terminal.runCommand("echo first remembered");
    terminal.runCommand(QString::fromUtf8("echo 中文记录"));
    terminal.runCommand("echo second remembered");
    type(&terminal, "draft pending");
    const QString transcript = terminal.toPlainText().left(terminal.toPlainText().size() - input(terminal).size());
    key(&terminal, Qt::Key_R, {}, Qt::ControlModifier);
    auto *search = terminal.findChild<QLineEdit *>("historySearch");
    type(search, "remembered");
    terminal.grab().save(QCoreApplication::applicationDirPath() + "/history-search-preview.png");
    check(input(terminal) == "echo second remembered", "incremental history search recalls newest match");
    key(search, Qt::Key_R, {}, Qt::ControlModifier);
    check(input(terminal) == "echo first remembered", "Ctrl+R finds an older matching command");
    key(search, Qt::Key_G, {}, Qt::ControlModifier);
    check(input(terminal) == "draft pending" && search->isHidden(), "cancel search restores original draft");
    key(&terminal, Qt::Key_R, {}, Qt::ControlModifier);
    type(search, QString::fromUtf8("中文"));
    key(search, Qt::Key_Escape);
    check(input(terminal) == QString::fromUtf8("echo 中文记录"), "IME search accepts a matching line for editing");
    key(&terminal, Qt::Key_A, {}, Qt::ControlModifier);
    key(&terminal, Qt::Key_K, {}, Qt::ControlModifier);
    type(&terminal, "echo alpha beta");
    key(&terminal, Qt::Key_W, {}, Qt::ControlModifier);
    check(input(terminal) == "echo alpha ", "Ctrl+W cuts the preceding whitespace-delimited word");
    key(&terminal, Qt::Key_Y, {}, Qt::ControlModifier);
    check(input(terminal) == "echo alpha beta", "Ctrl+Y restores cut text");
    key(&terminal, Qt::Key_B, {}, Qt::AltModifier);
    key(&terminal, Qt::Key_K, {}, Qt::ControlModifier);
    check(input(terminal) == "echo alpha ", "Alt+B moves by word and Ctrl+K cuts to end");
    key(&terminal, Qt::Key_U, {}, Qt::ControlModifier);
    key(&terminal, Qt::Key_Y, {}, Qt::ControlModifier);
    check(input(terminal) == "echo alpha beta", "consecutive backward and forward cuts combine in order");
    key(&terminal, Qt::Key_A, {}, Qt::ControlModifier);
    key(&terminal, Qt::Key_U, {}, Qt::ControlModifier);
    check(terminal.toPlainText().startsWith(transcript), "editing shortcuts cannot change earlier transcript");
    key(&terminal, Qt::Key_K, {}, Qt::ControlModifier);
    type(&terminal, "cat \"documents/quick");
    key(&terminal, Qt::Key_Tab);
    check(input(terminal) == "cat \"documents/quick start.txt\" ", "widget completes quoted filenames");
    key(&terminal, Qt::Key_A, {}, Qt::ControlModifier);
    key(&terminal, Qt::Key_K, {}, Qt::ControlModifier);
    type(&terminal, "cat documents/quic > output.txt");
    auto cursor = terminal.textCursor();
    cursor.setPosition(cursor.position() - 13);
    terminal.setTextCursor(cursor);
    key(&terminal, Qt::Key_Tab);
    check(input(terminal) == "cat documents/quick\\ start.txt > output.txt", "widget completes at cursor while retaining redirection");
    terminal.runCommand("echo saved across restart");
    TerminalWidget reopened(nullptr, world);
    key(&reopened, Qt::Key_Up);
    check(input(reopened) == "echo saved across restart", "new widget loads persistent history");
    key(&reopened, Qt::Key_R, {}, Qt::ControlModifier);
    auto *reopenedSearch = reopened.findChild<QLineEdit *>("historySearch");
    type(reopenedSearch, "first remembered");
    key(reopenedSearch, Qt::Key_Return);
    check(reopened.toPlainText().contains("\nfirst remembered\n"), "Enter executes a history search result");
    reopened.runCommand("history -c");
    type(&reopened, "new draft");
    key(&reopened, Qt::Key_Up);
    check(input(reopened) == "new draft", "clearing history keeps navigation indices valid");
    bool closed = false;
    QString longText;
    for (int i = 0; i < 100; ++i) longText += QString("line %1: %2\n").arg(i).arg(i == 10 || i == 75 ? "needle" : "text");
    TextPager pager("log.txt", longText, terminal.font(), 115, [&] { closed = true; });
    pager.resize(600, 300);
    pager.show();
    QCoreApplication::processEvents();
    auto *page = pager.findChild<QTextEdit *>("pagerPage");
    auto *prompt = pager.findChild<QLineEdit *>("pagerSearch");
    const QString original = page->toPlainText();
    key(page, Qt::Key_Space, " ");
    check(page->verticalScrollBar()->value() > 0, "pager advances a visible page");
    key(page, Qt::Key_G, "g");
    check(page->verticalScrollBar()->value() == 0, "pager returns to top");
    key(page, Qt::Key_Slash, "/");
    type(prompt, "needle");
    key(prompt, Qt::Key_Return);
    if (page->textCursor().selectedText() != "needle")
        std::fprintf(stderr, "Pager search failed: query=%s status=%s selection=%s\n", qPrintable(prompt->text()),
            qPrintable(pager.findChild<QLabel *>("pagerStatus")->text()), qPrintable(page->textCursor().selectedText()));
    check(page->textCursor().selectedText() == "needle", "pager finds a search match");
    key(page, Qt::Key_N, "n");
    check(page->textCursor().selectionStart() > original.indexOf("line 75"), "pager advances to a later match");
    pager.grab().save(QCoreApplication::applicationDirPath() + "/pager-preview.png");
    key(page, Qt::Key_N, "n");
    check(pager.findChild<QLabel *>("pagerStatus")->text() == "Pattern not found", "pager reports exhausted search without wrapping");
    key(page, Qt::Key_N, "N", Qt::ShiftModifier);
    check(page->textCursor().selectionStart() < original.indexOf("line 11"), "N reverses search direction");
    key(page, Qt::Key_G, "G", Qt::ShiftModifier);
    key(page, Qt::Key_Question, "?");
    type(prompt, "needle");
    key(prompt, Qt::Key_Return);
    check(page->textCursor().selectionStart() > original.indexOf("line 75"), "question mark searches backward from current page");
    key(page, Qt::Key_Slash, "/");
    type(prompt, "[");
    key(prompt, Qt::Key_Return);
    check(pager.findChild<QLabel *>("pagerStatus")->text().startsWith("Invalid search pattern"), "pager rejects invalid regex without closing");
    key(prompt, Qt::Key_Escape);
    type(page, "cannot edit");
    check(page->toPlainText() == original, "pager is read-only");
    key(page, Qt::Key_Q, "q");
    check(closed, "pager quits through close callback");
    // The same pager is hosted inside the terminal; exit must restore its transcript.
    bool hosted = false;
    QTimer::singleShot(50, &terminal, [&] {
        auto *view = terminal.findChild<QTextEdit *>("pagerPage");
        hosted = view && view->toPlainText() == "from pipe\n";
        if (view) key(view, Qt::Key_Q, "q");
    });
    terminal.runCommand("echo from pipe | less");
    check(hosted && terminal.toPlainText().endsWith("guest@silly:~$ "), "piped pager restores terminal and prompt");
    std::fprintf(stderr, "%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
