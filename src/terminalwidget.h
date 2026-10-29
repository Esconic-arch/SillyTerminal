#pragma once

#include "terminalcore.h"
#include <QDateTime>
#include <QTextEdit>
#include <functional>
#include <memory>
class CameraImageProvider;

class QInputMethodEvent;
class QKeyEvent;
class QMimeData;
class VimEditor;
class TextBrowser;
class QLineEdit;
class QLabel;
class TextPager;
class McFileManager;

class TerminalWidget : public QTextEdit {
public:
    explicit TerminalWidget(QWidget *parent = nullptr, const QString &worldRoot = {},
        const LocalIdentity &identity = {}, const QDateTime &previousLogin = {},
        std::shared_ptr<CameraImageProvider> cameraImages = {});
    void runCommand(const QString &command);
    void showAppearanceSettings();
    bool smokeTest(const QString &appearancePreviewPath = {});
    QString currentDirectory() const { return m_core.displayPath(); }
    std::function<void()> onLogout;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    TerminalCore m_core;
    std::shared_ptr<CameraImageProvider> m_cameraImages;
    int m_inputStart = 0;
    int m_historyIndex = 0;
    QString m_draft;
    QString m_killBuffer;
    bool m_lastWasKill = false;
    QLineEdit *m_historySearch = nullptr;
    QLabel *m_historySearchLabel = nullptr;
    QString m_searchDraft;
    int m_searchDraftCursor = 0;
    int m_searchIndex = -1;
    int m_lineSpacing = 115;
    VimEditor *m_vim = nullptr;
    TextBrowser *m_browser = nullptr;
    TextPager *m_pager = nullptr;
    McFileManager *m_fileManager = nullptr;

    QString currentInput() const;
    void setInput(const QString &text);
    void ensureEditableCursor();
    void write(const QString &text, const QColor &color);
    void showPrompt();
    void submit();
    void clearScreen();
    void completeInput();
    void beginHistorySearch();
    void layoutHistorySearch();
    void searchHistory(bool next);
    void finishHistorySearch(bool cancel);
    void killInput(int start, int end, bool backward);
    void applyAppearance(const QFont &font, int lineSpacing, bool save);
    void openVim(const QString &path);
    void openBrowser(const QString &address);
    void openPager(const QString &title, const QString &content);
    void openFileManager(const QStringList &paths);
};
