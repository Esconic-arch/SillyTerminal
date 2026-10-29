#include "terminalcore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp(QDir::current().filePath("tools-world-XXXXXX"));
    if (!temp.isValid()) return 2;
    TerminalCore core(temp.path() + "/world");
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    check(core.initializationError().isEmpty(), "isolated world initializes");
    core.execute("mkdir -p /samples/deep");
    core.saveFile("/samples/a.txt", "first\nsecond\nthird");
    core.saveFile("/samples/B.TXT", "UPPER\n");
    core.saveFile("/samples/.hidden.txt", "hidden\n");
    core.saveFile("/samples/deep/child.txt", "nested\n");
    core.saveFile("/samples/a b.log", QString::fromUtf8("你好\r\n第二行😀\r\n最后一行"));
    core.saveFile("/samples/empty", "");
    QString numbers;
    for (int i = 1; i <= 12; ++i) numbers += QString::number(i) + '\n';
    core.saveFile("/samples/numbers.log", numbers);
    check(core.execute("find /samples -name '*.txt' -type f").output
        == "/samples/.hidden.txt\n/samples/a.txt\n/samples/deep/child.txt\n", "find matches basenames case-sensitively and includes hidden files");
    check(core.execute("find /samples -iname '*.txt' -maxdepth 1").output.contains("/samples/B.TXT\n"), "iname ignores case");
    check(core.execute("find /samples -type d").output == "/samples\n/samples/deep\n", "find prints starting directory and descendants");
    check(core.execute("find /samples -maxdepth 0").output == "/samples\n", "maxdepth zero visits only the start");
    check(core.execute("find /samples -maxdepth 1 -type f").output.count('\n') == 6, "maxdepth one excludes grandchildren");
    check(core.execute("find /samples -name '[a-z].txt'").output == "/samples/a.txt\n", "find supports bracket filename patterns");
    check(core.execute("find /samples -name 'child*'").output == "/samples/deep/child.txt\n", "name matching uses filename rather than full path");
    check(core.execute("find /samples -name 'deep/*'").output.isEmpty(), "name patterns do not match directory paths");
    core.execute("cd /samples");
    check(core.execute("find . -type d").output == ".\n./deep\n", "relative start is preserved in output");
    check(core.execute("find -name 'a.txt'").output == "./a.txt\n", "find defaults to current directory");
    check(core.execute("find a.txt").output == "a.txt\n", "find accepts an individual starting file");
    check(core.execute("find /absent").error, "missing find start reports an error");
    check(core.execute("find . -name '*.absent'").exitCode == 0, "no files matched is successful empty output");
    for (const QString &syntax : {"find . -exec x", "find . -type l", "find . -maxdepth -1", "find . -maxdepth 999999999999", "find . -name"})
        check(core.execute(syntax).error, "unsupported or incomplete find expressions fail explicitly");
    check(core.execute("find . -type f | grep -F '.log' | head -n 1").output == "./a b.log\n", "find composes with existing filters and new head");
    check(!core.execute("find . -type f > list.txt").error && core.readTextFile("list.txt").text.contains("./list.txt\n"), "redirected find sees the newly-created output file on disk");
    check(core.execute("head -n 2 a.txt").output == "first\nsecond\n", "head selects first lines without altering separators");
    check(core.execute("tail -n2 a.txt").output == "second\nthird", "tail retains a final line without newline");
    check(core.execute("head numbers.log").output == numbers.left(numbers.indexOf("11\n")), "head defaults to 10 lines");
    check(core.execute("tail numbers.log").output == numbers.mid(numbers.indexOf("3\n")), "tail defaults to 10 lines");
    check(core.execute("head -n0 a.txt").output.isEmpty() && core.execute("tail -n 0 a.txt").output.isEmpty(), "zero lines produces empty output");
    check(core.execute("head -n 20 a.txt").output == "first\nsecond\nthird", "head beyond EOF returns whole text");
    check(core.execute("tail -n 20 a.txt").output == "first\nsecond\nthird", "tail beyond EOF returns whole text");
    check(core.execute("head empty").output.isEmpty() && core.execute("tail empty").output.isEmpty(), "empty files remain empty");
    check(core.execute("head -n1 'a b.log'").output == QString::fromUtf8("你好\r\n"), "head retains CRLF and UTF-8 characters");
    check(core.execute("tail -n2 'a b.log'").output == QString::fromUtf8("第二行😀\r\n最后一行"), "tail retains Unicode and original line endings");
    check(core.execute("cat a.txt | head -n2 | tail -n1").output == "second\n", "line tools receive piped stdin");
    check(core.execute("head -n1 - - < a.txt").output
        == "==> standard input <==\nfirst\n\n==> standard input <==\nsecond\n", "repeated stdin operands continue after lines consumed by head");
    check(core.execute("head -n1 a.txt empty").output == "==> a.txt <==\nfirst\n\n==> empty <==\n", "multiple files have identifying headers");
    check(core.execute("tail -n1 a.txt > last.txt").output.isEmpty() && core.readTextFile("last.txt").text == "third", "tail redirection preserves unterminated last line");
    const auto partial = core.execute("head -n1 /absent a.txt | grep -F first");
    check(partial.output == "first\n" && partial.diagnostics.contains("no such file") && partial.exitCode == 0,
        "valid file output continues through pipe while read errors stay separate");
    for (const QString &syntax : {"head -n -1 a.txt", "head -n nope a.txt", "tail -f a.txt", "tail -c2 a.txt", "head -n", "head"})
        check(core.execute(syntax).error, "unsupported line-tool modes produce explicit feedback");
    QFile external(core.worldRoot() + "/samples/a.txt");
    const auto writeOutside = [&](const QByteArray &data) {
        if (!external.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        const bool written = external.write(data) == data.size();
        external.close();
        return written;
    };
    check(writeOutside("host edit\n"), "external editor fixture writes physical file");
    check(core.execute("head a.txt").output == "host edit\n" && core.execute("tail a.txt").output == "host edit\n", "line tools read fresh external changes on each invocation");
    auto snapshot = core.readTextSnapshot("a.txt");
    check(snapshot.exists && snapshot.error.isEmpty() && snapshot.text == "host edit\n", "editor captures current file state");
    check(!core.saveTextSnapshot(snapshot, "saved\r\n").error, "unchanged disk allows a guarded save");
    check(!core.saveTextSnapshot(snapshot, "saved again\n").error, "successful write advances snapshot baseline");
    check(writeOutside("other edit!\n"), "external file content changes");
    check(core.saveTextSnapshot(snapshot, "ours\n").error && core.readTextFile("a.txt").text == "other edit!\n", "normal save cannot overwrite external content");
    check(!core.saveTextSnapshot(snapshot, "forced\n", true).error && core.readTextFile("a.txt").text == "forced\n", "explicit force replaces externally modified text");
    check(external.remove(), "external editor can delete a file");
    check(core.saveTextSnapshot(snapshot, "recreated\n").error && !external.exists(), "save cannot silently recreate an externally deleted file");
    check(!core.saveTextSnapshot(snapshot, "recreated\n", true).error && external.exists(), "explicit forced save can recreate a deleted file");
    auto missing = core.readTextSnapshot("new.txt");
    check(!missing.exists && missing.error.isEmpty(), "new buffer snapshot remembers absence");
    core.saveFile("new.txt", "created outside\n");
    check(core.saveTextSnapshot(missing, "ours\n").error, "new buffer does not overwrite a file created meanwhile");
    snapshot = core.readTextSnapshot("a.txt");
    writeOutside(QByteArray::fromHex("fffe00"));
    check(core.saveTextSnapshot(snapshot, "ours\n").error && !core.readTextSnapshot("a.txt").error.isEmpty(), "external binary or invalid UTF-8 is preserved on normal save and reload");
    check(!core.saveTextSnapshot(snapshot, "valid again\n", true).error, "forced save can intentionally replace invalid external content");
    core.saveFile("same.txt", "abc");
    auto same = core.readTextSnapshot("same.txt");
    QFile sameFile(core.worldRoot() + "/samples/same.txt");
    check(sameFile.open(QIODevice::ReadWrite), "open same-size edit fixture");
    const auto timestamp = sameFile.fileTime(QFileDevice::FileModificationTime);
    sameFile.write("xyz");
    sameFile.setFileTime(timestamp, QFileDevice::FileModificationTime);
    sameFile.close();
    check(core.saveTextSnapshot(same, "ours").error, "content digest detects changes even with identical size and timestamp");
    const QString outside = temp.path() + "/outside.txt";
    QFile sentinel(outside);
    check(sentinel.open(QIODevice::WriteOnly), "open external sentinel");
    sentinel.write("outside"); sentinel.close();
    if (QFile::link(outside, core.worldRoot() + "/samples/outside.lnk")) {
        check(!core.execute("find .").output.contains("outside.lnk"), "find skips symbolic links and shortcuts");
        auto linked = core.readTextSnapshot("outside.lnk");
        check(!linked.error.isEmpty() && core.saveTextSnapshot(linked, "overwrite", true).error, "force does not bypass the world boundary");
    }
    std::fprintf(stderr, "%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
