#include "terminalcore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringConverter>
#include <QTemporaryDir>
#include <algorithm>

namespace {
const QStringList commands = {"cat", "cd", "clear", "cp", "echo", "edit", "help", "ls",
    "mkdir", "mv", "pwd", "rm", "touch", "vim", "whoami"};
CommandResult fail(const QString &message) { return {message, true, false}; }
#ifdef Q_OS_WIN
constexpr auto pathCase = Qt::CaseInsensitive;
#else
constexpr auto pathCase = Qt::CaseSensitive;
#endif
bool within(const QString &path, const QString &root) {
    return path.compare(root, pathCase) == 0 || path.startsWith(root + '/', pathCase);
}
bool isLink(const QFileInfo &info) {
    return info.isSymLink() || info.isJunction();
}
QString invalidName(const QString &part) {
    static const QRegularExpression reserved(
        "^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$", QRegularExpression::CaseInsensitiveOption);
    if (part.endsWith('.') || part.endsWith(' ') || reserved.match(part).hasMatch())
        return "unsupported filename: " + part;
    for (const QChar ch : part)
        if (ch.unicode() < 32 || QStringLiteral("\\:*?\"<>|").contains(ch))
            return "unsupported character in filename";
    return {};
}
}

QString TerminalCore::defaultWorldRoot() {
    QDir candidate(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 7; ++depth) {
        if (QFileInfo::exists(candidate.filePath("CMakeLists.txt"))
            && QDir(candidate.filePath("assets/initial-world")).exists())
            return candidate.filePath("runtime/world");
        if (!candidate.cdUp()) break;
    }
    QDir portable(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    // A portable macOS world's data lives alongside the .app, outside its bundle.
    if (portable.dirName() == "MacOS") portable.cd("../../..");
#endif
    return portable.filePath("runtime/world");
}

TerminalCore::TerminalCore(const QString &worldRoot) {
    static const bool resourcesReady = [] { Q_INIT_RESOURCE(initial_world); return true; }();
    Q_UNUSED(resourcesReady);
    m_worldRoot = QDir::cleanPath(QFileInfo(worldRoot.isEmpty() ? defaultWorldRoot() : worldRoot).absoluteFilePath());
    m_statePath = QFileInfo(m_worldRoot).dir().filePath("session.json");
    initialize();
}

void TerminalCore::initialize() {
    const QFileInfo rootInfo(m_worldRoot);
    if (isLink(rootInfo)) {
        m_initializationError = "workspace: the world root cannot be a symbolic link or junction";
        return;
    }
    if (!rootInfo.exists()) {
        const QString parent = rootInfo.absolutePath();
        if (!QDir().mkpath(parent)) {
            m_initializationError = "workspace: cannot create runtime directory";
            return;
        }
        QTemporaryDir staged(parent + "/.world-init-XXXXXX");
        if (!staged.isValid()) {
            m_initializationError = "workspace: cannot initialize files";
            return;
        }
        const QString prefix = ":/initial-world/";
        QDirIterator entries(":/initial-world", QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (entries.hasNext()) {
            const QString resource = entries.next();
            const QString destination = staged.path() + '/' + resource.mid(prefix.size());
            if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
                m_initializationError = "workspace: cannot initialize directory";
                return;
            }
            QFile input(resource);
            QSaveFile output(destination);
            if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
                m_initializationError = "workspace: cannot initialize file";
                return;
            }
            const QByteArray data = input.readAll();
            if (input.error() != QFileDevice::NoError || output.write(data) != data.size() || !output.commit()) {
                m_initializationError = "workspace: cannot write initial file";
                return;
            }
        }
        if (!QDir().rename(staged.path(), m_worldRoot)) {
            m_initializationError = "workspace: cannot finish initialization";
            return;
        }
        staged.setAutoRemove(false);
    }
    if (!QFileInfo(m_worldRoot).isDir()) {
        m_initializationError = "workspace: world path is not a directory";
        return;
    }
    m_worldRoot = QFileInfo(m_worldRoot).canonicalFilePath();
    m_statePath = QFileInfo(m_worldRoot).dir().filePath("session.json");
    QString error;
    QString home = physicalPath(m_home, error);
    if (error.isEmpty() && QFileInfo(home).isDir()) m_currentPath = m_home;
    else m_currentPath = "/";
    QFile state(m_statePath);
    if (!isLink(QFileInfo(m_statePath)) && state.open(QIODevice::ReadOnly)) {
        const auto object = QJsonDocument::fromJson(state.readAll()).object();
        const QString saved = object.value("currentPath").toString();
        if (saved.startsWith('/')) {
            error.clear();
            const QString path = physicalPath(resolve(saved), error);
            if (error.isEmpty() && QFileInfo(path).isDir()) m_currentPath = resolve(saved);
        }
    }
    m_previousPath = m_currentPath;
}

QString TerminalCore::resolve(const QString &path) const {
    QString expanded = path;
    if (expanded == "~") expanded = m_home;
    else if (expanded.startsWith("~/")) expanded = m_home + expanded.mid(1);
    if (!expanded.startsWith('/')) expanded = m_currentPath + '/' + expanded;
    QStringList parts;
    for (const auto &part : expanded.split('/', Qt::SkipEmptyParts)) {
        if (part == ".") continue;
        if (part == "..") { if (!parts.isEmpty()) parts.removeLast(); }
        else parts.append(part);
    }
    return '/' + parts.join('/');
}

QString TerminalCore::physicalPath(const QString &path, QString &error) const {
    if (!m_initializationError.isEmpty()) { error = m_initializationError; return {}; }
    const QFileInfo root(m_worldRoot);
    if (!root.isDir() || isLink(root)
        || root.canonicalFilePath().compare(m_worldRoot, pathCase) != 0) {
        error = "workspace: world directory is missing or has changed";
        return {};
    }
    QString native = m_worldRoot;
    for (const auto &part : path.split('/', Qt::SkipEmptyParts)) {
        error = invalidName(part);
        if (!error.isEmpty()) return {};
        native += '/' + part;
        const QFileInfo info(native);
        if (isLink(info)) {
            error = "symbolic links and junctions are not supported: " + path;
            return {};
        }
        if (info.exists()) {
            const QString canonical = info.canonicalFilePath();
            if (canonical.isEmpty() || !within(canonical, m_worldRoot)
                || (!info.isFile() && !info.isDir())) {
                error = "unsupported path: " + path;
                return {};
            }
        }
    }
    return native;
}

QString TerminalCore::validateTree(const QString &path) const {
    QString error;
    const QString native = physicalPath(path, error);
    if (!error.isEmpty()) return error;
    if (!QFileInfo(native).isDir()) return {};
    QDirIterator entries(native, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        entries.next();
        const auto info = entries.fileInfo();
        if (isLink(info)) return "directory contains a symbolic link or junction";
        if (!info.isDir() && !info.isFile()) return "directory contains an unsupported file type";
        error = invalidName(info.fileName());
        if (!error.isEmpty()) return error;
        if (!within(info.canonicalFilePath(), m_worldRoot)) return "directory contains an unsupported path";
    }
    return {};
}

void TerminalCore::saveSession() const {
    if (isLink(QFileInfo(m_statePath))) return;
    QSaveFile file(m_statePath);
    if (!file.open(QIODevice::WriteOnly)) return;
    const QByteArray data = QJsonDocument(QJsonObject{{"currentPath", m_currentPath}}).toJson();
    if (file.write(data) == data.size()) file.commit();
}

QStringList TerminalCore::children(const QString &directory, bool hidden) const {
    QString error;
    const QString native = physicalPath(directory, error);
    if (!error.isEmpty()) return {};
    auto filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
    if (hidden) filters |= QDir::Hidden;
    QStringList result;
    for (const auto &info : QDir(native).entryInfoList(filters, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase)) {
        if (!hidden && info.fileName().startsWith('.')) continue;
        if (isLink(info)) continue;
        result.append(info.fileName() + (info.isDir() ? "/" : ""));
    }
    return result;
}

QList<TerminalCore::Token> TerminalCore::tokenize(const QString &line, QString &error) {
    QList<Token> tokens;
    QString token;
    QChar quote;
    bool started = false;
    bool escaped = false;
    const auto flush = [&] {
        if (started) { tokens.append({token, false}); token.clear(); started = false; }
    };
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (escaped) { token += ch; started = true; escaped = false; continue; }
        if (ch == '\\' && quote != '\'') { escaped = true; started = true; continue; }
        if (!quote.isNull()) {
            if (ch == quote) quote = QChar();
            else token += ch;
        } else if (ch == '\'' || ch == '"') { quote = ch; started = true; }
        else if (ch.isSpace()) flush();
        else if (ch == '>') {
            flush();
            QString op = ">";
            if (i + 1 < line.size() && line.at(i + 1) == '>') { op = ">>"; ++i; }
            tokens.append({op, true});
        } else { token += ch; started = true; }
    }
    if (escaped) { error = "parse: incomplete escape at end of input"; return {}; }
    if (!quote.isNull()) { error = "parse: unclosed quote"; return {}; }
    flush();
    return tokens;
}

QString TerminalCore::displayPath() const {
    if (m_currentPath == m_home) return "~";
    if (m_currentPath.startsWith(m_home + '/')) return '~' + m_currentPath.mid(m_home.size());
    return m_currentPath;
}

QString TerminalCore::prompt() const { return "guest@silly:" + displayPath() + "$ "; }

CommandResult TerminalCore::readFile(const QString &path) const {
    QString error;
    const QString native = physicalPath(path, error);
    if (!error.isEmpty()) return fail(error);
    if (!QFileInfo::exists(native)) return fail(path + ": no such file or directory");
    if (QFileInfo(native).isDir()) return fail(path + ": is a directory");
    QFile file(native);
    if (!file.open(QIODevice::ReadOnly)) return fail(path + ": " + file.errorString());
    const QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) return fail(path + ": " + file.errorString());
    if (data.contains('\0')) return fail(path + ": not a text file");
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(data);
    if (utf8.hasError()) return fail(path + ": file is not UTF-8 text");
    return {text};
}

CommandResult TerminalCore::writeFile(const QString &path, const QString &content, bool append) {
    QString error;
    const QString native = physicalPath(path, error);
    if (!error.isEmpty()) return fail(error);
    if (QFileInfo(native).isDir()) return fail(path + ": is a directory");
    if (!QFileInfo(QFileInfo(native).absolutePath()).isDir()) return fail(path + ": parent directory does not exist");
    QByteArray data;
    if (append && QFileInfo::exists(native)) {
        QFile previous(native);
        if (!previous.open(QIODevice::ReadOnly)) return fail(path + ": " + previous.errorString());
        data = previous.readAll();
        if (previous.error() != QFileDevice::NoError) return fail(path + ": " + previous.errorString());
    }
    data += content.toUtf8();
    QSaveFile file(native);
    if (!file.open(QIODevice::WriteOnly)) return fail(path + ": " + file.errorString());
    if (file.write(data) != data.size() || !file.commit()) return fail(path + ": could not save file");
    return {};
}

CommandResult TerminalCore::saveFile(const QString &path, const QString &content) {
    return writeFile(resolve(path), content, false);
}

CommandResult TerminalCore::transfer(const QString &command, const QStringList &input) {
    QStringList args = input;
    bool recursive = false;
    if (command == "cp" && !args.isEmpty() && args.first() == "-r") { recursive = true; args.removeFirst(); }
    if (!args.isEmpty() && args.first() == "--") args.removeFirst();
    if (args.size() != 2) return fail("usage: " + command + (command == "cp" ? " [-r]" : "") + " <source> <destination>");
    const QString source = resolve(args.at(0));
    QString target = resolve(args.at(1));
    QString error;
    const QString src = physicalPath(source, error);
    if (!error.isEmpty()) return fail(command + ": " + error);
    if (!QFileInfo::exists(src)) return fail(command + ": source does not exist");
    if (source == "/") return fail(command + ": cannot copy or move the root directory");
    QString dst = physicalPath(target, error);
    if (!error.isEmpty()) return fail(command + ": " + error);
    if (QFileInfo(dst).isDir()) {
        target = resolve(target + '/' + source.section('/', -1));
        dst = physicalPath(target, error);
        if (!error.isEmpty()) return fail(command + ": " + error);
    }
    if (within(dst, src)) return fail(command + ": destination cannot be the source or inside it");
    if (QFileInfo::exists(dst)) return fail(command + ": destination already exists");
    if (!QFileInfo(QFileInfo(dst).absolutePath()).isDir()) return fail(command + ": destination parent does not exist");
    if (command == "mv") {
        const QString current = physicalPath(m_currentPath, error);
        if (within(current, src)) return fail("mv: cannot move the current directory or its parent");
    }
    const bool directory = QFileInfo(src).isDir();
    if (directory && command == "cp" && !recursive) return fail("cp: use cp -r to copy a directory");
    error = validateTree(source);
    if (!error.isEmpty()) return fail(command + ": " + error);
    if (command == "mv") {
        if (!QDir().rename(src, dst)) return fail("mv: unable to move source");
        return {};
    }
    if (!directory) return QFile::copy(src, dst) ? CommandResult{} : fail("cp: unable to copy file");
    if (!QDir().mkdir(dst)) return fail("cp: unable to create destination");
    QDirIterator entries(src, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        entries.next();
        const auto info = entries.fileInfo();
        const QString copy = dst + '/' + QDir(src).relativeFilePath(info.filePath());
        if (info.isDir()) {
            if (!QDir().mkpath(copy)) return fail("cp: unable to create a destination directory");
        } else if (!QFile::copy(info.filePath(), copy)) return fail("cp: unable to copy a file");
    }
    return {};
}

CommandResult TerminalCore::execute(const QString &line) {
    if (!m_initializationError.isEmpty()) return fail(m_initializationError);
    QString parseError;
    auto tokens = tokenize(line, parseError);
    if (!parseError.isEmpty()) return fail(parseError);
    if (tokens.isEmpty()) return {};
    const auto first = tokens.takeFirst();
    if (first.redirection) return fail("parse: expected a command before redirection");
    const QString command = first.text;
    if (!commands.contains(command)) return fail(command + ": command not found. Type 'help' for commands.");
    int redirect = -1;
    QStringList args;
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i).redirection) {
            if (redirect != -1) return fail("parse: only one output redirection is supported");
            redirect = i;
        }
        args.append(tokens.at(i).text);
    }
    if (redirect >= 0) {
        if (command != "echo") return fail("redirection is currently supported for echo only");
        if (redirect != tokens.size() - 2 || tokens.last().redirection)
            return fail("usage: echo [text] > <file> or echo [text] >> <file>");
        return writeFile(resolve(args.last()), args.mid(0, redirect).join(' ') + '\n', tokens.at(redirect).text == ">>");
    }
    if (command == "echo") return {args.join(' ')};
    if (command == "help") {
        if (!args.isEmpty()) return fail("usage: help");
        return {"AVAILABLE COMMANDS\n\n"
            "  ls [-a] [path]       List files and directories\n"
            "  cd [path]            Change directory; no path returns home\n"
            "  pwd                  Print the current directory\n"
            "  cat <file>           Read a text file\n"
            "  mkdir [-p] <path>    Create a directory\n"
            "  touch <file>         Create a file; keep existing content\n"
            "  edit <file>          Open the text editor\n"
            "  vim <file>           Edit inside the terminal (i, Esc, :wq)\n"
            "  mv <source> <dest>   Move or rename a file or directory\n"
            "  cp [-r] <src> <dst>  Copy a file; -r copies directories\n"
            "  rm [-r] <path>       Delete a file; -r deletes directories\n"
            "  echo [text]          Print text\n"
            "  echo ... > <file>    Overwrite a file with one line\n"
            "  echo ... >> <file>   Append a line to a file\n"
            "  whoami               Show the current user\n"
            "  clear                Clear the screen\n"
            "  help                 Show this help\n\n"
            "PATHS   / absolute   .. parent   ~ home   cd - previous\n"
            "KEYS    Up/Down history   Tab complete   Ctrl+L clear   F1 help\n\n"
            "Files and the current directory persist between sessions.\n"
            "cp and mv never overwrite an existing destination."};
    }
    if (command == "clear" || command == "pwd" || command == "whoami") {
        if (!args.isEmpty()) return fail("usage: " + command);
        if (command == "clear") return {{}, false, true};
        return {command == "pwd" ? m_currentPath : QStringLiteral("guest")};
    }
    if (command == "cp" || command == "mv") return transfer(command, args);
    if (command == "cd") {
        if (args.size() > 1) return fail("usage: cd [path]");
        const bool previous = !args.isEmpty() && args.first() == "-";
        const QString target = previous ? m_previousPath : resolve(args.isEmpty() ? m_home : args.first());
        QString error;
        const QString native = physicalPath(target, error);
        if (!error.isEmpty()) return fail("cd: " + error);
        if (!QFileInfo::exists(native)) return fail("cd: " + target + ": no such file or directory");
        if (!QFileInfo(native).isDir()) return fail("cd: " + target + ": not a directory");
        m_previousPath = m_currentPath;
        m_currentPath = target;
        saveSession();
        return {previous ? m_currentPath : QString()};
    }
    if (command == "ls") {
        bool hidden = false, literal = false;
        QStringList paths;
        for (const auto &arg : args) {
            if (!literal && arg == "--") { literal = true; continue; }
            if (!literal && arg == "-a") hidden = true;
            else if (!literal && arg.startsWith('-')) return fail("ls: unsupported option " + arg + ". Use ls [-a] [path].");
            else paths.append(arg);
        }
        if (paths.size() > 1) return fail("usage: ls [-a] [path]");
        const QString path = resolve(paths.isEmpty() ? "." : paths.first());
        QString error;
        const QString native = physicalPath(path, error);
        if (!error.isEmpty()) return fail("ls: " + error);
        if (!QFileInfo::exists(native)) return fail("ls: " + path + ": no such file or directory");
        if (!QFileInfo(native).isDir()) return {path.section('/', -1)};
        return {children(path, hidden).join("    ")};
    }
    if (command == "mkdir") {
        bool parents = false;
        if (!args.isEmpty() && args.first() == "-p") { parents = true; args.removeFirst(); }
        if (!args.isEmpty() && args.first() == "--") args.removeFirst();
        if (args.size() != 1) return fail("usage: mkdir [-p] <path>");
        QString error;
        const QString native = physicalPath(resolve(args.first()), error);
        if (!error.isEmpty()) return fail("mkdir: " + error);
        if (QFileInfo::exists(native)) {
            if (parents && QFileInfo(native).isDir()) return {};
            return fail("mkdir: path already exists");
        }
        if (!(parents ? QDir().mkpath(native) : QDir().mkdir(native))) return fail("mkdir: unable to create directory (check the parent path)");
        return {};
    }
    if (command == "touch") {
        if (args.size() != 1) return fail("usage: touch <file>");
        const QString path = resolve(args.first());
        QString error;
        const QString native = physicalPath(path, error);
        if (!error.isEmpty()) return fail("touch: " + error);
        if (QFileInfo(native).isDir()) return fail("touch: path is a directory");
        if (!QFileInfo::exists(native)) return writeFile(path, {}, false);
        QFile file(native);
        if (!file.open(QIODevice::ReadWrite) || !file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime))
            return fail("touch: unable to update file time");
        return {};
    }
    if (command == "rm") {
        bool recursive = false;
        if (!args.isEmpty() && args.first() == "-r") { recursive = true; args.removeFirst(); }
        if (!args.isEmpty() && args.first() == "--") args.removeFirst();
        if (args.size() != 1) return fail("usage: rm [-r] <path>");
        const QString path = resolve(args.first());
        QString error;
        const QString native = physicalPath(path, error);
        if (!error.isEmpty()) return fail("rm: " + error);
        if (!QFileInfo::exists(native)) return fail("rm: no such file or directory");
        if (path == "/") return fail("rm: cannot delete the root directory");
        const QString current = physicalPath(m_currentPath, error);
        if (within(current, native)) return fail("rm: cannot delete the current directory or its parent");
        if (QFileInfo(native).isDir()) {
            if (!recursive) return fail("rm: use rm -r to delete a directory");
            error = validateTree(path);
            if (!error.isEmpty()) return fail("rm: " + error);
            return QDir(native).removeRecursively() ? CommandResult{} : fail("rm: unable to delete directory");
        }
        return QFile::remove(native) ? CommandResult{} : fail("rm: unable to delete file");
    }
    if (args.size() != 1) return fail("usage: " + command + " <file>");
    const QString path = resolve(args.first());
    if (command == "edit" || command == "vim") {
        QString error;
        const QString native = physicalPath(path, error);
        if (!error.isEmpty()) return fail(command + ": " + error);
        if (!QFileInfo(QFileInfo(native).absolutePath()).isDir()) return fail(command + ": parent directory does not exist");
        CommandResult opened;
        if (QFileInfo::exists(native)) {
            opened = readFile(path);
            if (opened.error) return opened;
        }
        CommandResult result;
        result.editPath = path;
        result.editContent = opened.text;
        result.useVim = command == "vim";
        return result;
    }
    return readFile(path);
}

Completion TerminalCore::complete(const QString &line) const {
    Completion result{line, {}};
    if (line.contains('"') || line.contains('\'') || line.contains('\\')) return result;
    if (!line.contains(' ')) {
        for (const auto &command : commands)
            if (command.startsWith(line)) result.candidates.append(command);
        if (result.candidates.size() == 1) result.replacement = result.candidates.first() + ' ';
    } else {
        const int start = line.lastIndexOf(' ') + 1;
        const QString token = line.mid(start);
        const int slash = token.lastIndexOf('/');
        const QString prefix = slash < 0 ? QString() : token.left(slash + 1);
        const QString partial = token.mid(slash + 1);
        const QString directory = resolve(prefix.isEmpty() ? "." : prefix);
        const QString command = line.section(' ', 0, 0);
        QString error;
        const QString native = physicalPath(directory, error);
        if (!error.isEmpty() || !QFileInfo(native).isDir()) return result;
        for (const auto &entry : children(directory, partial.startsWith('.'))) {
            if (!entry.startsWith(partial)) continue;
            if ((command == "cd" || command == "mkdir") && !entry.endsWith('/')) continue;
            QString candidate = prefix + entry;
            candidate.replace(" ", "\\ ");
            result.candidates.append(candidate);
        }
        if (result.candidates.size() == 1) {
            const auto &candidate = result.candidates.first();
            result.replacement = line.left(start) + candidate + (candidate.endsWith('/') ? "" : " ");
        } else if (!result.candidates.isEmpty()) {
            QString common = result.candidates.first();
            for (const auto &candidate : result.candidates)
                while (!candidate.startsWith(common)) common.chop(1);
            if (common.size() > token.size()) result.replacement = line.left(start) + common;
        }
    }
    return result;
}
