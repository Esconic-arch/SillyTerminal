#pragma once

#include "terminalcore.h"
#include <QTextEdit>

class QInputMethodEvent;
class QKeyEvent;
class QMimeData;
class VimEditor;
class TextBrowser;

class TerminalWidget : public QTextEdit {
public:
    explicit TerminalWidget(QWidget *parent = nullptr, const QString &worldRoot = {});
    void runCommand(const QString &command);
    void showAppearanceSettings();
    bool smokeTest(const QString &appearancePreviewPath = {});
    QString currentDirectory() const { return m_core.displayPath(); }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    TerminalCore m_core;
    int m_inputStart = 0;
    QStringList m_history;
    int m_historyIndex = 0;
    QString m_draft;
    int m_lineSpacing = 115;
    VimEditor *m_vim = nullptr;
    TextBrowser *m_browser = nullptr;

    QString currentInput() const;
    void setInput(const QString &text);
    void ensureEditableCursor();
    void write(const QString &text, const QColor &color);
    void showPrompt();
    void submit();
    void clearScreen();
    void completeInput();
    void applyAppearance(const QFont &font, int lineSpacing, bool save);
    void openEditor(const QString &path, const QString &content);
    void openVim(const QString &path, const QString &content);
    void openBrowser(const QString &address);
};
