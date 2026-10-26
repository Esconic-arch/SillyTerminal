#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct CommandResult {
    QString text;
    bool error = false;
    bool clear = false;
    QString editPath;
    QString editContent;
    bool useVim = false;
    bool openBrowser = false;
    QString browserUrl;
    // Exact stream contents; text remains the command's human-readable result.
    QString output;
    QString diagnostics;
    int exitCode = 0;
    bool openPager = false;
    QString pagerTitle;
};

struct Completion {
    QString replacement;
    QStringList candidates;
    int cursor = -1;
};

class TerminalCore {
public:
    explicit TerminalCore(const QString &worldRoot = {});
    CommandResult execute(const QString &line);
    Completion complete(const QString &line, int cursor = -1) const;
    const QStringList &history() const { return m_history; }
    QString historyWarning() const { return m_historyWarning; }
    QString currentPath() const { return m_currentPath; }
    QString displayPath() const;
    QString prompt() const;
    QString worldRoot() const { return m_worldRoot; }
    QString initializationError() const { return m_initializationError; }
    CommandResult saveFile(const QString &path, const QString &content);
    static QString defaultWorldRoot();

private:
    struct Token { QString text; bool control = false; int start = 0; int end = 0; };
    QString m_worldRoot;
    QString m_statePath;
    QString m_initializationError;
    QStringList m_history;
    QString m_historyWarning;
    bool m_historyWritable = true;
    QString m_currentPath = QStringLiteral("/home/guest");
    QString m_previousPath = QStringLiteral("/home/guest");
    const QString m_home = QStringLiteral("/home/guest");

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
    CommandResult readFile(const QString &path) const;
    CommandResult writeFile(const QString &path, const QString &content, bool append);
    CommandResult transfer(const QString &command, const QStringList &args);
    static QList<Token> tokenize(const QString &line, QString &error, bool partial = false);
};
