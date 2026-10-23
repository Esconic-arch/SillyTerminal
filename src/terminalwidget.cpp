#include "terminalwidget.h"
#include "vimeditor.h"
#include "textbrowser.h"

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
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
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
QFont defaultTerminalFont() {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const auto families = QFontDatabase::families();
    for (const QString &family : {QStringLiteral("SF Mono"), QStringLiteral("Menlo"),
             QStringLiteral("Cascadia Mono"), QStringLiteral("Cascadia Code"),
             QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono")}) {
        if (families.contains(family)) { font.setFamily(family); break; }
    }
    font.setPointSize(12);
    return font;
}
}

TerminalWidget::TerminalWidget(QWidget *parent, const QString &worldRoot)
    : QTextEdit(parent), m_core(worldRoot) {
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
    QFont initialFont = defaultTerminalFont();
    {
        QSettings settings;
        const QString family = settings.value("appearance/fontFamily").toString();
        if (QFontDatabase::families().contains(family)) initialFont.setFamily(family);
        initialFont.setPointSize(qBound(8, settings.value("appearance/fontSize", 12).toInt(), 32));
        m_lineSpacing = qBound(100, settings.value("appearance/lineSpacing", 115).toInt(), 160);
    }
    applyAppearance(initialFont, m_lineSpacing, false);
    write("SillyTerminal 0.1.0\n", foreground);
    write("Type help for commands, or cat notes.txt to begin.\n\n", muted);
    if (!m_core.initializationError().isEmpty()) write(m_core.initializationError() + '\n', errorColor);
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
    write("guest@silly", foreground);
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
    if (!line.trimmed().isEmpty() && (m_history.isEmpty() || m_history.last() != line))
        m_history.append(line);
    m_historyIndex = m_history.size();
    m_draft.clear();
    write("\n", foreground);
    const CommandResult result = m_core.execute(line);
    if (result.clear) { clearScreen(); return; }
    if (result.openBrowser) openBrowser(result.browserUrl);
    if (!result.editPath.isEmpty()) {
        if (result.useVim) openVim(result.editPath, result.editContent);
        else openEditor(result.editPath, result.editContent);
    }
    if (!result.text.isEmpty()) write(result.text + '\n', result.error ? errorColor : foreground);
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
    const auto result = m_core.complete(currentInput());
    if (result.replacement != currentInput()) setInput(result.replacement);
    else if (result.candidates.size() > 1) {
        const QString input = currentInput();
        write("\n" + result.candidates.join("    ") + "\n", muted);
        showPrompt();
        setInput(input);
    }
}

void TerminalWidget::keyPressEvent(QKeyEvent *event) {
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
        m_historyIndex = m_history.size();
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
    if (commandModifier && event->key() == Qt::Key_K) { clearScreen(); return; }
    if (event->key() == Qt::Key_F1) { runCommand("help"); return; }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { submit(); return; }
    if (event->key() == Qt::Key_Tab) { completeInput(); return; }
    if (event->key() == Qt::Key_Up) {
        if (m_historyIndex == m_history.size()) m_draft = currentInput();
        if (m_historyIndex > 0) setInput(m_history.at(--m_historyIndex));
        return;
    }
    if (event->key() == Qt::Key_Down) {
        if (m_historyIndex < m_history.size()) {
            ++m_historyIndex;
            setInput(m_historyIndex == m_history.size() ? m_draft : m_history.at(m_historyIndex));
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
    ensureEditableCursor();
    QTextEdit::inputMethodEvent(event);
}

void TerminalWidget::insertFromMimeData(const QMimeData *source) {
    if (!source || !source->hasText()) return;
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

void TerminalWidget::openEditor(const QString &path, const QString &content) {
    QDialog dialog(window());
    dialog.setWindowTitle("Edit " + path);
    dialog.resize(760, 540);
    dialog.setMinimumSize(440, 300);
    dialog.setStyleSheet(
        "QDialog { background: #25262a; } QLabel { color: #b2b2bd; }"
        "QPushButton { background: #36363f; color: #dedee5; border: 1px solid #4b4b57; border-radius: 5px; padding: 7px 16px; }"
        "QPushButton:hover { background: #454550; }"
        "QPushButton:default { background: #c0c5ed; color: #202129; }");
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);
    auto *title = new QLabel(path, &dialog);
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    layout->addWidget(title);
    auto *editor = new QTextEdit(&dialog);
    editor->setObjectName("fileEditor");
    editor->setAcceptRichText(false);
    editor->setFont(font());
    editor->document()->setDefaultFont(font());
    editor->document()->setDocumentMargin(16);
    editor->setStyleSheet("QTextEdit { background: #1c1c1f; color: #dedee5; border: 1px solid #3c3c46; border-radius: 5px; selection-background-color: #45465b; }");
    editor->setPlainText(content);
    QTextCursor all(editor->document());
    all.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(m_lineSpacing, QTextBlockFormat::ProportionalHeight);
    all.mergeBlockFormat(format);
    layout->addWidget(editor, 1);
    auto *status = new QLabel("UTF-8   |   Ctrl+S / Command+S to save   |   Esc to cancel", &dialog);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName("editorButtons");
    layout->addWidget(buttons);
    const auto save = [this, &dialog, path, editor, status] {
        const auto result = m_core.saveFile(path, editor->toPlainText());
        if (result.error) {
            status->setStyleSheet("color: #f0a6a9;");
            status->setText(result.text);
            return;
        }
        dialog.accept();
    };
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, save);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *saveShortcut = new QShortcut(QKeySequence::Save, &dialog);
    QObject::connect(saveShortcut, &QShortcut::activated, &dialog, save);
    editor->setFocus();
    if (dialog.exec() == QDialog::Accepted) write("Saved " + path + '\n', muted);
    else write("Edit cancelled.\n", muted);
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
            fontBox->setCurrentFont(defaultTerminalFont());
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
    bool editorSaved = false;
    QTimer::singleShot(100, this, [&editorSaved, appearancePreviewPath] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto *editor = dialog->findChild<QTextEdit *>("fileEditor");
        auto *buttons = dialog->findChild<QDialogButtonBox *>("editorButtons");
        if (editor && buttons) {
            editor->setPlainText(QString::fromUtf8("Saved from the editor.\n你好，终端。\n"));
            editorSaved = true;
            if (!appearancePreviewPath.isEmpty())
                editorSaved = dialog->grab().save(appearancePreviewPath + ".editor.png");
            buttons->button(QDialogButtonBox::Save)->click();
        } else dialog->reject();
    });
    runCommand("edit editor-check.txt");
    if (!editorSaved || m_core.execute("cat editor-check.txt").text
        != QString::fromUtf8("Saved from the editor.\n你好，终端。\n")) { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    QTimer::singleShot(100, this, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (dialog) dialog->reject();
    });
    runCommand("edit cancelled.txt");
    if (!m_core.execute("cat cancelled.txt").error) { qWarning("UI smoke check failed at line %d", __LINE__); return false; }
    runCommand("cat editor-check.txt");
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
        browserChecked = browserChecked && address->text() == "https://www.daybreak.news/culture/bookshop";
        QKeyEvent back(QEvent::KeyPress, Qt::Key_Left, Qt::AltModifier);
        QCoreApplication::sendEvent(view, &back);
        browserChecked = browserChecked && view->toPlainText().contains("DAYBREAK");
        address->setText("linnotes.me/about");
        QCoreApplication::sendEvent(address, &enter);
        browserChecked = browserChecked && address->text() == "https://linnotes.me/about";
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
    return true;
}
