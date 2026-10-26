#include "terminalcore.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary(QDir::current().filePath("core-world-XXXXXX"));
    if (!temporary.isValid()) return 2;
    const QString world = temporary.path() + "/world";
    TerminalCore core(world);
    if (!core.initializationError().isEmpty())
        std::fprintf(stderr, "World initialization failed: %s\n", qPrintable(core.initializationError()));
    int failures = 0;
    int checks = 0;
    const auto check = [&](bool condition, const char *name) {
        ++checks;
        if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    check(core.execute("pwd").text == "/home/guest", "initial directory");
    check(core.execute("ls").text.contains("documents/"), "list directories");
    check(!core.execute("ls").text.contains(".config"), "hidden entries omitted");
    check(core.execute("ls -a").text.contains(".config/"), "hidden entries shown");
    check(core.execute("ls /var/log").text == "boot.log", "absolute ls path");
    check(!core.execute("cd documents").error, "relative cd");
    check(core.displayPath() == "~/documents", "home-relative prompt");
    check(core.execute("cat welcome.txt").text.contains(QString::fromUtf8("你好")), "UTF-8 file contents");
    check(core.execute("cat 'quick start.txt'").text.contains("NAVIGATION"), "single quoted path");
    check(core.execute("cat \"quick start.txt\"").text.contains("NAVIGATION"), "double quoted path");
    check(core.execute("cat quick\\ start.txt").text.contains("NAVIGATION"), "escaped space path");
    check(core.execute("cd welcome.txt").error, "cd rejects files");
    check(core.currentPath() == "/home/guest/documents", "failed cd preserves path");
    check(core.execute("cat .").error, "cat rejects directories");
    check(core.execute("cat absent.txt").error, "missing file");
    check(core.execute("cd /missing/path").error, "missing directory");
    check(core.execute("cd /var/../home/guest/./documents").error == false, "absolute path normalization");
    core.execute("cd ../../../../..");
    check(core.currentPath() == "/", "cannot navigate above root");
    core.execute("cd");
    check(core.currentPath() == "/home/guest", "cd without argument returns home");
    core.execute("cd /var/log");
    core.execute("cd -");
    check(core.currentPath() == "/home/guest", "previous directory");
    check(core.execute("cat ~/notes.txt").text.contains("Welcome back"), "tilde file path");
    check(core.execute("echo \"\"").text.isEmpty(), "empty quoted argument");
    check(core.execute("cat \"notes.txt").error, "unclosed quote rejected");
    check(core.execute("cat notes.txt\\").error, "incomplete escape rejected");
    check(core.execute("ls -z").error, "unsupported flag rejected");
    check(core.execute("cd a b").error, "extra argument rejected");
    check(core.execute("whoami").text == "guest", "current user");
    check(core.execute("unknown").error, "unknown command");
    check(core.execute("clear").clear, "clear action");
    check(core.complete("pw").replacement == "pwd ", "command completion");
    check(core.complete("cd doc").replacement == "cd documents/", "directory completion");
    check(core.complete("cat /var/log/bo").replacement == "cat /var/log/boot.log ", "absolute file completion");
    const auto completed = core.complete("cat documents/quick").replacement;
    check(!core.execute(completed).error, "completed path with spaces executes");
    check(core.complete("cd ").candidates.size() == 2, "cd completion filters files and hidden entries");
    check(core.initializationError().isEmpty(), "world initializes from embedded resources");
    check(QFileInfo::exists(world + "/home/guest/notes.txt"), "seed becomes a real file");
    check(!core.execute("mkdir journals").error, "create directory");
    check(core.execute("mkdir journals").error, "mkdir rejects existing directory");
    check(!core.execute("mkdir -p journals/nested/deep").error, "mkdir creates parents");
    check(!core.execute("mkdir -p journals/nested/deep").error, "mkdir -p accepts existing directory");
    check(!core.execute("touch journals/todo.txt").error, "create empty file");
    check(core.execute("cat journals/todo.txt").text.isEmpty(), "new file is empty");
    check(!core.execute("echo first > journals/todo.txt").error, "overwrite redirection");
    check(!core.execute("echo second >> journals/todo.txt").error, "append redirection");
    check(core.execute("cat journals/todo.txt").text == "first\nsecond\n", "append retains existing bytes");
    check(!core.execute("touch journals/todo.txt").error, "touch existing file");
    check(core.execute("cat journals/todo.txt").text == "first\nsecond\n", "touch does not truncate");
    check(!core.execute("echo replaced>journals/todo.txt").error, "redirection without spaces");
    check(core.execute("cat journals/todo.txt").text == "replaced\n", "overwrite replaces previous content");
    check(core.execute("echo \"a > b\"").text == "a > b", "quoted operator remains text");
    check(core.execute("echo \\>").text == ">", "escaped operator remains text");
    check(!core.execute("echo 'quoted >> text' > 'journals/a b.txt'").error, "quoted destination and literal operators");
    check(core.execute("cat 'journals/a b.txt'").text == "quoted >> text\n", "quoted text saved");
    check(core.execute("echo hello >").error, "missing redirection destination");
    check(core.execute("echo a > x > y").error, "multiple redirections rejected");
    check(!core.execute("cat notes.txt > journals/saved.txt").error, "cat supports output redirection");
    check(core.execute("cat journals/saved.txt").text == core.execute("cat notes.txt").text, "cat redirection preserves exact bytes");
    check(!core.execute("pwd>journals/path.txt").error, "general output redirection without spaces");
    check(core.execute("cat journals/path.txt").text == "/home/guest\n", "line-oriented commands end stream output with newline");
    check(!core.execute("ls documents > journals/list.txt").error, "ls redirects to a file");
    check(core.execute("cat journals/list.txt").text == "quick start.txt\nwelcome.txt\n", "piped and redirected ls uses one name per line");
    core.saveFile("journals/search.txt", QString::fromUtf8("warning one\n普通信息\nWARNING two\nwarning.three"));
    check(core.execute("cat journals/search.txt|grep -Fin warning").output
        == "1:warning one\n3:WARNING two\n4:warning.three\n", "pipe accepts combined grep flags and preserves line numbers");
    check(core.execute("grep -F . journals/search.txt").output == "warning.three\n", "grep -F treats regex metacharacters literally");
    check(core.execute("cat journals/search.txt | grep -F warning | grep -Fn three").output == "2:warning.three\n", "multiple pipeline stages filter successive input");
    check(!core.execute("grep -Fn warning < journals/search.txt > journals/matches.txt").error, "input and output redirection compose");
    check(core.execute("cat journals/matches.txt").text == "1:warning one\n4:warning.three\n", "grep redirection saves exact matching lines");
    const auto absentMatch = core.execute("echo value | grep -F absent");
    check(absentMatch.exitCode == 1 && !absentMatch.error && absentMatch.diagnostics.isEmpty() && absentMatch.output.isEmpty(), "grep no match is status 1 with no error message");
    const auto readError = core.execute("cat /absent | grep -F absent > journals/errors.txt");
    check(readError.error && readError.diagnostics.contains("no such file") && readError.output.isEmpty(), "pipeline retains earlier diagnostics");
    check(core.execute("cat journals/errors.txt").text.isEmpty(), "stderr is not piped or redirected into stdout");
    core.execute("echo original > journals/truncate.txt");
    check(core.execute("cat /absent > journals/truncate.txt").error
        && core.execute("cat journals/truncate.txt").text.isEmpty(), "shell opens and truncates output before command failure");
    core.saveFile("journals/raw.txt", "without newline");
    core.execute("cat journals/raw.txt >> journals/raw-copy.txt");
    core.execute("echo suffix >> journals/raw-copy.txt");
    check(core.execute("cat journals/raw-copy.txt").text == "without newlinesuffix\n", "append never invents a separator newline");
    core.execute("cat journals/raw-copy.txt > journals/raw-copy.txt");
    check(core.execute("cat journals/raw-copy.txt").text.isEmpty(), "same input/output file is truncated before reading");
    check(core.execute("echo 'a|b<c>d' | grep -F '|'").output == "a|b<c>d\n", "quoted shell operators survive pipelines");
    check(core.execute("echo a\\|b\\>c").text == "a|b>c", "escaped operators remain text");
    check(!core.execute("echo '123'>journals/numeric.txt").error
        && core.execute("cat journals/numeric.txt").text == "123\n", "quoted numeric arguments are not mistaken for file descriptors");
    check(core.execute("echo hi 2>journals/stderr.txt").error
        && !QFileInfo::exists(world + "/home/guest/journals/stderr.txt"), "unsupported descriptor redirection is explicit and has no side effects");
    check(core.execute("echo once | cat - -").output == "once\n", "stdin is consumed only once across repeated cat operands");
    core.saveFile("journals/bom.txt", QString(QChar(0xfeff)) + "one\r\ntwo\r\n");
    core.execute("cat journals/bom.txt > journals/bom-copy.txt");
    check(core.execute("cat journals/bom-copy.txt").text == QString(QChar(0xfeff)) + "one\r\ntwo\r\n", "text redirection preserves UTF-8 BOM and CRLF");
    const auto partialRead = core.execute("cat journals/raw.txt /absent | grep -F without");
    check(partialRead.output == "without newline\n" && partialRead.diagnostics.contains("no such file")
        && partialRead.exitCode == 0, "partial stdout continues through pipe while diagnostics and final status stay separate");
    for (const QString &syntax : {"| pwd", "pwd |", "pwd || whoami", "echo x && pwd", "pwd >", "pwd > > x", "echo x; pwd"})
        check(core.execute(syntax).error, "invalid or unsupported shell syntax is rejected");
    check(core.execute("cd / | cat").error && core.currentPath() == "/home/guest", "pipeline cannot mutate parent directory");
    check(core.execute("browser > journals/browser.txt").error && !QFileInfo::exists(world + "/home/guest/journals/browser.txt"), "interactive redirection is rejected before opening target");
    const auto page = core.execute("cat journals/search.txt | less");
    check(page.openPager && page.output.startsWith("warning one") && page.pagerTitle == "(standard input)", "less pages piped input");
    check(!core.execute("less journals/search.txt > journals/paged.txt").openPager
        && core.execute("cat journals/paged.txt").text == core.execute("cat journals/search.txt").text, "redirected less emits text without opening a pager");
    const auto recursive = core.execute(QString::fromUtf8("grep -rFn 普通信息 journals"));
    check(recursive.output.contains(QString::fromUtf8("journals/search.txt:2:普通信息")), "recursive grep searches UTF-8 content with filename and line number");
    check(core.execute("grep -F x /absent").exitCode == 2, "grep file errors have status 2");
    check(core.execute("grep warning journals/search.txt").error, "unsupported regex mode has explicit feedback");
    check(core.execute("cat < /absent > journals/no-create.txt").error
        && !QFileInfo::exists(world + "/home/guest/journals/no-create.txt"), "failed earlier input redirection prevents later output creation");
    check(core.complete("cat \"documents/quick").replacement == "cat \"documents/quick start.txt\" ", "open double quote completes and closes");
    check(!core.execute(core.complete("cat 'documents/quick'").replacement).error, "closed single quoted completion executes");
    check(core.complete("cat documents/quick\\ s").replacement == "cat documents/quick\\ start.txt ", "escaped path can be completed repeatedly");
    const auto middle = core.complete("cat documents/quic > journals/out.txt", 18);
    check(middle.replacement == "cat documents/quick\\ start.txt > journals/out.txt"
        && middle.cursor == 30, "completion replaces token under cursor and preserves trailing arguments");
    check(core.complete("echo hello|gre").replacement == "echo hello|grep ", "command completes after a pipe");
    check(core.complete("echo hi > journals/sear").replacement == "echo hi > journals/search.txt ", "completion recognizes redirection destination");
    core.saveFile("documents/it's here.txt", "quoted name");
    const auto apostrophe = core.complete("cat 'documents/it");
    check(core.execute(apostrophe.replacement).text == "quoted name", "completion safely quotes apostrophes inside single quoted paths");
    const auto opened = core.execute("edit journals/new.txt");
    check(!opened.error && opened.editPath == "/home/guest/journals/new.txt", "editor opens new file");
    check(!QFileInfo::exists(world + opened.editPath), "opening editor does not create a file");
    const QString edited = QString::fromUtf8("A journal.\n今天开始记录。\n");
    check(!core.saveFile(opened.editPath, edited).error, "editor saves UTF-8");
    check(core.execute("cat journals/new.txt").text == edited, "read edited file");
    QFile physical(world + opened.editPath);
    check(physical.open(QIODevice::ReadOnly) && physical.readAll() == edited.toUtf8(), "content exists on disk");
    physical.close();
    check(physical.open(QIODevice::WriteOnly | QIODevice::Truncate), "open file outside terminal");
    physical.write("external edit\n");
    physical.close();
    check(core.execute("cat journals/new.txt").text == "external edit\n", "terminal observes external edits");
    check(!core.execute("cp journals/todo.txt journals/copy.txt").error, "copy file");
    check(core.execute("cp journals/new.txt journals/copy.txt").error, "copy refuses overwrite");
    check(!core.execute("cp journals/todo.txt projects").error, "copy into a directory");
    check(core.execute("cat projects/todo.txt").text == "replaced\n", "directory destination keeps source filename");
    check(!core.execute("mv journals/copy.txt journals/renamed.txt").error, "rename file");
    check(core.execute("cat journals/copy.txt").error, "moved source is gone");
    check(core.execute("cat journals/renamed.txt").text == "replaced\n", "moved destination retains content");
    check(core.execute("mv journals/new.txt journals/renamed.txt").error, "move refuses overwrite");
    check(core.execute("cp journals backup").error, "directory copy requires -r");
    check(!core.execute("cp -r journals backup").error, "recursive copy");
    check(QFileInfo(world + "/home/guest/backup/nested/deep").isDir(), "recursive copy includes nested directories");
    check(core.execute("cat backup/todo.txt").text == "replaced\n", "recursive copy includes content");
    check(core.execute("cp -r journals journals/nested/copy").error, "reject copy into own descendant");
    check(core.execute("mv journals journals/nested/moved").error, "reject move into own descendant");
    check(!core.execute("mv backup archive").error, "move directory");
    check(core.execute("cat archive/todo.txt").text == "replaced\n", "directory move retains descendants");
    check(core.execute("rm archive").error, "directory deletion requires -r");
    check(!core.execute("rm -r archive").error, "recursive deletion");
    check(!QFileInfo::exists(world + "/home/guest/archive"), "deleted directory removed from disk");
    check(!core.execute("rm journals/renamed.txt").error, "delete file");
    check(core.execute("cat journals/renamed.txt").error, "deleted file unreadable");
    check(core.execute("rm -r /").error, "root directory protected");
    check(core.execute("rm -r /home").error, "current directory parent protected");
    check(core.execute("mv /home /var/home").error, "current directory parent cannot move");
    check(core.execute("echo x > /home/guest/journals").error, "cannot redirect into directory");
    check(core.execute("touch absent/file.txt").error, "missing file parent rejected");
    check(core.execute("edit absent/file.txt").error, "missing editor parent rejected");
    check(core.execute("edit journals").error, "editor rejects directories");
    check(core.execute("touch 'bad:name.txt'").error, "host drive and alternate-stream syntax rejected");
    check(core.execute("touch 'trailing.'").error, "Windows filename aliases rejected");
    check(core.execute("touch CON.txt").error, "device names rejected");
    QFile invalidUtf8(world + "/home/guest/invalid.txt");
    check(invalidUtf8.open(QIODevice::WriteOnly), "create non-UTF-8 fixture");
    invalidUtf8.write(QByteArray::fromHex("fffe61"));
    invalidUtf8.close();
    check(core.execute("edit invalid.txt").error, "editor rejects non-UTF-8 without changing bytes");
    core.execute("rm invalid.txt");
    check(core.execute("mkdir -p journals/todo.txt/child").error, "file cannot become parent directory");
    check(!core.execute("echo confined > /../../confined.txt").error, "parent traversal stays inside virtual root");
    check(QFileInfo::exists(world + "/confined.txt") && !QFileInfo::exists(temporary.path() + "/confined.txt"), "writes cannot traverse out of world");
    const QString outsidePath = temporary.path() + "/outside.txt";
    QFile outside(outsidePath);
    check(outside.open(QIODevice::WriteOnly), "create external sentinel");
    outside.write("outside remains unchanged");
    outside.close();
    const QString link = world + "/home/guest/outside-link.lnk";
    if (QFile::link(outsidePath, link)) {
        check(core.execute("cat outside-link.lnk").error, "read through symlink rejected");
        check(core.execute("echo changed > outside-link.lnk").error, "write through symlink rejected");
        check(core.execute("rm outside-link.lnk").error, "delete through symlink rejected");
        check(core.execute("echo changed | cat > outside-link.lnk").error, "pipeline output cannot follow a link outside the world");
        check(!core.execute("grep -rF 'outside remains unchanged' .").output.contains("outside remains unchanged"), "recursive grep skips links outside the world");
        check(outside.open(QIODevice::ReadOnly) && outside.readAll() == "outside remains unchanged", "external sentinel unchanged");
        outside.close();
        QFile::remove(link);
    } else qInfo() << "Symlink creation unavailable on this host; symlink checks skipped.";
    check(!core.execute("rm notes.txt").error, "delete an initial file");
    check(!core.execute("cd journals").error, "change directory before reload");
    TerminalCore restored(world);
    check(restored.currentPath() == "/home/guest/journals", "current directory persists");
    check(restored.execute("cat new.txt").text == "external edit\n", "user contents survive reload");
    check(restored.execute("cat ../notes.txt").error, "deleted initial file is not recreated");
    QFile initial(":/initial-world/home/guest/notes.txt");
    check(initial.open(QIODevice::ReadOnly) && initial.readAll().contains("Welcome back"), "initial template remains unchanged");
    const auto editExisting = restored.execute("edit new.txt");
    check(editExisting.editContent == "external edit\n", "editor loads existing contents");
    restored.execute("cd /");
    check(!restored.execute("rm -r /home").error, "delete home after leaving it");
    TerminalCore withoutHome(world);
    check(withoutHome.currentPath() == "/" && withoutHome.execute("cd ~").error, "missing home stays deleted after reload");
    {
        const QString historyWorld = temporary.path() + "/history-runtime/world";
        TerminalCore history(historyWorld);
        history.execute("echo remembered");
        history.execute("echo remembered");
        history.execute("   ");
        check(history.history() == QStringList{"echo remembered"}, "history ignores empty lines and consecutive duplicates");
        TerminalCore reopened(historyWorld);
        check(reopened.history() == history.history(), "history persists independently from current directory");
        check(reopened.execute("history 1").output.contains("history 1"), "history includes current command and accepts a count");
        check(reopened.execute("history -c").output.isEmpty() && reopened.history().isEmpty(), "history can be cleared explicitly");
        for (int i = 0; i < 1003; ++i) reopened.execute("echo " + QString::number(i));
        TerminalCore bounded(historyWorld);
        check(bounded.history().size() == 1000 && bounded.history().first() == "echo 3"
            && bounded.history().last() == "echo 1002", "history retains the most recent 1000 commands on disk");
        QFile historyFile(temporary.path() + "/history-runtime/history.json");
        check(historyFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "open corrupt history fixture");
        historyFile.write("not json"); historyFile.close();
        TerminalCore damaged(historyWorld);
        check(!damaged.historyWarning().isEmpty() && !damaged.execute("echo still usable").error, "corrupt history does not prevent terminal use");
        check(historyFile.open(QIODevice::ReadOnly) && historyFile.readAll() == "not json", "corrupt history is preserved until explicit reset");
        historyFile.close();
        damaged.execute("history -c");
        check(damaged.historyWarning().isEmpty() && damaged.history().isEmpty(), "explicit clear repairs history storage");
    }
    std::fprintf(stderr, "%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
