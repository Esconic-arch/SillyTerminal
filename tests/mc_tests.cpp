#include "mcfilemanager.h"
#include "terminalwidget.h"
#include "textpager.h"
#include "vimeditor.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>
#include <cstdlib>

namespace {
void key(QWidget *target, int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::KeyPress, code, modifiers);
    QCoreApplication::sendEvent(target, &event);
}
bool select(QTreeWidget *panel, const QString &name) {
    for (int i = 0; i < panel->topLevelItemCount(); ++i) {
        if (panel->topLevelItem(i)->data(0, Qt::UserRole).toString() != name) continue;
        panel->setCurrentItem(panel->topLevelItem(i));
        return true;
    }
    return false;
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SillyTerminalTests");
    QCoreApplication::setApplicationName("McFileManager");
    QTemporaryDir temp(QDir::current().filePath("mc-tests-XXXXXX"));
    if (!temp.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path() + "/settings");
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    const QString world = temp.path() + "/world";
    TerminalCore core(world);
    check(core.initializationError().isEmpty(), "isolated world initializes");
    const auto launch = core.execute("mc documents /var/log");
    check(launch.openFileManager && launch.managerPaths == QStringList{"/home/guest/documents", "/var/log"},
        "mc validates and resolves both startup paths");
    check(core.complete("mc").replacement == "mc ", "mc participates in completion");
    check(!core.execute("mc --help").openFileManager && core.execute("mc --help").text.contains("Usage:"), "command help is available");
    check(core.execute("mc notes.txt").error && core.execute("mc --unknown").error
        && core.execute("mc a b c").error, "invalid startup paths and options fail before opening UI");
    check(core.execute("mc > should-not-exist").error && core.execute("echo text | mc").error
        && !QFileInfo::exists(world + "/home/guest/should-not-exist"), "interactive redirection is rejected before creating a file");

    core.createDirectory("source");
    core.createDirectory("target");
    core.createDirectory("source/tree");
    const QString unusual = QString::fromUtf8("资料 'quote' & (draft).txt");
    check(!core.saveFile("source/" + unusual, QString::fromUtf8("中文内容\n")).error, "valid special-character fixture can be written");
    check(core.saveFile("source/invalid|name.txt", "text").error, "unsupported native filename characters remain rejected");
    core.saveFile("source/tree/inside.txt", "nested\n");
    core.saveFile("source/a.txt", "first\n");
    core.saveFile("source/b.txt", "second\n");
    core.saveFile("source/.hidden", "hidden\n");
    QFile binary(world + "/home/guest/source/blob.bin");
    check(binary.open(QIODevice::WriteOnly), "binary fixture can be written");
    const QByteArray bytes("\0\xff\x01\x02", 4);
    binary.write(bytes);
    binary.close();
    const QStringList historyBefore = core.history();
    bool closed = false;
    McFileManager manager(core, {"/home/guest/source", "/home/guest/target"}, QFont("Consolas", 11), 115,
        [&] { closed = true; });
    manager.resize(1000, 560);
    manager.show();
    manager.focusPanel();
    QCoreApplication::processEvents();
    auto *left = manager.findChild<QTreeWidget *>("mcLeftPanel");
    auto *right = manager.findChild<QTreeWidget *>("mcRightPanel");
    auto *input = manager.findChild<QLineEdit *>("mcInput");
    auto *ok = manager.findChild<QPushButton *>("mcAccept");
    auto *cancel = manager.findChild<QPushButton *>("mcCancel");
    auto *status = manager.findChild<QLabel *>("mcStatus");
    check(select(left, ".hidden"), "dotfiles appear in panel");
    key(left, Qt::Key_Period, Qt::AltModifier);
    check(!select(left, ".hidden"), "dotfiles can be hidden");
    key(left, Qt::Key_Period, Qt::AltModifier);

    check(select(left, unusual), "Unicode and shell metacharacters are listed literally");
    key(left, Qt::Key_F5);
    check(input->isVisible() && input->text() == "/home/guest/target/", "copy defaults to opposite panel");
    key(input, Qt::Key_Escape);
    check(core.readTextFile("target/" + unusual).error, "cancel copy leaves world untouched");
    key(left, Qt::Key_F5);
    key(input, Qt::Key_Return);
    check(core.readTextFile("target/" + unusual).text == QString::fromUtf8("中文内容\n"),
        "copy uses structured paths without shell interpretation");
    check(core.history() == historyBefore, "file manager operations do not pollute shell history");
    core.saveFile("source/" + unusual, "changed source\n");
    key(left, Qt::Key_F5);
    ok->click();
    check(status->text().contains("already exists") && core.readTextFile("target/" + unusual).text.contains(QString::fromUtf8("中文")),
        "existing destination is preserved and failure is visible");

    select(left, "tree");
    key(left, Qt::Key_F5);
    ok->click();
    check(core.readTextFile("target/tree/inside.txt").text == "nested\n", "directories copy recursively");
    select(left, "blob.bin");
    key(left, Qt::Key_F5);
    ok->click();
    QFile copied(world + "/home/guest/target/blob.bin");
    check(copied.open(QIODevice::ReadOnly), "binary copy can be read");
    check(copied.readAll() == bytes, "copy preserves binary bytes");
    key(left, Qt::Key_F3);
    check(status->text().contains("not a text file"), "binary view produces an honest unsupported-content error");

    QString viewed, edited;
    manager.viewText = [&](const QString &path, const QString &content) { viewed = path + ':' + content; };
    manager.editText = [&](const QString &path, const QString &) { edited = path; core.saveFile(path, "edited\n"); };
    select(left, unusual);
    key(left, Qt::Key_F3);
    check(viewed.contains("changed source"), "viewer receives current file data");
    key(left, Qt::Key_F4);
    check(edited.endsWith(unusual) && core.readTextFile(edited).text == "edited\n", "editor writes the same virtual file");
    key(left, Qt::Key_F1);
    check(viewed.contains("MIDNIGHT COMMANDER"), "F1 opens software help");

    select(left, "a.txt");
    key(left, Qt::Key_Insert);
    select(left, "b.txt");
    key(left, Qt::Key_Insert);
    key(left, Qt::Key_F5);
    input->setText("/home/guest/target/not-a-directory");
    ok->click();
    check(status->text().contains("existing destination directory") && core.readTextFile("target/a.txt").error,
        "batch requires a directory and makes no changes for an invalid target");
    key(left, Qt::Key_F5);
    ok->click();
    check(core.readTextFile("target/a.txt").text == "first\n" && core.readTextFile("target/b.txt").text == "second\n",
        "marked files are copied together");
    select(left, "a.txt");
    key(left, Qt::Key_F6);
    input->setText("renamed file.txt");
    ok->click();
    check(core.readTextFile("source/a.txt").error && core.readTextFile("source/renamed file.txt").text == "first\n",
        "relative move destination is resolved in the active panel");
    key(left, Qt::Key_F7);
    input->setText(QString::fromUtf8("新文件夹"));
    ok->click();
    check(core.listDirectory(QString::fromUtf8("source/新文件夹")).error.isEmpty(), "mkdir creates a real persistent directory");

    select(left, "tree");
    key(left, Qt::Key_Return);
    check(select(left, "inside.txt"), "Enter changes panel directory");
    key(left, Qt::Key_Backspace);
    check(left->currentItem()->data(0, Qt::UserRole).toString() == "tree", "parent navigation restores selection on the child");
    key(left, Qt::Key_F8);
    key(cancel, Qt::Key_Return);
    check(!core.readTextFile("source/tree/inside.txt").error, "delete dialog defaults to cancel");
    key(left, Qt::Key_F8);
    ok->click();
    check(!QFileInfo::exists(world + "/home/guest/source/tree"), "confirmed deletion removes a nonempty directory");

    // Partial batches retain completed work and unprocessed marks.
    select(left, "b.txt");
    key(left, Qt::Key_Insert);
    select(left, "renamed file.txt");
    key(left, Qt::Key_Insert);
    key(left, Qt::Key_F5);
    ok->click();
    check(status->text().contains("0 of 2") && core.readTextFile("target/renamed file.txt").error,
        "batch stops on an existing destination instead of reporting false success");
    select(left, "b.txt"); key(left, Qt::Key_Insert);
    select(left, "renamed file.txt"); key(left, Qt::Key_Insert);

    key(left, Qt::Key_Tab);
    check(right->hasFocus(), "Tab switches active panel");
    key(right, Qt::Key_C, Qt::AltModifier);
    input->setText("/var/log");
    ok->click();
    check(select(right, "boot.log"), "change-directory dialog navigates the active panel");
    key(right, Qt::Key_Backtab, Qt::ShiftModifier);
    manager.grab().save(QCoreApplication::applicationDirPath() + "/mc-preview.png");
    manager.resize(540, 320);
    QCoreApplication::processEvents();
    check(left->isColumnHidden(2) && left->width() > 150 && right->width() > 150, "narrow terminals keep both filename panels usable");
    manager.resize(1000, 560);
    key(left, Qt::Key_F10);
    check(closed && core.currentPath() == "/home/guest", "quit returns without changing the shell directory");
    TerminalCore reloaded(world);
    McFileManager restored(reloaded, {}, manager.font(), 115, [] {});
    auto *restoredRight = restored.findChild<QTreeWidget *>("mcRightPanel");
    check(select(restoredRight, "boot.log"), "passive directory survives program restart");
    check(reloaded.readTextFile("source/renamed file.txt").text == "first\n", "file operations survive restart");
    check(core.removeEntry("/home").error && core.moveEntry("/home/guest", "/var/guest").error,
        "structured operations retain current-directory protection");
    check(!core.listDirectory("/C:/Windows").error.isEmpty(), "structured access rejects native drive paths");

    // Exercise the actual terminal -> manager -> pager/Vim -> manager -> terminal path.
    TerminalWidget terminal(nullptr, world);
    terminal.resize(1000, 560);
    terminal.show();
    terminal.runCommand("echo transcript survives");
    bool integrated = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [] { std::fprintf(stderr, "FAIL: nested program exit timed out\n"); std::abort(); });
    watchdog.start(8000);
    QTimer::singleShot(0, [&] {
        auto *mc = static_cast<McFileManager *>(terminal.findChild<QWidget *>("mcFileManager"));
        if (!mc) return;
        auto *panel = mc->findChild<QTreeWidget *>("mcLeftPanel");
        select(panel, "notes.txt");
        QTimer::singleShot(0, [&] {
            auto *pager = static_cast<TextPager *>(terminal.findChild<QWidget *>("textPager"));
            check(pager && pager->isVisible(), "F3 opens terminal pager above manager");
            if (pager) key(pager->findChild<QTextEdit *>("pagerPage"), Qt::Key_Escape);
        });
        key(panel, Qt::Key_F3);
        check(mc->isVisible(), "closing pager restores manager");
        QTimer::singleShot(0, [&] {
            auto *vim = static_cast<VimEditor *>(terminal.findChild<QWidget *>("vimEditor"));
            check(vim && vim->isVisible(), "F4 opens the existing Vim editor");
            if (vim) {
                QKeyEvent colon(QEvent::KeyPress, Qt::Key_Colon, Qt::NoModifier, ":");
                QCoreApplication::sendEvent(vim->findChild<QTextEdit *>("vimBuffer"), &colon);
                auto *command = vim->findChild<QLineEdit *>("vimCommand");
                command->setText(":q");
                key(command, Qt::Key_Return);
            }
        });
        key(panel, Qt::Key_F4);
        integrated = mc->isVisible();
        key(panel, Qt::Key_F10);
    });
    terminal.runCommand("mc");
    watchdog.stop();
    check(integrated && !terminal.findChild<QWidget *>("mcFileManager") && terminal.toPlainText().contains("transcript survives")
        && terminal.toPlainText().endsWith("guest@silly:~$ "), "nested programs return to the preserved terminal transcript and prompt");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
