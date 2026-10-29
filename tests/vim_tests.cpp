#include "vimeditor.h"

#include <QApplication>
#include <QDir>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextEdit>
#include <cstdio>

namespace {
void key(QWidget *target, int code, const QString &text = {}, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::KeyPress, code, modifiers, text);
    QCoreApplication::sendEvent(target, &event);
}
QTextEdit *buffer(VimEditor &editor) { return editor.findChild<QTextEdit *>("vimBuffer"); }
void keys(VimEditor &editor, const QString &text) {
    for (const QChar ch : text) {
        if (ch == '\n') key(buffer(editor), Qt::Key_Return, "\r");
        else key(buffer(editor), ch.toUpper().unicode(), QString(ch));
    }
}
void escape(VimEditor &editor) { key(buffer(editor), Qt::Key_Escape); }
void command(VimEditor &editor, const QString &text) {
    keys(editor, text.startsWith('/') ? "/" : ":");
    auto *line = editor.findChild<QLineEdit *>("vimCommand");
    line->setText(text);
    key(line, Qt::Key_Return);
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    const QFont font("monospace", 12);
    const auto discardSave = [](const QString &, bool) { return CommandResult{}; };
    {
        bool closed = false;
        QString saved;
        VimEditor editor("/note.txt", "alpha\nbeta\n", font, 115,
            [&](const QString &text, bool) { saved = text; return CommandResult{}; }, [&] { closed = true; });
        check(editor.findChild<QLabel *>("vimMode")->text() == "NORMAL", "starts in normal mode");
        keys(editor, "z");
        check(buffer(editor)->toPlainText() == "alpha\nbeta\n", "normal mode does not type arbitrary letters");
        keys(editor, "istart ");
        check(buffer(editor)->toPlainText() == "start alpha\nbeta\n", "insert before cursor");
        escape(editor);
        check(editor.findChild<QLabel *>("vimMode")->text() == "NORMAL", "escape returns to normal");
        command(editor, ":q");
        check(!closed, "unsaved quit is blocked");
        check(editor.findChild<QLineEdit *>("vimCommand")->text().contains("E37"), "unsaved message explains force quit");
        command(editor, ":w");
        check(saved == "start alpha\nbeta\n" && !closed, "write saves without quitting and preserves final newline");
        command(editor, ":q");
        check(closed, "quit succeeds after save");
    }
    {
        VimEditor editor("/lines.txt", "a\nb\nc", font, 115, discardSave, [] {});
        keys(editor, "dd");
        check(buffer(editor)->toPlainText() == "b\nc", "dd deletes one complete line");
        keys(editor, "p");
        check(buffer(editor)->toPlainText() == "b\na\nc", "p restores a deleted line below cursor");
        keys(editor, "u");
        check(buffer(editor)->toPlainText() == "b\nc", "undo line paste");
        key(buffer(editor), Qt::Key_R, {}, Qt::ControlModifier);
        check(buffer(editor)->toPlainText() == "b\na\nc", "redo line paste");
        keys(editor, "ggyyp");
        check(buffer(editor)->toPlainText() == "b\nb\na\nc", "yy and p duplicate a complete line");
        keys(editor, "Gdd");
        check(buffer(editor)->toPlainText() == "b\nb\na", "dd last line removes its separator");
        keys(editor, "p");
        check(buffer(editor)->toPlainText() == "b\nb\na\nc", "paste below final line");
    }
    {
        VimEditor editor("/text.txt", "abc", font, 115, discardSave, [] {});
        keys(editor, "aX"); escape(editor);
        check(buffer(editor)->toPlainText() == "aXbc", "a inserts after cursor");
        keys(editor, "A!"); escape(editor);
        check(buffer(editor)->toPlainText() == "aXbc!", "A appends at line end");
        keys(editor, "I>"); escape(editor);
        check(buffer(editor)->toPlainText() == ">aXbc!", "I inserts at line start");
        keys(editor, "obelow"); escape(editor);
        check(buffer(editor)->toPlainText() == ">aXbc!\nbelow", "o opens a line below");
        keys(editor, "Omiddle"); escape(editor);
        check(buffer(editor)->toPlainText() == ">aXbc!\nmiddle\nbelow", "O opens a line above");
        keys(editor, "gg0x");
        check(buffer(editor)->toPlainText().startsWith("aXbc!"), "x deletes current character");
        keys(editor, "p");
        check(buffer(editor)->toPlainText().startsWith("a>Xbc!"), "character paste inserts after cursor");
    }
    {
        VimEditor editor("/movement.txt", "one two\nthree\nfour", font, 115, discardSave, [] {});
        keys(editor, "w");
        check(buffer(editor)->textCursor().position() == 4, "w moves to next word");
        keys(editor, "b");
        check(buffer(editor)->textCursor().position() == 0, "b moves to previous word");
        keys(editor, "G");
        check(buffer(editor)->textCursor().blockNumber() == 2, "G moves to final line");
        keys(editor, "gg");
        check(buffer(editor)->textCursor().position() == 0, "gg moves to start");
        keys(editor, "j");
        check(buffer(editor)->textCursor().blockNumber() == 1, "j moves down");
        keys(editor, "k$");
        check(buffer(editor)->textCursor().position() == 6, "k and dollar move up and to final character");
        keys(editor, "h");
        check(buffer(editor)->textCursor().position() == 5, "h moves left");
        keys(editor, "l0");
        check(buffer(editor)->textCursor().position() == 0, "l and zero move right and to line start");
        key(buffer(editor), Qt::Key_Down);
        check(buffer(editor)->textCursor().blockNumber() == 1, "arrow keys work in normal mode");
    }
    {
        VimEditor editor("/search.txt", "one two one\nend", font, 115, discardSave, [] {});
        command(editor, "/one");
        check(buffer(editor)->textCursor().position() == 8, "forward search");
        keys(editor, "n");
        check(buffer(editor)->textCursor().position() == 0, "search wraps");
        keys(editor, "N");
        check(buffer(editor)->textCursor().position() == 8, "reverse search wraps");
        command(editor, "/absent");
        check(editor.findChild<QLineEdit *>("vimCommand")->text().contains("not found"), "missing search pattern feedback");
        keys(editor, ":");
        auto *line = editor.findChild<QLineEdit *>("vimCommand");
        key(line, Qt::Key_Escape);
        check(editor.findChild<QLabel *>("vimMode")->text() == "NORMAL", "escape cancels command line");
    }
    {
        bool closed = false;
        VimEditor editor("/failed.txt", "original", font, 115,
            [](const QString &, bool) { return CommandResult{"read-only target", true}; }, [&] { closed = true; });
        keys(editor, "iX"); escape(editor);
        command(editor, ":wq");
        check(!closed && editor.findChild<QLineEdit *>("vimCommand")->text().contains("Write failed"), "failed write does not quit");
        command(editor, ":q!");
        check(closed, "force quit discards unsaved changes");
    }
    {
        bool closed = false;
        VimEditor editor("/windows.txt", "first\r\nsecond\r\n", font, 115, discardSave, [&] { closed = true; });
        command(editor, ":q");
        check(closed, "CRLF input is not considered edited on opening");
    }
    {
        VimEditor editor("/single.txt", "only line", font, 115, discardSave, [] {});
        keys(editor, "ddp");
        check(buffer(editor)->toPlainText() == "only line", "restore a deleted sole line without adding a blank first line");
    }
    {
        const QString emoji = QString::fromUtf8("a😀");
        VimEditor editor("/unicode.txt", emoji, font, 115, discardSave, [] {});
        keys(editor, "$x");
        check(buffer(editor)->toPlainText() == "a", "end-of-line deletion respects supplementary Unicode characters");
    }
    {
        QTemporaryDir temporary(QDir::current().filePath("vim-world-XXXXXX"));
        if (!temporary.isValid()) return 2;
        TerminalCore core(temporary.path() + "/world");
        const auto opened = core.execute("vim 'new note.txt'");
        check(opened.editPath == "/home/guest/new note.txt" && !opened.error && opened.editContent.isEmpty(), "vim command opens a new file");
        bool closed = false;
        VimEditor editor(opened.editPath, opened.editContent, font, 115,
            [&](const QString &text, bool) { return core.saveFile(opened.editPath, text); }, [&] { closed = true; });
        QInputMethodEvent chinese;
        chinese.setCommitString(QString::fromUtf8("你好，Vim。"));
        QCoreApplication::sendEvent(buffer(editor), &chinese);
        check(buffer(editor)->toPlainText().isEmpty(), "normal mode ignores IME edits");
        keys(editor, "i");
        QCoreApplication::sendEvent(buffer(editor), &chinese);
        check(buffer(editor)->toPlainText() == QString::fromUtf8("你好，Vim。"), "insert mode accepts Chinese IME");
        escape(editor);
        command(editor, ":wq");
        check(closed && core.execute("cat 'new note.txt'").text == QString::fromUtf8("你好，Vim。"), "wq saves UTF-8 to real world file");
        TerminalCore reloaded(core.worldRoot());
        check(reloaded.execute("cat 'new note.txt'").text == QString::fromUtf8("你好，Vim。"), "vim content survives restart");
        check(core.execute("vim /missing/file.txt").error, "vim rejects missing parent");
        check(core.execute("vim documents").error, "vim rejects directories");
    }
    {
        QString disk = "outside content\r\n";
        bool closed = false, forced = false;
        bool unreadable = false;
        VimEditor editor("/shared.txt", "original\n", font, 125,
            [&](const QString &text, bool force) {
                forced = force;
                if (!force) return CommandResult{"File changed outside Vim", true};
                disk = text;
                return CommandResult{};
            }, [&] { closed = true; }, [&] {
                return unreadable ? CommandResult{"invalid UTF-8", true} : CommandResult{disk};
            });
        keys(editor, "iours "); escape(editor);
        const QString ours = buffer(editor)->toPlainText();
        command(editor, ":wq");
        check(!closed && buffer(editor)->toPlainText() == ours && disk.startsWith("outside"), "save conflict keeps Vim open with local changes intact");
        command(editor, ":e");
        check(buffer(editor)->toPlainText() == ours && editor.findChild<QLineEdit *>("vimCommand")->text().contains("E37"), "reload refuses to discard unsaved edits without bang");
        unreadable = true;
        command(editor, ":e!");
        check(buffer(editor)->toPlainText() == ours && editor.findChild<QLineEdit *>("vimCommand")->text().contains("Read failed"), "failed reload keeps local text and undo history");
        unreadable = false;
        command(editor, ":e!");
        check(buffer(editor)->toPlainText() == "outside content\n", "forced reload reads and normalizes external text");
        keys(editor, "u");
        check(buffer(editor)->toPlainText() == "outside content\n", "reloading clears stale undo history");
        command(editor, ":e");
        check(!editor.findChild<QLineEdit *>("vimCommand")->text().contains("E37"), "reloaded buffer is unmodified");
        keys(editor, "inew "); escape(editor);
        command(editor, ":w!");
        check(forced && !closed && disk.startsWith("new "), "bang write passes explicit force flag without quitting");
        command(editor, ":wq!");
        check(closed && forced, "forced write-and-quit closes only after successful save");
    }
    std::fprintf(stderr, "%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
