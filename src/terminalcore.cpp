#include "terminalcore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringConverter>
#include <QTemporaryDir>
#ifdef Q_OS_WIN
#include <QThread>
#endif
#include <algorithm>
#include <functional>

namespace {
const QStringList commands = {"browser", "camera", "cat", "cd", "clear", "cp", "echo", "exit", "feh", "find", "grep", "head", "help", "history", "less", "logout", "ls",
    "mc", "mkdir", "mv", "pwd", "rm", "tail", "touch", "vim", "whoami"};
constexpr int historyLimit = 1000;
CommandResult fail(const QString &message, int status = 1) {
    CommandResult result{message, true, false};
    result.exitCode = status;
    return result;
}
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

TerminalCore::TerminalCore(const QString &worldRoot, const LocalIdentity &identity)
    : m_identity(identity), m_currentPath(identity.homePath), m_previousPath(identity.homePath), m_home(identity.homePath) {
    static const bool resourcesReady = [] { Q_INIT_RESOURCE(initial_world); return true; }();
    Q_UNUSED(resourcesReady);
    m_worldRoot = QDir::cleanPath(QFileInfo(worldRoot.isEmpty() ? defaultWorldRoot() : worldRoot).absoluteFilePath());
    m_statePath = QFileInfo(m_worldRoot).dir().filePath("session.json");
    initialize();
    if (m_initializationError.isEmpty()) loadHistory();
}

void TerminalCore::loadHistory() {
    const QString path = QFileInfo(m_worldRoot).dir().filePath("history.json");
    QFile file(path);
    if (!QFileInfo::exists(path) && !isLink(QFileInfo(path))) return;
    if (isLink(QFileInfo(path)) || !file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        m_historyWarning = "history: cannot read history.json; history will not be saved this session";
        m_historyWritable = false;
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (file.error() != QFileDevice::NoError || error.error != QJsonParseError::NoError || !document.isArray()) {
        m_historyWarning = "history: invalid history.json; existing file preserved (history -c resets it)";
        m_historyWritable = false;
        return;
    }
    const auto entries = document.array();
    for (const auto &entry : entries) {
        const QString line = entry.toString();
        if (!line.trimmed().isEmpty() && line.size() <= 16384 && !line.contains('\n') && !line.contains('\r'))
            m_history.append(line);
    }
    if (m_history.size() > historyLimit) m_history = m_history.last(historyLimit);
}

void TerminalCore::saveHistory() {
    if (!m_historyWritable) return;
    QString error;
    if (physicalPath("/", error).isEmpty()) {
        m_historyWarning = "history: cannot save because the world directory has changed";
        return;
    }
    const QString path = QFileInfo(m_worldRoot).dir().filePath("history.json");
    QSaveFile file(path);
    if (isLink(QFileInfo(path)) || !file.open(QIODevice::WriteOnly)) {
        m_historyWarning = "history: cannot save history.json";
        return;
    }
    QJsonArray entries;
    for (const auto &line : m_history) entries.append(line);
    QByteArray data = QJsonDocument(entries).toJson(QJsonDocument::Compact);
    while (data.size() > 4 * 1024 * 1024 && !m_history.isEmpty()) {
        m_history.removeFirst(); entries.removeAt(0);
        data = QJsonDocument(entries).toJson(QJsonDocument::Compact);
    }
    if (file.write(data) != data.size() || !file.commit()) m_historyWarning = "history: cannot save history.json";
    else m_historyWarning.clear();
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
        const auto publish = [&] {
            return !QFileInfo::exists(m_worldRoot) && QDir().rename(staged.path(), m_worldRoot);
        };
        bool published = publish();
#ifdef Q_OS_WIN
        // Newly written files can briefly be held open by Windows services.
        // Retry only while the destination is absent; never replace a world.
        for (const auto delay : {25ul, 50ul, 100ul, 200ul}) {
            if (published || QFileInfo::exists(m_worldRoot)) break;
            QThread::msleep(delay);
            published = publish();
        }
#endif
        if (!published) {
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

QList<TerminalCore::Token> TerminalCore::tokenize(const QString &line, QString &error, bool partial) {
    QList<Token> tokens;
    QString token;
    QChar quote;
    bool started = false;
    bool escaped = false;
    int start = 0;
    int end = 0;
    const auto flush = [&] {
        if (started) { tokens.append({token, false, start, end}); token.clear(); started = false; }
    };
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (!started) start = i;
        end = i + 1;
        if (escaped) { token += ch; started = true; escaped = false; continue; }
        if (ch == '\\' && quote != '\'') {
            // Within double quotes, only shell-special characters are escaped.
            if (quote == '"' && i + 1 < line.size() && !QStringLiteral("$`\"\\").contains(line.at(i + 1)))
                token += ch;
            else escaped = true;
            started = true;
            continue;
        }
        if (!quote.isNull()) {
            if (ch == quote) quote = QChar();
            else token += ch;
        } else if (ch == '\'' || ch == '"') { quote = ch; started = true; }
        else if (ch.isSpace()) { end = i; flush(); }
        else if (QStringLiteral("|><;&()`").contains(ch)) {
            if (!partial && QStringLiteral(";&()`").contains(ch)) {
                error = "parse: unsupported shell operator " + QString(ch); return {};
            }
            const QString raw = line.mid(start, i - start);
            const bool descriptor = started && !raw.isEmpty()
                && std::all_of(raw.begin(), raw.end(), [](QChar c) { return c >= '0' && c <= '9'; });
            if (!partial && (ch == '>' || ch == '<') && descriptor) {
                error = "parse: file descriptor redirection is not supported"; return {};
            }
            end = i;
            flush();
            const int opStart = i;
            QString op(ch);
            if (i + 1 < line.size() && line.at(i + 1) == ch) {
                op += ch; ++i;
                if (!partial && op != ">>") { error = "parse: unsupported shell operator " + op; return {}; }
            }
            tokens.append({op, true, opStart, i + 1});
        } else { token += ch; started = true; }
    }
    if (escaped && !partial) { error = "parse: incomplete escape at end of input"; return {}; }
    if (!quote.isNull() && !partial) { error = "parse: unclosed quote"; return {}; }
    end = line.size();
    flush();
    return tokens;
}

QString TerminalCore::displayPath() const {
    if (m_currentPath == m_home) return "~";
    if (m_currentPath.startsWith(m_home + '/')) return '~' + m_currentPath.mid(m_home.size());
    return m_currentPath;
}

QString TerminalCore::prompt() const { return m_identity.label() + ':' + displayPath() + "$ "; }

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
    QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::ConvertInitialBom);
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

CommandResult TerminalCore::saveNewBinaryFile(const QString &path, const QByteArray &bytes) {
    if (bytes.isEmpty() || bytes.size() > 20 * 1024 * 1024) return fail("save: unsupported image size");
    QString error;
    const QString filename = physicalPath(resolve(path), error);
    if (!error.isEmpty()) return fail("save: " + error);
    if (!QFileInfo(filename).dir().exists()) return fail("save: parent directory does not exist");
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("save: cannot create file; existing files are never overwritten");
    if (file.write(bytes) != bytes.size() || !file.flush()) return fail("save: write failed; check the destination file");
    return {};
}

CommandResult TerminalCore::saveFile(const QString &path, const QString &content) {
    return writeFile(resolve(path), content, false);
}

TextFileSnapshot TerminalCore::readTextSnapshot(const QString &path) const {
    TextFileSnapshot snapshot;
    snapshot.path = resolve(path);
    const QString native = physicalPath(snapshot.path, snapshot.error);
    if (!snapshot.error.isEmpty()) return snapshot;
    if (!QFileInfo(native).dir().exists()) { snapshot.error = "parent directory does not exist"; return snapshot; }
    snapshot.exists = QFileInfo::exists(native);
    if (!snapshot.exists) return snapshot;
    const auto read = readFile(snapshot.path);
    if (read.error) { snapshot.error = read.text; return snapshot; }
    snapshot.text = read.text;
    snapshot.digest = QCryptographicHash::hash(snapshot.text.toUtf8(), QCryptographicHash::Sha256);
    return snapshot;
}

CommandResult TerminalCore::saveTextSnapshot(TextFileSnapshot &snapshot, const QString &content, bool force) {
    if (!snapshot.error.isEmpty()) return fail(snapshot.error);
    if (!force) {
        QString error;
        const QString native = physicalPath(snapshot.path, error);
        if (!error.isEmpty()) return fail(error);
        const bool exists = QFileInfo::exists(native);
        QByteArray digest;
        if (exists) {
            if (QFileInfo(native).isDir()) return fail("path is a directory");
            QFile file(native);
            if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
            const auto bytes = file.readAll();
            if (file.error() != QFileDevice::NoError) return fail(file.errorString());
            digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        }
        if (exists != snapshot.exists || (exists && digest != snapshot.digest))
            return fail("File changed or was deleted outside Vim. Use :e! to reload, :w! to overwrite, or :q! to discard.");
    }
    const auto saved = writeFile(snapshot.path, content, false);
    if (!saved.error) {
        snapshot.exists = true;
        snapshot.text = content;
        snapshot.digest = QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256);
    }
    return saved;
}

WorldDirectory TerminalCore::listDirectory(const QString &path) const {
    WorldDirectory result;
    result.path = resolve(path);
    const QString native = physicalPath(result.path, result.error);
    if (!result.error.isEmpty()) return result;
    const QFileInfo directory(native);
    if (!directory.exists()) result.error = result.path + ": no such file or directory";
    else if (!directory.isDir()) result.error = result.path + ": not a directory";
    else if (!directory.isReadable()) result.error = result.path + ": permission denied";
    if (!result.error.isEmpty()) return result;
    const auto filters = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
    for (const auto &info : QDir(native).entryInfoList(filters, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase)) {
        if (isLink(info) || (!info.isDir() && !info.isFile())) continue;
        result.entries.append({info.fileName(), info.isDir(), info.size(), info.lastModified()});
    }
    return result;
}

CommandResult TerminalCore::readTextFile(const QString &path) const { return readFile(resolve(path)); }
WorldFile TerminalCore::readBinaryFile(const QString &path, qint64 maximumBytes) const {
    WorldFile result;
    result.path = resolve(path);
    const QString native = physicalPath(result.path, result.error);
    if (!result.error.isEmpty()) return result;
    const QFileInfo info(native);
    if (!info.exists()) result.error = result.path + ": no such file or directory";
    else if (!info.isFile()) result.error = result.path + ": not a regular file";
    else if (maximumBytes <= 0 || maximumBytes > 64 * 1024 * 1024 || info.size() > maximumBytes)
        result.error = result.path + ": file exceeds the supported size limit";
    if (!result.error.isEmpty()) return result;
    QFile file(native);
    if (!file.open(QIODevice::ReadOnly)) { result.error = result.path + ": " + file.errorString(); return result; }
    // Bound the read itself as well, in case an external editor grows the file.
    result.bytes = file.read(maximumBytes + 1);
    if (file.error() != QFileDevice::NoError) result.error = result.path + ": " + file.errorString();
    else if (result.bytes.size() > maximumBytes) result.error = result.path + ": file exceeds the supported size limit";
    if (!result.error.isEmpty()) result.bytes.clear();
    return result;
}
CommandResult TerminalCore::copyEntry(const QString &source, const QString &destination) {
    return transfer("cp", {"-r", "--", source, destination});
}
CommandResult TerminalCore::moveEntry(const QString &source, const QString &destination) {
    return transfer("mv", {"--", source, destination});
}
CommandResult TerminalCore::removeEntry(const QString &path) {
    return executeCommand("rm", {"-r", "--", path}, {}, false, false);
}
CommandResult TerminalCore::createDirectory(const QString &path) {
    return executeCommand("mkdir", {"--", path}, {}, false, false);
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
    if (!m_initializationError.isEmpty()) {
        auto result = fail(m_initializationError);
        result.diagnostics = result.text + '\n';
        return result;
    }
    if (!line.trimmed().isEmpty() && line.size() <= 16384
        && !line.contains('\n') && !line.contains('\r')
        && (m_history.isEmpty() || m_history.last() != line)) {
        m_history.append(line);
        if (m_history.size() > historyLimit) m_history.removeFirst();
        saveHistory();
    }
    auto result = executePipeline(line);
    // Errors returned before a command runs still have a diagnostic stream.
    if (result.error && result.diagnostics.isEmpty()) result.diagnostics = result.text + '\n';
    if (!m_historyWarning.isEmpty()) result.diagnostics += m_historyWarning + '\n';
    return result;
}

CommandResult TerminalCore::executePipeline(const QString &line) {
    QString parseError;
    const auto tokens = tokenize(line, parseError);
    if (!parseError.isEmpty()) return fail(parseError);
    if (tokens.isEmpty()) return {};
    struct Redirection { QString op; QString path; };
    struct Stage { QStringList words; QList<Redirection> redirects; };
    QList<Stage> stages(1);
    for (int i = 0; i < tokens.size(); ++i) {
        const auto &token = tokens.at(i);
        if (!token.control) stages.last().words.append(token.text);
        else if (token.text == "|") {
            if (stages.last().words.isEmpty()) return fail("parse: expected a command before '|'");
            stages.append(Stage{});
        } else {
            if (i + 1 >= tokens.size() || tokens.at(i + 1).control)
                return fail("parse: expected a filename after " + token.text);
            for (const auto &existing : stages.last().redirects)
                if ((existing.op == "<") == (token.text == "<"))
                    return fail("parse: only one input and one output redirection per command are supported");
            stages.last().redirects.append({token.text, tokens.at(++i).text});
        }
    }
    if (stages.last().words.isEmpty()) return fail("parse: expected a command");
    const QStringList textCommands = {"cat", "echo", "find", "grep", "head", "help", "history", "less", "ls", "pwd", "tail", "whoami"};
    for (int i = 0; i < stages.size(); ++i) {
        const auto &stage = stages.at(i);
        const QString command = stage.words.first();
        if (stages.size() > 1 && commands.contains(command) && !textCommands.contains(command))
            return fail(command + ": this command cannot be used in a pipeline");
        if ((command == "browser" || command == "camera" || command == "vim" || command == "mc" || command == "feh" || command == "clear"
            || command == "exit" || command == "logout")
            && !stage.redirects.isEmpty()) return fail(command + ": redirection is not supported for this interactive command");
        if (command == "history" && stage.words.contains("-c") && stages.size() > 1)
            return fail("history: -c cannot be used in a pipeline");
    }
    QString input, diagnostics;
    CommandResult final;
    for (int i = 0; i < stages.size(); ++i) {
        const auto &stage = stages.at(i);
        const QString command = stage.words.first();
        bool hasInput = i > 0;
        QString inputFile, outputFile;
        bool append = false, redirected = false, prepared = true;
        CommandResult result;
        // Prepare redirections before executing the command, in lexical order.
        // In particular, `cat file > file` reads the already-truncated file.
        for (const auto &redirect : stage.redirects) {
            if (redirect.op == "<") {
                inputFile = resolve(redirect.path);
                result = readFile(inputFile);
                hasInput = true;
            } else {
                redirected = true;
                outputFile = resolve(redirect.path);
                append = redirect.op == ">>";
                result = writeFile(outputFile, {}, append);
            }
            if (result.error) { prepared = false; break; }
        }
        if (prepared && !inputFile.isEmpty()) {
            result = readFile(inputFile);
            if (result.error) prepared = false;
            else input = result.text;
        }
        if (prepared) result = executeCommand(command, stage.words.mid(1), input, hasInput,
            redirected || i + 1 < stages.size());
        if (result.error && result.diagnostics.isEmpty()) {
            result.diagnostics = result.text + '\n';
            result.text.clear();
        }
        if (prepared && result.output.isNull() && !result.error) {
            result.output = result.text;
            if (command != "cat" && command != "grep" && command != "less"
                && (!result.output.isEmpty() || command == "echo")) result.output += '\n';
        }
        if (prepared && redirected) {
            const auto saved = writeFile(outputFile, result.output, true);
            if (saved.error) {
                result.diagnostics += saved.text + '\n';
                result.error = true;
                result.exitCode = 1;
            }
            result.output.clear();
            result.text.clear();
        }
        diagnostics += result.diagnostics;
        input = result.output;
        final = result;
    }
    final.diagnostics = diagnostics;
    final.error = !diagnostics.isEmpty();
    return final;
}

CommandResult TerminalCore::executeCommand(const QString &command, QStringList args,
    const QString &input, bool hasInput, bool streamOutput) {
    if (!commands.contains(command)) return fail(command + ": command not found. Type 'help' for commands.", 127);
    if (command == "exit" || command == "logout") {
        if (!args.isEmpty()) return fail("usage: " + command);
        CommandResult result;
        result.endSession = true;
        return result;
    }
    if (command == "history") {
        if (args == QStringList{"-c"}) {
            m_history.clear();
            m_historyWritable = true;
            saveHistory();
            return {};
        }
        bool valid = true;
        int count = args.isEmpty() ? m_history.size() : args.first().toInt(&valid);
        if (!valid || count < 0 || args.size() > 1) return fail("usage: history [count] or history -c");
        QStringList lines;
        for (int i = qMax(0, int(m_history.size()) - count); i < m_history.size(); ++i)
            lines.append(QString("%1  %2").arg(i + 1, 5).arg(m_history.at(i)));
        return {lines.join('\n')};
    }
    if (command == "grep") return grep(args, input, hasInput);
    if (command == "find") return findFiles(args);
    if (command == "head" || command == "tail") return readLines(command, args, input, hasInput);
    if (command == "cat" || command == "less") {
        if (!args.isEmpty() && args.first() == "--") args.removeFirst();
        if (command == "less" && args.size() > 1) return fail("usage: less [file] (or command | less)");
        if (args.isEmpty()) args.append("-");
        CommandResult result;
        result.output = QStringLiteral("");
        bool consumed = false;
        for (const auto &arg : args) {
            const auto read = arg == "-"
                ? (hasInput ? CommandResult{consumed ? QString() : input} : fail(command + ": provide a file, a pipe, or '< file'"))
                : readFile(resolve(arg));
            if (arg == "-") consumed = true;
            if (read.error) result.diagnostics += command + ": " + read.text + '\n';
            else result.output += read.text;
        }
        result.error = !result.diagnostics.isEmpty();
        result.exitCode = result.error ? 1 : 0;
        result.text = result.output;
        result.openPager = command == "less" && !streamOutput && !result.error;
        result.pagerTitle = args == QStringList{"-"} ? "(standard input)" : args.first();
        return result;
    }
    if (command == "echo") return {args.join(' ')};
    if (command == "camera") {
        if (args == QStringList{"--help"} || args == QStringList{"-h"})
            return {"Usage: camera [http://address]\nOpen the fixed-camera snapshot client. Sign in inside the client; take a snapshot explicitly."};
        if (args.size() > 1 || (!args.isEmpty() && args.first().startsWith('-')))
            return fail("usage: camera [http://address]");
        CommandResult result;
        result.openCamera = true;
        if (!args.isEmpty()) result.cameraAddress = args.first();
        return result;
    }
    if (command == "browser") {
        if (args.size() > 1) return fail("usage: browser [https://site/path]");
        CommandResult result;
        result.openBrowser = true;
        if (!args.isEmpty()) result.browserUrl = args.first();
        return result;
    }
    if (command == "feh") {
        if (args == QStringList{"--help"} || args == QStringList{"-h"})
            return {"Usage: feh [--] [file|directory ...]\n"
                "View local PNG/JPEG images. No arguments browses the current directory.\n"
                "Directories are scanned once, without recursion. F1 shows supported keys.\n"
                "This version is read-only; other feh options are not implemented."};
        bool literal = false;
        QStringList paths;
        for (const auto &arg : args) {
            if (!literal && arg == "--") { literal = true; continue; }
            if (!literal && arg.startsWith('-')) return fail("feh: unsupported option " + arg + "; use feh --help");
            QString error;
            const QString path = resolve(arg);
            physicalPath(path, error);
            if (!error.isEmpty()) return fail("feh: " + error);
            paths.append(path);
        }
        CommandResult result;
        result.openImageViewer = true;
        result.imagePaths = paths.isEmpty() ? QStringList{m_currentPath} : paths;
        return result;
    }
    if (command == "mc") {
        if (args == QStringList{"--help"} || args == QStringList{"-h"})
            return {"Usage: mc [directory [other-directory]]\n"
                "Open the two-panel file manager. F1 shows supported keys.\n"
                "Use -- before a directory whose name begins with '-'."};
        if (!args.isEmpty() && args.first() == "--") args.removeFirst();
        else for (const auto &arg : args)
            if (arg.startsWith('-')) return fail("mc: unsupported option " + arg + "; use mc --help");
        if (args.size() > 2) return fail("usage: mc [directory [other-directory]]");
        CommandResult result;
        result.openFileManager = true;
        for (const auto &arg : args) {
            const auto directory = listDirectory(arg);
            if (!directory.error.isEmpty()) return fail("mc: " + directory.error);
            result.managerPaths.append(directory.path);
        }
        return result;
    }
    if (command == "help") {
        if (!args.isEmpty()) return fail("usage: help");
        return {"AVAILABLE COMMANDS\n\n"
            "  browser [address]    Open the web browser\n"
            "  camera [address]     Open the camera snapshot client\n"
            "  mc [dir [other]]     Open the two-panel file manager\n"
            "  feh [file|dir ...]   View local PNG/JPEG images\n"
            "  ls [-a] [path]       List files and directories\n"
            "  cd [path]            Change directory; no path returns home\n"
            "  pwd                  Print the current directory\n"
            "  cat [file ...]       Read text files or piped input\n"
            "  find [path] ...      Find names: -name/-iname glob, -type f|d, -maxdepth n\n"
            "  head [-n count] ...  Read the first 10 lines (or count) of files or stdin\n"
            "  tail [-n count] ...  Read the last 10 lines (or count) of files or stdin\n"
            "  grep -F[nirv] ...    Search literal text; -n lines, -i ignore case, -r recursive\n"
            "  less [file]          Page through text; / search, n/N next/previous, q quit\n"
            "  history [count]      Show saved commands; history -c clears history\n"
            "  mkdir [-p] <path>    Create a directory\n"
            "  touch <file>         Create a file; keep existing content\n"
            "  vim <file>           Edit inside the terminal (i, Esc, :wq)\n"
            "  mv <source> <dest>   Move or rename a file or directory\n"
            "  cp [-r] <src> <dst>  Copy a file; -r copies directories\n"
            "  rm [-r] <path>       Delete a file; -r deletes directories\n"
            "  echo [text]          Print text\n"
            "  command > file      Save stdout (overwrite); >> appends\n"
            "  command < file      Read stdin from a UTF-8 file\n"
            "  command | command   Send stdout to the next text command\n"
            "  whoami               Show the current user\n"
            "  exit / logout        End this login session\n"
            "  clear                Clear the screen\n"
            "  help                 Show this help\n\n"
            "PATHS   / absolute   .. parent   ~ home   cd - previous\n"
            "KEYS    Up/Down history   Ctrl+R search   Tab complete   Ctrl+L clear   F1 help\n"
            "EDIT    Ctrl+A/E start/end   Alt+B/F word   Ctrl+U/K/W cut   Ctrl+Y yank\n\n"
            "Files, the current directory, and up to 1000 commands persist between sessions.\n"
            "cp and mv never overwrite an existing destination."};
    }
    if (command == "clear" || command == "pwd" || command == "whoami") {
        if (!args.isEmpty()) return fail("usage: " + command);
        if (command == "clear") return {{}, false, true};
        return {command == "pwd" ? m_currentPath : m_identity.userName};
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
        return {children(path, hidden).join(streamOutput ? "\n" : "    ")};
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
    if (command == "vim") {
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
        return result;
    }
    return readFile(path);
}

CommandResult TerminalCore::grep(QStringList args, const QString &input, bool hasInput) const {
    bool fixed = false, numbered = false, recursive = false, ignoreCase = false, invert = false;
    bool options = true;
    QStringList operands;
    for (const auto &arg : args) {
        if (options && arg == "--") { options = false; continue; }
        if (options && operands.isEmpty() && arg.startsWith('-') && arg != "-") {
            for (const auto flag : arg.mid(1)) {
                if (flag == 'F') fixed = true;
                else if (flag == 'n') numbered = true;
                else if (flag == 'i') ignoreCase = true;
                else if (flag == 'r') recursive = true;
                else if (flag == 'v') invert = true;
                else return fail("grep: unsupported option -" + QString(flag) + "; use grep -F[nirv] pattern [file ...]", 2);
            }
        } else { operands.append(arg); options = false; }
    }
    if (!fixed) return fail("grep: regular expressions are not implemented; use -F for literal text", 2);
    if (operands.isEmpty()) return fail("usage: grep -F[nirv] <pattern> [file ...]", 2);
    const QStringList patterns = operands.takeFirst().split('\n');
    if (operands.isEmpty()) operands.append(recursive && !hasInput ? "." : "-");
    CommandResult result;
    result.output = QStringLiteral("");
    bool matched = false;
    const bool filenames = recursive || operands.size() > 1;
    const auto select = [&](const QString &text, const QString &label) {
        const QStringList lines = text.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            if (i == lines.size() - 1 && lines.at(i).isEmpty()) break;
            const bool contains = std::any_of(patterns.begin(), patterns.end(), [&](const QString &pattern) {
                return lines.at(i).contains(pattern, ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive);
            });
            if (contains == invert) continue;
            matched = true;
            if (filenames) result.output += label + ':';
            if (numbered) result.output += QString::number(i + 1) + ':';
            result.output += lines.at(i) + '\n';
        }
    };
    std::function<void(const QString &)> visit = [&](const QString &name) {
        QString error;
        const QString native = physicalPath(resolve(name), error);
        if (!error.isEmpty()) { result.diagnostics += "grep: " + name + ": " + error + '\n'; return; }
        if (QFileInfo(native).isDir()) {
            if (!recursive) { result.diagnostics += "grep: " + name + ": is a directory (use -r)\n"; return; }
            const QDir dir(native);
            if (!dir.isReadable()) { result.diagnostics += "grep: " + name + ": permission denied\n"; return; }
            for (const auto &info : dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                     QDir::Name)) {
                if (isLink(info)) continue;
                visit((name.endsWith('/') ? name : name + '/') + info.fileName());
            }
        } else {
            const auto read = readFile(resolve(name));
            if (read.error) result.diagnostics += "grep: " + read.text + '\n';
            else select(read.text, name);
        }
    };
    bool consumed = false;
    for (const auto &name : operands) {
        if (name == "-") {
            if (!hasInput) result.diagnostics += "grep: provide a file, a pipe, or '< file'\n";
            else if (!consumed) select(input, "(standard input)");
            consumed = true;
        } else visit(name);
    }
    result.error = !result.diagnostics.isEmpty();
    result.exitCode = result.error ? 2 : (matched ? 0 : 1);
    result.text = result.output;
    return result;
}

Completion TerminalCore::complete(const QString &line, int position) const {
    if (position < 0) position = line.size();
    position = qBound(0, position, int(line.size()));
    Completion result{line, {}, position};
    QString error;
    const auto tokens = tokenize(line, error, true);
    int start = position, end = position, index = tokens.size();
    for (int i = 0; i < tokens.size(); ++i) {
        const auto &token = tokens.at(i);
        if (position >= token.start && position <= token.end && !token.control) {
            start = token.start; end = token.end; index = i; break;
        }
        if (token.start >= position) { index = i; break; }
    }
    const auto fragment = tokenize(line.mid(start, position - start), error, true);
    const QString partial = fragment.isEmpty() ? QString() : fragment.last().text;
    QString command;
    bool redirection = false;
    for (int i = 0; i < index; ++i) {
        const auto &token = tokens.at(i);
        if (token.control && token.text == "|") { command.clear(); redirection = false; }
        else if (token.control) redirection = true;
        else if (redirection) redirection = false;
        else if (command.isEmpty()) command = token.text;
    }
    const bool commandPosition = command.isEmpty() && !redirection;
    QStringList candidates;
    if (commandPosition) {
        for (const auto &name : commands) if (name.startsWith(partial)) candidates.append(name);
    } else if (!redirection && (command == "camera" || command == "browser")) {
        // These arguments are service addresses, not paths in the local world.
        return result;
    } else {
        const int slash = partial.lastIndexOf('/');
        const QString prefix = slash < 0 ? QString() : partial.left(slash + 1);
        const QString name = partial.mid(slash + 1);
        const QString directory = resolve(prefix.isEmpty() ? "." : prefix);
        for (const auto &entry : children(directory, name.startsWith('.'))) {
            if (!entry.startsWith(name)) continue;
            if (!redirection && (command == "cd" || command == "mkdir") && !entry.endsWith('/')) continue;
            candidates.append(prefix + entry);
        }
    }
    const QChar quote = start < line.size() && (line.at(start) == '\'' || line.at(start) == '"') ? line.at(start) : QChar();
    const auto encode = [&](QString value, bool close) {
        if (quote == '\'') {
            value.replace("'", "'\\''");
            return "'" + value + (close ? "'" : "");
        }
        if (quote == '"') {
            value.replace("\\", "\\\\"); value.replace("\"", "\\\"");
            return "\"" + value + (close ? "\"" : "");
        }
        QString encoded;
        for (const auto c : value) {
            if (c.isSpace() || QStringLiteral("\\\"'|><;&()`").contains(c)) encoded += '\\';
            encoded += c;
        }
        return encoded;
    };
    for (const auto &candidate : candidates) result.candidates.append(encode(candidate, true));
    if (candidates.isEmpty()) return result;
    QString replacement = candidates.first();
    if (candidates.size() > 1) {
        if (position != end) return result;
        for (const auto &candidate : candidates)
            while (!candidate.startsWith(replacement)) replacement.chop(1);
        if (!replacement.isEmpty() && replacement.back().isHighSurrogate()) replacement.chop(1);
        if (replacement.size() <= partial.size()) return result;
    }
    const bool complete = candidates.size() == 1 && !replacement.endsWith('/');
    const bool hadClosingQuote = !quote.isNull() && end > start + 1 && line.at(end - 1) == quote;
    QString encoded = encode(replacement, complete || hadClosingQuote);
    int cursor = start + encoded.size();
    if (complete && (end == line.size() || !line.at(end).isSpace())) { encoded += ' '; ++cursor; }
    result.replacement = line.left(start) + encoded + line.mid(end);
    result.cursor = cursor;
    return result;
}
