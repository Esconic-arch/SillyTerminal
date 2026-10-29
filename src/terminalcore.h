#pragma once

#include "localidentity.h"
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

struct CommandResult {
    QString text;
    bool error = false;
    bool clear = false;
    QString editPath;
    QString editContent;
    bool openBrowser = false;
    QString browserUrl;
    // Exact stream contents; text remains the command's human-readable result.
    QString output;
    QString diagnostics;
    int exitCode = 0;
    bool openPager = false;
    QString pagerTitle;
    bool endSession = false;
    bool openFileManager = false;
    QStringList managerPaths;
    bool openCamera = false;
    QString cameraAddress;
    bool openImageViewer = false;
    QStringList imagePaths;
};

struct WorldEntry {
    QString name;
    bool directory = false;
    qint64 size = 0;
    QDateTime modified;
};

struct WorldDirectory {
    QString path;
    QList<WorldEntry> entries;
    QString error;
};

struct WorldFile {
    QString path;
    QByteArray bytes;
    QString error;
};

struct Completion {
    QString replacement;
    QStringList candidates;
    int cursor = -1;
};

struct TextFileSnapshot {
    QString path;
    QString text;
    bool exists = false;
    QByteArray digest;
    QString error;
};

class TerminalCore {
public:
    explicit TerminalCore(const QString &worldRoot = {}, const LocalIdentity &identity = {});
    CommandResult execute(const QString &line);
    Completion complete(const QString &line, int cursor = -1) const;
    const QStringList &history() const { return m_history; }
    QString historyWarning() const { return m_historyWarning; }
    QString currentPath() const { return m_currentPath; }
    QString displayPath() const;
    QString prompt() const;
    QString worldRoot() const { return m_worldRoot; }
    QString initializationError() const { return m_initializationError; }
    LocalIdentity identity() const { return m_identity; }
    CommandResult saveFile(const QString &path, const QString &content);
    TextFileSnapshot readTextSnapshot(const QString &path) const;
    CommandResult saveTextSnapshot(TextFileSnapshot &snapshot, const QString &content, bool force = false);
    // Export generated media into the same real, bounded filesystem. Never overwrite.
    CommandResult saveNewBinaryFile(const QString &path, const QByteArray &bytes);
    // Structured access for programs; uses the same world boundary as shell commands.
    WorldDirectory listDirectory(const QString &path) const;
    CommandResult readTextFile(const QString &path) const;
    WorldFile readBinaryFile(const QString &path, qint64 maximumBytes = 20 * 1024 * 1024) const;
    CommandResult copyEntry(const QString &source, const QString &destination);
    CommandResult moveEntry(const QString &source, const QString &destination);
    CommandResult removeEntry(const QString &path);
    CommandResult createDirectory(const QString &path);
    static QString defaultWorldRoot();

private:
    struct Token { QString text; bool control = false; int start = 0; int end = 0; };
    LocalIdentity m_identity;
    QString m_worldRoot;
    QString m_statePath;
    QString m_initializationError;
    QStringList m_history;
    QString m_historyWarning;
    bool m_historyWritable = true;
    QString m_currentPath;
    QString m_previousPath;
    const QString m_home;

    QString resolve(const QString &path) const;
    QStringList children(const QString &directory, bool hidden) const;
    QString physicalPath(const QString &path, QString &error) const;
    QString validateTree(const QString &path) const;
    void initialize();
    void saveSession() const;
    void loadHistory();
    void saveHistory();
    CommandResult executePipeline(const QString &line);
    CommandResult executeCommand(const QString &command, QStringList args,
        const QString &input, bool hasInput, bool streamOutput);
    CommandResult grep(QStringList args, const QString &input, bool hasInput) const;
    CommandResult findFiles(QStringList args) const;
    CommandResult readLines(const QString &command, QStringList args, const QString &input, bool hasInput) const;
    CommandResult readFile(const QString &path) const;
    CommandResult writeFile(const QString &path, const QString &content, bool append);
    CommandResult transfer(const QString &command, const QStringList &args);
    static QList<Token> tokenize(const QString &line, QString &error, bool partial = false);
};
