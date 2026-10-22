#pragma once

#include "terminalcore.h"
#include <QWidget>
#include <functional>

class QLabel;
class QKeyEvent;
class VimBuffer;
class VimCommandLine;

class VimEditor : public QWidget {
public:
    using SaveHandler = std::function<CommandResult(const QString &)>;
    using CloseHandler = std::function<void()>;
    VimEditor(const QString &path, const QString &content, const QFont &font,
        int lineSpacing, SaveHandler save, CloseHandler close, QWidget *parent = nullptr);
    void focusBuffer();
    void setAppearance(const QFont &font, int lineSpacing);

private:
    friend class VimBuffer;
    friend class VimCommandLine;
    enum class Mode { Normal, Insert, Command, Search };
    Mode m_mode = Mode::Normal;
    QString m_path;
    QString m_savedText;
    QString m_register;
    bool m_registerIsLine = false;
    QString m_pending;
    QString m_search;
    bool m_closed = false;
    VimBuffer *m_buffer;
    VimCommandLine *m_command;
    QLabel *m_modeLabel;
    QLabel *m_fileLabel;
    QLabel *m_positionLabel;
    SaveHandler m_save;
    CloseHandler m_close;

    bool handleBufferKey(QKeyEvent *event);
    void enterInsert();
    void enterNormal(bool fromInsert = false);
    void startCommand(Mode mode);
    void executeCommand();
    bool save();
    void quit(bool force);
    void message(const QString &text, bool error = false);
    void refreshStatus();
    bool modified() const;
    void clampNormalCursor();
    void deleteLine();
    void pasteRegister();
    void findNext(bool backward = false);
};
