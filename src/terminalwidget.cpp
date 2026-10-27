#include "terminalwidget.h"
#include "vimeditor.h"
#include "textbrowser.h"
#include "textpager.h"
#include "mcfilemanager.h"
#include "terminalappearance.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace {
const QColor foreground("#dedee5");
const QColor accent("#c0c5ed");
const QColor muted("#92939f");
const QColor errorColor("#f0a6a9");
}

TerminalWidget::TerminalWidget(QWidget *parent, const QString &worldRoot,
    const LocalIdentity &identity, const QDateTime &previousLogin)
    : QTextEdit(parent), m_core(worldRoot, identity) {
    setAcceptRichText(false);
    setUndoRedoEnabled(false);
    setAcceptDrops(false);
    setFrameStyle(QFrame::NoFrame);
    setTabChangesFocus(false);
    setLineWrapMode(QTextEdit::WidgetWidth);
    document()->setDocumentMargin(24);
    setCursorWidth(2);
    setStyleSheet(
        "QTextEdit { background: #1c1c1f; color: #dedee5; selection-background-color: #45465b; selection-color: #ffffff; }"
        "QScrollBar:vertical { background: #1c1c1f; width: 7px; margin: 7px 1px; }"
        "QScrollBar::handle:vertical { background: #45454d; min-height: 36px; border-radius: 3px; }"
        "QScrollBar::handle:vertical:hover { background: #61616c; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }");
    const auto appearance = TerminalAppearance::load();
    applyAppearance(appearance.font, appearance.lineSpacing, false);
    if (previousLogin.isValid()) {
        write("Last login: " + QLocale::c().toString(previousLogin.toLocalTime(), "ddd MMM d HH:mm:ss yyyy")
            + " on tty1\n\n", muted);
    }
    if (!m_core.initializationError().isEmpty()) write(m_core.initializationError() + '\n', errorColor);
    if (!m_core.historyWarning().isEmpty()) write(m_core.historyWarning() + '\n', errorColor);
    m_historyIndex = m_core.history().size();
    m_historySearch = new QLineEdit(this);
    m_historySearch->setObjectName("historySearch");
    m_historySearch->setPlaceholderText("reverse-i-search: type to search command history");
    m_historySearch->setToolTip("Ctrl+R: older match | Esc: edit match | Enter: execute | Ctrl+G: cancel");
    m_historySearch->setFont(font());
    m_historySearch->installEventFilter(this);
    m_historySearch->hide();
    m_historySearchLabel = new QLabel("reverse-i-search:", this);
    m_historySearchLabel->setFont(font());
    m_historySearchLabel->setStyleSheet("QLabel { color: #92939f; }");
    m_historySearchLabel->hide();
    connect(m_historySearch, &QLineEdit::textEdited, this, [this] { searchHistory(false); });
    showPrompt();
}

void TerminalWidget::write(const QString &text, const QColor &color) {
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setForeground(color);
    cursor.insertText(text, format);
    setTextCursor(cursor);
}

void TerminalWidget::showPrompt() {
    write(m_core.identity().label(), foreground);
    write(":", muted);
    write(m_core.displayPath(), accent);
    write("$ ", muted);
    m_inputStart = document()->characterCount() - 1;
    QTextCharFormat format;
    format.setForeground(foreground);
    setCurrentCharFormat(format);
    ensureCursorVisible();
}

QString TerminalWidget::currentInput() const {
    return toPlainText().mid(m_inputStart);
}

void TerminalWidget::setInput(const QString &text) {
    QTextCursor cursor(document());
    cursor.setPosition(m_inputStart);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    QTextCharFormat format;
    format.setForeground(foreground);
    cursor.insertText(text, format);
    setTextCursor(cursor);
    ensureCursorVisible();
}

void TerminalWidget::ensureEditableCursor() {
    auto cursor = textCursor();
    if (cursor.position() < m_inputStart || cursor.anchor() < m_inputStart) {
        cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);
    }
    QTextCharFormat format;
    format.setForeground(foreground);
    setCurrentCharFormat(format);
}

void TerminalWidget::submit() {
    const QString line = currentInput();
    m_draft.clear();
    write("\n", foreground);
    const CommandResult result = m_core.execute(line);
    m_historyIndex = m_core.history().size();
    if (result.endSession) {
        setReadOnly(true);
        if (onLogout) onLogout();
        return;
    }
    if (result.clear) { clearScreen(); return; }
    if (result.openBrowser) openBrowser(result.browserUrl);
    if (result.openFileManager) openFileManager(result.managerPaths);
    if (!result.editPath.isEmpty()) openVim(result.editPath, result.editContent);
    if (result.openPager) openPager(result.pagerTitle, result.output);
    else if (!result.output.isEmpty()) write(result.output, foreground);
    if (!result.diagnostics.isEmpty()) write(result.diagnostics, errorColor);
    showPrompt();
}

void TerminalWidget::runCommand(const QString &command) {
    setInput(command);
    submit();
}

void TerminalWidget::clearScreen() {
    clear();
    applyAppearance(font(), m_lineSpacing, false);
    showPrompt();
}

void TerminalWidget::completeInput() {
    ensureEditableCursor();
    const int position = textCursor().position() - m_inputStart;
    const auto result = m_core.complete(currentInput(), position);
    if (result.replacement != currentInput()) {
        setInput(result.replacement);
        auto cursor = textCursor();
        cursor.setPosition(m_inputStart + result.cursor);
        setTextCursor(cursor);
    }
    else if (result.candidates.size() > 1) {
        const QString input = currentInput();
        write("\n" + result.candidates.join("    ") + "\n", muted);
        showPrompt();
        setInput(input);
        auto cursor = textCursor();
        cursor.setPosition(m_inputStart + position);
        setTextCursor(cursor);
    }
}

void TerminalWidget::layoutHistorySearch() {
    const int labelWidth = qMin(width() / 2, fontMetrics().horizontalAdvance("reverse-i-search:") + 12);
    m_historySearchLabel->setGeometry(16, height() - 42, labelWidth, 30);
    m_historySearch->setGeometry(16 + labelWidth, height() - 42, qMax(40, width() - labelWidth - 32), 30);
}

void TerminalWidget::beginHistorySearch() {
    ensureEditableCursor();
    m_searchDraft = currentInput();
    m_searchDraftCursor = textCursor().position() - m_inputStart;
    m_searchIndex = m_core.history().size();
    m_historySearch->clear();
    layoutHistorySearch();
    setViewportMargins(0, 0, 0, 42);
    m_historySearchLabel->show();
    m_historySearch->show();
    m_historySearch->raise();
    m_historySearch->setFocus();
    searchHistory(false);
}

void TerminalWidget::searchHistory(bool next) {
    const auto &history = m_core.history();
    const int start = next ? m_searchIndex - 1 : int(history.size()) - 1;
    for (int i = start; i >= 0; --i) {
        if (!history.at(i).contains(m_historySearch->text())) continue;
        m_searchIndex = i;
        setInput(history.at(i));
        m_historySearch->setStyleSheet("QLineEdit { background: #25262a; color: #dedee5; border: 1px solid #92939f; padding: 4px; }");
        return;
    }
    m_historySearch->setStyleSheet("QLineEdit { background: #25262a; color: #f0a6a9; border: 1px solid #f0a6a9; padding: 4px; }");
}

void TerminalWidget::finishHistorySearch(bool cancel) {
    m_historySearch->hide();
    m_historySearchLabel->hide();
    setViewportMargins(0, 0, 0, 0);
    if (cancel) {
        setInput(m_searchDraft);
        auto cursor = textCursor();
        cursor.setPosition(m_inputStart + m_searchDraftCursor);
        setTextCursor(cursor);
    }
    m_historyIndex = cancel ? m_core.history().size() : m_searchIndex;
    m_draft = m_searchDraft;
    setFocus();
}

bool TerminalWidget::eventFilter(QObject *object, QEvent *event) {
    if (object == m_historySearch && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool control = key->modifiers().testFlag(Qt::ControlModifier);
        if (control && key->key() == Qt::Key_R) { searchHistory(true); return true; }
        if (control && (key->key() == Qt::Key_G || key->key() == Qt::Key_C)) {
            finishHistorySearch(true); return true;
        }
        if (key->key() == Qt::Key_Escape) { finishHistorySearch(false); return true; }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            finishHistorySearch(false); submit(); return true;
        }
        if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right
            || key->key() == Qt::Key_Home || key->key() == Qt::Key_End
            || (control && (key->key() == Qt::Key_A || key->key() == Qt::Key_E))) {
            finishHistorySearch(false); keyPressEvent(key); return true;
        }
    }
    return QTextEdit::eventFilter(object, event);
}

void TerminalWidget::killInput(int start, int end, bool backward) {
    if (start == end) return;
    auto cursor = textCursor();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    const QString killed = cursor.selectedText();
    if (m_lastWasKill) m_killBuffer = backward ? killed + m_killBuffer : m_killBuffer + killed;
    else m_killBuffer = killed;
    cursor.removeSelectedText();
    setTextCursor(cursor);
    m_lastWasKill = true;
}

void TerminalWidget::keyPressEvent(QKeyEvent *event) {
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    const bool meta = event->modifiers().testFlag(Qt::MetaModifier);
    const bool alt = event->modifiers().testFlag(Qt::AltModifier);
    const bool kill = control && (event->key() == Qt::Key_U || event->key() == Qt::Key_K || event->key() == Qt::Key_W);
    if (!kill) m_lastWasKill = false;
    if (control && event->key() == Qt::Key_R) { beginHistorySearch(); return; }
    if (kill) {
        ensureEditableCursor();
        const auto cursor = textCursor();
        const int position = cursor.position();
        if (cursor.hasSelection()) killInput(cursor.selectionStart(), cursor.selectionEnd(), false);
        else if (event->key() == Qt::Key_K) killInput(position, document()->characterCount() - 1, false);
        else if (event->key() == Qt::Key_U) killInput(m_inputStart, position, true);
        else {
            const QString input = currentInput();
            int start = position - m_inputStart;
            while (start > 0 && input.at(start - 1).isSpace()) --start;
            while (start > 0 && !input.at(start - 1).isSpace()) --start;
            killInput(m_inputStart + start, position, true);
        }
        return;
    }
    if (control && event->key() == Qt::Key_Y) { ensureEditableCursor(); insertPlainText(m_killBuffer); return; }
    if ((control && (event->key() == Qt::Key_A || event->key() == Qt::Key_E))
        || (alt && (event->key() == Qt::Key_B || event->key() == Qt::Key_F))) {
        ensureEditableCursor();
        auto cursor = textCursor();
        const auto mode = event->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
        if (control) cursor.setPosition(event->key() == Qt::Key_A ? m_inputStart : document()->characterCount() - 1, mode);
        else {
            cursor.movePosition(event->key() == Qt::Key_B ? QTextCursor::PreviousWord : QTextCursor::NextWord, mode);
            if (cursor.position() < m_inputStart) cursor.setPosition(m_inputStart, mode);
        }
        setTextCursor(cursor); return;
    }
    const bool commandModifier = event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::MetaModifier);
    if (commandModifier && event->key() == Qt::Key_Comma) { showAppearanceSettings(); return; }
    if (event->matches(QKeySequence::Copy)) {
        if (textCursor().hasSelection()) { copy(); return; }
        if (event->key() != Qt::Key_C) return;
    }
    if (commandModifier && event->key() == Qt::Key_C) {
        setInput(currentInput() + "^C");
        write("\n\n", muted);
        m_historyIndex = m_core.history().size();
        m_draft.clear();
        showPrompt();
        return;
    }
    if (event->matches(QKeySequence::Paste)) { insertFromMimeData(QGuiApplication::clipboard()->mimeData()); return; }
    if (event->matches(QKeySequence::Cut)) { ensureEditableCursor(); cut(); return; }
    if (event->matches(QKeySequence::SelectAll)) {
        auto cursor = textCursor();
        cursor.setPosition(m_inputStart);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        setTextCursor(cursor);
        return;
    }
    if (commandModifier && event->key() == Qt::Key_L) { clearScreen(); return; }
    if (meta && event->key() == Qt::Key_K) { clearScreen(); return; }
    if (event->key() == Qt::Key_F1) { runCommand("help"); return; }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { submit(); return; }
    if (event->key() == Qt::Key_Tab) { completeInput(); return; }
    if (event->key() == Qt::Key_Up) {
        const auto &history = m_core.history();
        if (m_historyIndex == history.size()) m_draft = currentInput();
        if (m_historyIndex > 0) setInput(history.at(--m_historyIndex));
        return;
    }
    if (event->key() == Qt::Key_Down) {
        const auto &history = m_core.history();
        if (m_historyIndex < history.size()) {
            ++m_historyIndex;
            setInput(m_historyIndex == history.size() ? m_draft : history.at(m_historyIndex));
        }
        return;
    }
    if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) {
        const int delta = event->key() == Qt::Key_PageUp ? -verticalScrollBar()->pageStep() : verticalScrollBar()->pageStep();
        verticalScrollBar()->setValue(verticalScrollBar()->value() + delta);
        return;
    }
    ensureEditableCursor();
    if (event->key() == Qt::Key_Home) {
        auto cursor = textCursor();
        cursor.setPosition(m_inputStart, event->modifiers().testFlag(Qt::ShiftModifier)
            ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
        setTextCursor(cursor);
        return;
    }
    auto cursor = textCursor();
    if ((event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Left)
        && !cursor.hasSelection() && cursor.position() <= m_inputStart) return;
    // Let the editor handle text, selection, and platform shortcuts, but never
    // allow an edit or word movement to cross into the immutable transcript.
    if (commandModifier && event->key() != Qt::Key_Left && event->key() != Qt::Key_Right
        && event->key() != Qt::Key_End && event->key() != Qt::Key_Backspace && event->key() != Qt::Key_Delete)
        return;
    if (commandModifier && (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)) {
        if (!cursor.hasSelection()) {
            cursor.movePosition(event->key() == Qt::Key_Backspace ? QTextCursor::PreviousWord : QTextCursor::NextWord,
                QTextCursor::KeepAnchor);
            if (cursor.selectionStart() < m_inputStart) {
                cursor.setPosition(m_inputStart, QTextCursor::KeepAnchor);
            }
        }
        cursor.removeSelectedText();
        setTextCursor(cursor);
        return;
    }
    QTextEdit::keyPressEvent(event);
    cursor = textCursor();
    if (cursor.position() < m_inputStart) {
        cursor.setPosition(m_inputStart, event->modifiers().testFlag(Qt::ShiftModifier)
            ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
        setTextCursor(cursor);
    }
}

void TerminalWidget::inputMethodEvent(QInputMethodEvent *event) {
    m_lastWasKill = false;
    ensureEditableCursor();
    QTextEdit::inputMethodEvent(event);
}

void TerminalWidget::insertFromMimeData(const QMimeData *source) {
    if (!source || !source->hasText()) return;
    m_lastWasKill = false;
    ensureEditableCursor();
    QString text = source->text();
    text.replace("\r\n", " ").replace('\n', ' ').replace('\r', ' ');
    text.remove(QChar(0));
    insertPlainText(text);
}

void TerminalWidget::applyAppearance(const QFont &chosenFont, int lineSpacing, bool save) {
    const QTextCursor originalCursor = textCursor();
    m_lineSpacing = qBound(100, lineSpacing, 160);
    setFont(chosenFont);
    document()->setDefaultFont(chosenFont);
    QTextCursor all(document());
    all.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(m_lineSpacing, QTextBlockFormat::ProportionalHeight);
    format.setTopMargin(0);
    format.setBottomMargin(0);
    all.mergeBlockFormat(format);
    setTextCursor(originalCursor);
    ensureCursorVisible();
    if (m_vim) m_vim->setAppearance(chosenFont, m_lineSpacing);
    if (m_browser) m_browser->setAppearance(chosenFont, m_lineSpacing);
    if (m_pager) m_pager->setAppearance(chosenFont, m_lineSpacing);
    if (m_fileManager) m_fileManager->setAppearance(chosenFont, m_lineSpacing);
    if (m_historySearch) m_historySearch->setFont(chosenFont);
    if (m_historySearchLabel) { m_historySearchLabel->setFont(chosenFont); layoutHistorySearch(); }
    if (save) {
        QSettings settings;
        settings.setValue("appearance/fontFamily", chosenFont.family());
        settings.setValue("appearance/fontSize", chosenFont.pointSize());
        settings.setValue("appearance/lineSpacing", m_lineSpacing);
        settings.sync();
    }
}

void TerminalWidget::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    if (m_vim) m_vim->setGeometry(rect());
    if (m_browser) m_browser->setGeometry(rect());
    if (m_pager) m_pager->setGeometry(rect());
    if (m_fileManager) m_fileManager->setGeometry(rect());
    if (m_historySearchLabel) layoutHistorySearch();
}

void TerminalWidget::openFileManager(const QStringList &paths) {
    QEventLoop loop;
    m_fileManager = new McFileManager(m_core, paths, font(), m_lineSpacing, [&loop] { loop.quit(); }, this);
    m_fileManager->viewText = [this](const QString &path, const QString &content) { openPager(path, content); };
    m_fileManager->editText = [this](const QString &path, const QString &content) { openVim(path, content); };
    m_fileManager->setGeometry(rect());
    m_fileManager->show();
    m_fileManager->raise();
    m_fileManager->focusPanel();
    loop.exec();
    delete m_fileManager;
    m_fileManager = nullptr;
    setFocus();
}

void TerminalWidget::openPager(const QString &title, const QString &content) {
    QEventLoop loop;
    m_pager = new TextPager(title, content, font(), m_lineSpacing, [&loop] { loop.quit(); }, this);
    m_pager->setGeometry(rect());
    m_pager->show();
    m_pager->raise();
    m_pager->focusPage();
    loop.exec();
    delete m_pager;
    m_pager = nullptr;
    setFocus();
}

void TerminalWidget::openBrowser(const QString &address) {
    QEventLoop loop;
    const QString stateFile = QFileInfo(m_core.worldRoot()).dir().filePath("browser.json");
    m_browser = new TextBrowser(stateFile, address, font(), m_lineSpacing, [&loop] { loop.quit(); }, this);
    m_browser->setGeometry(rect());
    m_browser->show();
    m_browser->raise();
    m_browser->focusPage();
    loop.exec();
    delete m_browser;
    m_browser = nullptr;
    write("Browser closed.\n", muted);
    setFocus();
}

void TerminalWidget::openVim(const QString &path, const QString &content) {
    QEventLoop loop;
    m_vim = new VimEditor(path, content, font(), m_lineSpacing,
        [this, path](const QString &text) { return m_core.saveFile(path, text); },
        [&loop] { loop.quit(); }, this);
    m_vim->setGeometry(rect());
    m_vim->show();
    m_vim->raise();
    m_vim->focusBuffer();
    loop.exec();
    delete m_vim;
    m_vim = nullptr;
    write("Closed vim: " + path + '\n', muted);
    setFocus();
}

void TerminalWidget::showAppearanceSettings() {
    const QFont originalFont = font();
    const int originalSpacing = m_lineSpacing;
    QDialog dialog(window());
    dialog.setWindowTitle("Font & spacing");
    dialog.setMinimumWidth(510);
    dialog.setStyleSheet(
        "QDialog { background: #25262a; color: #dedee5; }"
        "QLabel { color: #dedee5; }"
        "QComboBox, QSpinBox { background: #1c1c1f; color: #dedee5; border: 1px solid #484852; border-radius: 5px; padding: 6px; }"
        "QComboBox QAbstractItemView { background: #25262a; color: #dedee5; selection-background-color: #45465b; }"
        "QPushButton { background: #36363f; color: #dedee5; border: 1px solid #4b4b57; border-radius: 5px; padding: 7px 13px; }"
        "QPushButton:hover { background: #454550; }"
        "QPushButton:default { background: #c0c5ed; color: #202129; border-color: #c0c5ed; }");
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(18);
    auto *description = new QLabel("Make the terminal comfortable to read.", &dialog);
    description->setStyleSheet("color: #a5a5b0; font-size: 12px;");
    layout->addWidget(description);

    auto *form = new QFormLayout;
    form->setSpacing(12);
    auto *fontBox = new QFontComboBox(&dialog);
    fontBox->setObjectName("appearanceFont");
    fontBox->setFontFilters(QFontComboBox::MonospacedFonts);
    fontBox->setCurrentFont(originalFont);
    auto *sizeBox = new QSpinBox(&dialog);
    sizeBox->setObjectName("appearanceSize");
    sizeBox->setRange(8, 32);
    sizeBox->setSuffix(" pt");
    sizeBox->setValue(originalFont.pointSize());
    auto *spacingBox = new QSpinBox(&dialog);
    spacingBox->setObjectName("appearanceSpacing");
    spacingBox->setRange(100, 160);
    spacingBox->setSingleStep(5);
    spacingBox->setSuffix(" %");
    spacingBox->setValue(originalSpacing);
    form->addRow("Font", fontBox);
    form->addRow("Size", sizeBox);
    form->addRow("Line spacing", spacingBox);
    layout->addLayout(form);

    auto *preview = new QTextEdit(&dialog);
    preview->setReadOnly(true);
    preview->setAcceptRichText(false);
    preview->setFixedHeight(180);
    preview->document()->setDocumentMargin(14);
    preview->setStyleSheet("QTextEdit { background: #1c1c1f; color: #dedee5; border: 1px solid #3c3c46; border-radius: 6px; }");
    preview->setPlainText(QString::fromUtf8(
        "guest@silly:~$ ls\ndocuments/  projects/  notes.txt\nguest@silly:~$ echo hello\nhello — 你好"));
    layout->addWidget(preview);

    const auto updatePreview = [this, fontBox, sizeBox, spacingBox, preview] {
        QFont chosen = fontBox->currentFont();
        chosen.setPointSize(sizeBox->value());
        applyAppearance(chosen, spacingBox->value(), false);
        preview->setFont(chosen);
        preview->document()->setDefaultFont(chosen);
        QTextCursor all(preview->document());
        all.select(QTextCursor::Document);
        QTextBlockFormat format;
        format.setLineHeight(spacingBox->value(), QTextBlockFormat::ProportionalHeight);
        all.mergeBlockFormat(format);
    };
    QObject::connect(fontBox, &QFontComboBox::currentFontChanged, &dialog, updatePreview);
    QObject::connect(sizeBox, &QSpinBox::valueChanged, &dialog, updatePreview);
    QObject::connect(spacingBox, &QSpinBox::valueChanged, &dialog, updatePreview);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
        &dialog, [fontBox, sizeBox, spacingBox] {
            fontBox->setCurrentFont(TerminalAppearance::defaultFont());
            sizeBox->setValue(12);
            spacingBox->setValue(115);
        });
    layout->addWidget(buttons);
    updatePreview();
    if (dialog.exec() == QDialog::Accepted) {
        QFont chosen = fontBox->currentFont();
        chosen.setPointSize(sizeBox->value());
        applyAppearance(chosen, spacingBox->value(), true);
    } else {
        applyAppearance(originalFont, originalSpacing, false);
    }
    setFocus();
}

void TerminalWidget::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    auto *copyAction = menu.addAction("Copy", this, [this] { copy(); });
    copyAction->setEnabled(textCursor().hasSelection());
    menu.addAction("Paste", this, [this] { insertFromMimeData(QGuiApplication::clipboard()->mimeData()); });
    menu.addSeparator();
    menu.addAction("Font & spacing...", this, [this] { showAppearanceSettings(); });
    menu.addAction("Clear screen", this, [this] { clearScreen(); });
    menu.exec(event->globalPos());
}

bool TerminalWidget::smokeTest(const QString &appearancePreviewPath) {
    if (!m_core.initializationError().isEmpty()) {
        qWarning("UI smoke world initialization failed: %s", qPrintable(m_core.initializationError()));
        return false;
    }
    runCommand("ls");
    runCommand("cd documents");
    runCommand("cat welcome.txt");
    if (!toPlainText().contains(QString::fromUtf8("你好")) || !toPlainText().endsWith("~/documents$ ")) { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    setInput("draft");
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    keyPressEvent(&up);
    if (currentInput() != "cat welcome.txt") { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    keyPressEvent(&down);
    if (currentInput() != "draft") { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    setInput("pw");
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    keyPressEvent(&tab);
    if (currentInput() != "pwd ") { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    setInput("");
    const QString transcript = toPlainText();
    auto cursor = textCursor();
    cursor.setPosition(0);
    setTextCursor(cursor);
    QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    keyPressEvent(&backspace);
    if (toPlainText() != transcript) { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    QKeyEvent wordDelete(QEvent::KeyPress, Qt::Key_Backspace, Qt::ControlModifier);
    keyPressEvent(&wordDelete);
    if (toPlainText() != transcript) { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    QInputMethodEvent chineseInput;
    chineseInput.setCommitString(QString::fromUtf8("中文输入"));
    inputMethodEvent(&chineseInput);
    if (currentInput() != QString::fromUtf8("中文输入")) { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    setInput("");
    QMimeData paste;
    paste.setText("one\ntwo");
    insertFromMimeData(&paste);
    if (currentInput() != "one two") { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    setInput("");
    runCommand("pwd");
    // The smoke-test process redirects preferences to a build-local INI store.
    // Check preview, cancellation, saving, and loading without touching user settings.
    const QFont savedFont = font();
    const int savedSpacing = m_lineSpacing;
    setInput("echo pending");
    const QString savedTranscript = toPlainText();
    bool dialogChecked = false;
    QTimer::singleShot(100, this, [this, &dialogChecked, appearancePreviewPath] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto *size = dialog->findChild<QSpinBox *>("appearanceSize");
        auto *spacing = dialog->findChild<QSpinBox *>("appearanceSpacing");
        auto *family = dialog->findChild<QFontComboBox *>("appearanceFont");
        if (size && spacing && family && family->count() > 0) {
            const int originalIndex = family->currentIndex();
            family->setCurrentIndex((originalIndex + 1) % family->count());
            dialogChecked = font().family() == family->currentFont().family();
            family->setCurrentIndex(originalIndex);
            size->setValue(14);
            spacing->setValue(125);
            dialogChecked = dialogChecked && font().pointSize() == 14 && m_lineSpacing == 125;
            if (!appearancePreviewPath.isEmpty())
                dialogChecked = dialog->grab().save(appearancePreviewPath) && dialogChecked;
        }
        dialog->reject();
    });
    showAppearanceSettings();
    if (!dialogChecked || font() != savedFont || m_lineSpacing != savedSpacing
        || toPlainText() != savedTranscript || currentInput() != "echo pending") { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    bool saved = false;
    QTimer::singleShot(100, this, [&saved] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto *size = dialog->findChild<QSpinBox *>("appearanceSize");
        auto *spacing = dialog->findChild<QSpinBox *>("appearanceSpacing");
        if (size && spacing) {
            size->setValue(14);
            spacing->setValue(125);
            saved = true;
            dialog->accept();
        } else dialog->reject();
    });
    showAppearanceSettings();
    if (!saved) { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    TerminalWidget reloaded(nullptr, m_core.worldRoot());
    if (reloaded.font().pointSize() != 14 || reloaded.m_lineSpacing != 125
        || toPlainText() != savedTranscript) { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    applyAppearance(savedFont, savedSpacing, true);
    setInput("");
    bool vimChecked = false;
    QTimer::singleShot(100, this, [this, &vimChecked, appearancePreviewPath] {
        auto *buffer = m_vim ? m_vim->findChild<QTextEdit *>("vimBuffer") : nullptr;
        auto *command = m_vim ? m_vim->findChild<QLineEdit *>("vimCommand") : nullptr;
        if (!buffer || !command) return;
        QKeyEvent insert(QEvent::KeyPress, Qt::Key_I, Qt::NoModifier, "i");
        QCoreApplication::sendEvent(buffer, &insert);
        QInputMethodEvent text;
        text.setCommitString(QString::fromUtf8("A note written in vim.\n你好，Vim。\n"));
        QCoreApplication::sendEvent(buffer, &text);
        if (!appearancePreviewPath.isEmpty())
            vimChecked = m_vim->grab().save(appearancePreviewPath + ".vim.png");
        else vimChecked = true;
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(buffer, &escape);
        QKeyEvent colon(QEvent::KeyPress, Qt::Key_Colon, Qt::NoModifier, ":");
        QCoreApplication::sendEvent(buffer, &colon);
        command->setText(":wq");
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(command, &enter);
    });
    runCommand("vim vim-check.txt");
    if (!vimChecked || m_core.execute("cat vim-check.txt").text
        != QString::fromUtf8("A note written in vim.\n你好，Vim。\n")) { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    runCommand("cat vim-check.txt");
    bool browserChecked = false;
    QTimer::singleShot(100, this, [this, &browserChecked, appearancePreviewPath] {
        auto *view = m_browser ? m_browser->findChild<QTextBrowser *>("browserPage") : nullptr;
        auto *address = m_browser ? m_browser->findChild<QLineEdit *>("browserAddress") : nullptr;
        if (!view || !address) return;
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &enter);
        browserChecked = view->toPlainText().contains("DAYBREAK");
        if (!appearancePreviewPath.isEmpty())
            browserChecked = m_browser->grab().save(appearancePreviewPath + ".website.png") && browserChecked;
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &down);
        QCoreApplication::sendEvent(view, &enter);
        browserChecked = browserChecked && address->text() == "https://www.daybreak.news/about";
        QKeyEvent back(QEvent::KeyPress, Qt::Key_Left, Qt::AltModifier);
        QCoreApplication::sendEvent(view, &back);
        browserChecked = browserChecked && view->toPlainText().contains("DAYBREAK");
        address->setText("linnotes.me/about");
        QCoreApplication::sendEvent(address, &enter);
        browserChecked = browserChecked && address->text() == "https://linnotes.me/about";
        address->setText("bbs.afterhours.net");
        QCoreApplication::sendEvent(address, &enter);
        if (!appearancePreviewPath.isEmpty())
            browserChecked = m_browser->grab().save(appearancePreviewPath + ".forum.png") && browserChecked;
        if (!appearancePreviewPath.isEmpty()) {
            const QSize original = m_browser->size();
            m_browser->resize(560, 360);
            browserChecked = m_browser->grab().save(appearancePreviewPath + ".narrow.png") && browserChecked;
            m_browser->resize(original);
        }
        address->setText("bbs.afterhours.net/t/47");
        QCoreApplication::sendEvent(address, &enter);
        if (!appearancePreviewPath.isEmpty())
            browserChecked = m_browser->grab().save(appearancePreviewPath + ".thread.png") && browserChecked;
        address->setText("linnotes.me");
        QCoreApplication::sendEvent(address, &enter);
        if (!appearancePreviewPath.isEmpty())
            browserChecked = m_browser->grab().save(appearancePreviewPath + ".blog.png") && browserChecked;
        address->setText("hcmetro.com");
        QCoreApplication::sendEvent(address, &enter);
        browserChecked = browserChecked && address->text() == "https://www.hcmetro.com/";
        if (!appearancePreviewPath.isEmpty())
            browserChecked = m_browser->grab().save(appearancePreviewPath + ".transit.png") && browserChecked;
        address->setText("hcmetro.com/lines/loop");
        QCoreApplication::sendEvent(address, &enter);
        browserChecked = browserChecked && address->text() == "https://www.hcmetro.com/lines/loop";
        if (!appearancePreviewPath.isEmpty())
            browserChecked = m_browser->grab().save(appearancePreviewPath + ".timetable.png") && browserChecked;
        address->setText(BrowserCore::homeUrl());
        QCoreApplication::sendEvent(address, &enter);
        if (!appearancePreviewPath.isEmpty())
            browserChecked = m_browser->grab().save(appearancePreviewPath + ".browser.png") && browserChecked;
        QKeyEvent quit(QEvent::KeyPress, Qt::Key_Q, Qt::NoModifier, "q");
        QCoreApplication::sendEvent(view, &quit);
    });
    runCommand("browser");
    BrowserCore restoredBrowser(QFileInfo(m_core.worldRoot()).dir().filePath("browser.json"));
    if (!browserChecked || restoredBrowser.currentUrl() != BrowserCore::homeUrl()
        || !toPlainText().contains("A note written in vim.")) { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    const QString beforeManager = toPlainText();
    const QString directoryBeforeManager = m_core.currentPath();
    bool managerChecked = false;
    QTimer::singleShot(100, this, [this, &managerChecked, appearancePreviewPath] {
        auto *quit = m_fileManager ? m_fileManager->findChild<QPushButton *>("mcAction10") : nullptr;
        if (!quit) return;
        managerChecked = m_fileManager->findChild<QWidget *>("mcLeftPanel")
            && m_fileManager->findChild<QWidget *>("mcRightPanel");
        if (!appearancePreviewPath.isEmpty())
            managerChecked = m_fileManager->grab().save(appearancePreviewPath + ".mc.png") && managerChecked;
        quit->click();
    });
    runCommand("mc");
    if (!managerChecked || m_fileManager || m_core.currentPath() != directoryBeforeManager
        || !toPlainText().startsWith(beforeManager) || !toPlainText().endsWith(m_core.prompt())) {
        qWarning("UI smoke mc launch/return failed");
        return false;
    }
    return true;
}
