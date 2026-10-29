#include "terminalcore.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <limits>

namespace {
CommandResult errorResult(const QString &message) {
    CommandResult result{message, true};
    result.exitCode = 1;
    return result;
}
bool nonnegative(const QString &text, int &value) {
    static const QRegularExpression number("^[0-9]+$");
    bool valid = false;
    if (!number.match(text).hasMatch()) return false;
    value = text.toInt(&valid);
    return valid;
}
}

CommandResult TerminalCore::findFiles(QStringList args) const {
    const QString usage = "usage: find [path] [-name pattern] [-iname pattern] [-type f|d] [-maxdepth n]";
    if (args == QStringList{"--help"}) return {usage};
    QString root = ".";
    if (!args.isEmpty() && !args.first().startsWith('-')) root = args.takeFirst();
    if (root.isEmpty()) return errorResult("find: empty starting path");
    if (root == "~" || root.startsWith("~/")) root = resolve(root);
    while (root.size() > 1 && root.endsWith('/')) root.chop(1);
    QList<QRegularExpression> patterns;
    QList<QChar> types;
    int maxDepth = std::numeric_limits<int>::max();
    while (!args.isEmpty()) {
        const QString option = args.takeFirst();
        if (option != "-name" && option != "-iname" && option != "-type" && option != "-maxdepth")
            return errorResult("find: unsupported expression " + option + "; " + usage);
        if (args.isEmpty()) return errorResult("find: missing argument for " + option);
        const QString value = args.takeFirst();
        if (option == "-name" || option == "-iname") {
            QRegularExpression pattern(QRegularExpression::wildcardToRegularExpression(value),
                option == "-iname" ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);
            if (!pattern.isValid()) return errorResult("find: invalid filename pattern " + value);
            patterns.append(pattern);
        } else if (option == "-type") {
            if (value != "f" && value != "d") return errorResult("find: supported types are f (file) and d (directory)");
            types.append(value.at(0));
        } else if (!nonnegative(value, maxDepth)) return errorResult("find: -maxdepth requires a nonnegative integer");
    }
    struct Pending { QString path; int depth; };
    QList<Pending> pending{{root, 0}};
    CommandResult result;
    result.output = QStringLiteral("");
    while (!pending.isEmpty()) {
        const auto entry = pending.takeLast();
        QString error;
        const QString native = physicalPath(resolve(entry.path), error);
        const QFileInfo info(native);
        if (error.isEmpty() && !info.exists()) error = "no such file or directory";
        if (!error.isEmpty()) { result.diagnostics += "find: " + entry.path + ": " + error + '\n'; continue; }
        const bool directory = info.isDir();
        const QString name = entry.path == "/" ? "/" : entry.path.section('/', -1);
        bool matches = true;
        for (const auto &pattern : patterns) if (!pattern.match(name).hasMatch()) matches = false;
        for (const auto type : types) if ((type == 'd') != directory) matches = false;
        if (matches) result.output += entry.path + '\n';
        if (!directory || entry.depth >= maxDepth) continue;
        if (!info.isReadable()) { result.diagnostics += "find: " + entry.path + ": permission denied\n"; continue; }
        const auto children = QDir(native).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
        // Stack traversal keeps parent-before-child output and stable sibling order.
        for (auto child = children.crbegin(); child != children.crend(); ++child) {
            if (child->isSymLink() || child->isJunction()) continue;
            pending.append({entry.path + (entry.path.endsWith('/') ? "" : "/") + child->fileName(), entry.depth + 1});
        }
    }
    result.text = result.output;
    result.error = !result.diagnostics.isEmpty();
    result.exitCode = result.error ? 1 : 0;
    return result;
}

CommandResult TerminalCore::readLines(const QString &command, QStringList args,
    const QString &input, bool hasInput) const {
    const QString usage = "usage: " + command + " [-n count] [file ...] (or command | " + command + ")";
    if (args == QStringList{"--help"}) return {usage};
    QStringList files;
    int count = 10;
    bool literal = false;
    for (int i = 0; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (!literal && arg == "--") { literal = true; continue; }
        if (!literal && (arg == "-n" || arg.startsWith("-n"))) {
            QString value = arg.mid(2);
            if (value.isEmpty() && i + 1 < args.size()) value = args.at(++i);
            if (!nonnegative(value, count)) return errorResult(command + ": -n requires a nonnegative integer");
        } else if (!literal && arg.startsWith('-') && arg != "-") {
            return errorResult(command + ": unsupported option " + arg + "; " + usage);
        } else files.append(arg);
    }
    if (files.isEmpty()) files.append("-");
    CommandResult result;
    result.output = QStringLiteral("");
    QString remaining = input;
    bool printed = false;
    for (const auto &file : files) {
        const auto read = file == "-"
            ? (hasInput ? CommandResult{remaining} : errorResult("provide a file, a pipe, or '< file'"))
            : readFile(resolve(file));
        if (read.error) { result.diagnostics += command + ": " + read.text + '\n'; continue; }
        const QString &text = read.text;
        int offset = 0;
        int lines = count;
        if (command == "tail") {
            const int total = text.count('\n') + (!text.isEmpty() && !text.endsWith('\n') ? 1 : 0);
            lines = qMax(0, total - count);
        }
        for (int line = 0; line < lines && offset < text.size(); ++line) {
            const auto newline = text.indexOf('\n', offset);
            offset = newline < 0 ? text.size() : newline + 1;
        }
        if (files.size() > 1) {
            if (printed) result.output += '\n';
            result.output += "==> " + (file == "-" ? "standard input" : file) + " <==\n";
        }
        result.output += command == "head" ? text.left(offset) : text.mid(offset);
        if (file == "-") remaining = command == "head" ? remaining.mid(offset) : QString();
        printed = true;
    }
    result.text = result.output;
    result.error = !result.diagnostics.isEmpty();
    result.exitCode = result.error ? 1 : 0;
    return result;
}
