#include "vimeditor.h"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextEdit>
#include <QVBoxLayout>
#include <utility>

class VimBuffer : public QTextEdit {
public:
    explicit VimBuffer(VimEditor *owner) : QTextEdit(owner), m_owner(owner) {
        setObjectName("vimBuffer");
        setAcceptRichText(false);
        setAcceptDrops(false);
        setTabChangesFocus(false);
        setFrameStyle(QFrame::NoFrame);
        setContextMenuPolicy(Qt::NoContextMenu);
        document()->setDocumentMargin(18);
        setStyleSheet("QTextEdit { background: #1c1c1f; color: #dedee5; selection-background-color: #45465b; selection-color: #fff; }"
            "QScrollBar:vertical { background: #1c1c1f; width: 7px; }"
            "QScrollBar::handle:vertical { background: #45454d; min-height: 28px; border-radius: 3px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }");
        m_gutter = new Gutter(this);
        setViewportMargins(48, 0, 0, 0);
        QObject::connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { m_gutter->update(); });
        QObject::connect(this, &QTextEdit::textChanged, this, [this] { m_gutter->update(); });
        QObject::connect(this, &QTextEdit::cursorPositionChanged, this, [this] { m_gutter->update(); });
    }

protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (!m_owner->handleBufferKey(event)) QTextEdit::keyPressEvent(event);
        m_owner->refreshStatus();
    }
    void inputMethodEvent(QInputMethodEvent *event) override {
        if (m_owner->m_mode == VimEditor::Mode::Insert) QTextEdit::inputMethodEvent(event);
        else event->ignore();
    }
    void insertFromMimeData(const QMimeData *source) override {
        if (m_owner->m_mode != VimEditor::Mode::Insert || !source->hasText()) return;
        QString text = source->text();
        text.replace("\r\n", "\n").replace('\r', '\n');
        insertPlainText(text);
    }
    void resizeEvent(QResizeEvent *event) override {
        QTextEdit::resizeEvent(event);
        m_gutter->setGeometry(0, 0, 48, viewport()->height());
    }

private:
    class Gutter : public QWidget {
    public:
        explicit Gutter(VimBuffer *buffer) : QWidget(buffer), m_buffer(buffer) {}
    protected:
        void paintEvent(QPaintEvent *) override {
            QPainter painter(this);
            painter.fillRect(rect(), QColor("#1c1c1f"));
            painter.setFont(m_buffer->font());
            const qreal offset = m_buffer->verticalScrollBar()->value();
            const qreal ascent = QFontMetricsF(m_buffer->font()).ascent();
            for (QTextBlock block = m_buffer->document()->begin(); block.isValid(); block = block.next()) {
                const QRectF bounds = m_buffer->document()->documentLayout()->blockBoundingRect(block);
                const qreal top = bounds.top() - offset;
                if (top > height()) break;
                if (bounds.bottom() < offset) continue;
                painter.setPen(block == m_buffer->textCursor().block() ? QColor("#b9bedc") : QColor("#64646f"));
                painter.drawText(QRectF(0, top, width() - 8, ascent + 5), Qt::AlignRight | Qt::AlignTop,
                    QString::number(block.blockNumber() + 1));
            }
        }
    private:
        VimBuffer *m_buffer;
    };
    VimEditor *m_owner;
    Gutter *m_gutter;
};

class VimCommandLine : public QLineEdit {
public:
    explicit VimCommandLine(VimEditor *owner) : QLineEdit(owner), m_owner(owner) {
        setObjectName("vimCommand");
        setFrame(false);
        setReadOnly(true);
        setStyleSheet("QLineEdit { color: #dedee5; background: #1c1c1f; padding: 8px 16px; border: none; }");
    }
protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape || (event->modifiers().testFlag(Qt::ControlModifier) && event->key() == Qt::Key_C)) {
            m_owner->enterNormal();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            m_owner->executeCommand();
            return;
        }
        if (event->key() == Qt::Key_Backspace && text().size() <= 1) {
            m_owner->enterNormal();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
private:
    VimEditor *m_owner;
};

VimEditor::VimEditor(const QString &path, const QString &content, const QFont &font,
    int lineSpacing, SaveHandler save, CloseHandler close, QWidget *parent)
    : QWidget(parent), m_path(path), m_savedText(content), m_save(std::move(save)), m_close(std::move(close)) {
    setObjectName("vimEditor");
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet("#vimEditor { background: #1c1c1f; }");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_buffer = new VimBuffer(this);
    m_buffer->setUndoRedoEnabled(false);
    m_buffer->setPlainText(content);
    m_savedText = m_buffer->toPlainText();
    layout->addWidget(m_buffer, 1);
    auto *status = new QWidget(this);
    status->setStyleSheet("background: #29292e;");
    auto *row = new QHBoxLayout(status);
    row->setContentsMargins(16, 7, 16, 7);
    row->setSpacing(16);
    m_modeLabel = new QLabel(status);
    m_modeLabel->setObjectName("vimMode");
    m_fileLabel = new QLabel(status);
    m_fileLabel->setTextFormat(Qt::PlainText);
    m_fileLabel->setStyleSheet("color: #c0c0ca;");
    m_positionLabel = new QLabel(status);
    m_positionLabel->setStyleSheet("color: #92939f;");
    row->addWidget(m_modeLabel);
    row->addWidget(m_fileLabel, 1);
    row->addWidget(m_positionLabel);
    layout->addWidget(status);
    m_command = new VimCommandLine(this);
    layout->addWidget(m_command);
    setAppearance(font, lineSpacing);
    m_buffer->setUndoRedoEnabled(true);
    QObject::connect(m_buffer, &QTextEdit::textChanged, this, [this] { refreshStatus(); });
    QObject::connect(m_buffer, &QTextEdit::cursorPositionChanged, this, [this] { refreshStatus(); });
    enterNormal();
}

void VimEditor::setAppearance(const QFont &font, int lineSpacing) {
    m_buffer->setFont(font);
    m_buffer->document()->setDefaultFont(font);
    m_buffer->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(' ') * 4);
    const QTextCursor original = m_buffer->textCursor();
    QTextCursor all(m_buffer->document());
    all.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(lineSpacing, QTextBlockFormat::ProportionalHeight);
    format.setTopMargin(0);
    format.setBottomMargin(0);
    all.mergeBlockFormat(format);
    m_buffer->setTextCursor(original);
    QFont small = font;
    small.setPointSize(qMax(9, font.pointSize() - 2));
    m_modeLabel->setFont(small);
    m_fileLabel->setFont(small);
    m_positionLabel->setFont(small);
    m_command->setFont(small);
}

void VimEditor::focusBuffer() { m_buffer->setFocus(); }
bool VimEditor::modified() const { return m_buffer->toPlainText() != m_savedText; }

void VimEditor::refreshStatus() {
    QString mode = "NORMAL";
    if (m_mode == Mode::Insert) mode = "INSERT";
    else if (m_mode == Mode::Command) mode = "COMMAND";
    else if (m_mode == Mode::Search) mode = "SEARCH";
    m_modeLabel->setText(mode);
    m_modeLabel->setStyleSheet(m_mode == Mode::Insert ? "color: #a5d7bb; font-weight: 600;" : "color: #c0c5ed; font-weight: 600;");
    m_fileLabel->setText(m_path + (modified() ? "  [+]" : ""));
    const auto cursor = m_buffer->textCursor();
    m_positionLabel->setText(QString("Ln %1, Col %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
}

void VimEditor::message(const QString &text, bool error) {
    m_command->setReadOnly(true);
    m_command->setStyleSheet(QString("QLineEdit { color: %1; background: #1c1c1f; padding: 8px 16px; border: none; }")
        .arg(error ? "#f0a6a9" : "#92939f"));
    m_command->setText(text);
    m_command->setCursorPosition(0);
}

void VimEditor::clampNormalCursor() {
    auto cursor = m_buffer->textCursor();
    const auto block = cursor.block();
    if (block.length() > 1 && cursor.positionInBlock() >= block.length() - 1) {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.movePosition(QTextCursor::PreviousCharacter);
    }
    cursor.clearSelection();
    m_buffer->setTextCursor(cursor);
}

void VimEditor::enterNormal(bool fromInsert) {
    if (fromInsert) {
        auto cursor = m_buffer->textCursor();
        if (cursor.positionInBlock() > 0) cursor.movePosition(QTextCursor::PreviousCharacter);
        m_buffer->setTextCursor(cursor);
    }
    m_mode = Mode::Normal;
    m_pending.clear();
    m_buffer->setAttribute(Qt::WA_InputMethodEnabled, false);
    clampNormalCursor();
    message("i insert   Esc normal   :w save   :q quit   / search   F1 help");
    refreshStatus();
    focusBuffer();
}

void VimEditor::enterInsert() {
    m_mode = Mode::Insert;
    m_pending.clear();
    m_buffer->setAttribute(Qt::WA_InputMethodEnabled, true);
    message("-- INSERT --   Esc returns to normal mode");
    refreshStatus();
    focusBuffer();
}

void VimEditor::startCommand(Mode mode) {
    m_mode = mode;
    m_pending.clear();
    m_command->setReadOnly(false);
    m_command->setStyleSheet("QLineEdit { color: #dedee5; background: #1c1c1f; padding: 8px 16px; border: none; }");
    m_command->setText(mode == Mode::Command ? ":" : "/");
    m_command->setCursorPosition(1);
    m_command->setFocus();
    refreshStatus();
}

bool VimEditor::save() {
    const auto result = m_save(m_buffer->toPlainText());
    if (result.error) { message("Write failed: " + result.text, true); return false; }
    m_savedText = m_buffer->toPlainText();
    message(QString("%1  —  %2 lines written").arg(m_path).arg(m_buffer->document()->blockCount()));
    refreshStatus();
    return true;
}

void VimEditor::quit(bool force) {
    if (!force && modified()) { message("E37: Unsaved changes. Use :w to save or :q! to discard.", true); return; }
    m_closed = true;
    m_close();
}

void VimEditor::executeCommand() {
    const QString text = m_command->text();
    const bool search = m_mode == Mode::Search;
    enterNormal();
    if (search) {
        const QString pattern = text.startsWith('/') ? text.mid(1) : text;
        if (!pattern.isEmpty()) m_search = pattern;
        findNext();
        return;
    }
    const QString command = (text.startsWith(':') ? text.mid(1) : text).trimmed();
    if (command == "w") save();
    else if (command == "q") quit(false);
    else if (command == "q!") quit(true);
    else if (command == "wq") { if (save()) quit(false); }
    else if (command == "x") { if (!modified() || save()) quit(false); }
    else if (command == "help") message("i/a/I/A/o/O insert | hjkl/arrows move | gg/G 0/$ w/b | x dd yy p u | Ctrl+R redo | / n N");
    else if (!command.isEmpty()) message("Not supported: :" + command + "   (try :w, :q, :wq, :q!, :help)", true);
}

void VimEditor::findNext(bool backward) {
    if (m_search.isEmpty()) { message("No previous search pattern.", true); return; }
    const int position = m_buffer->textCursor().position();
    const auto flags = backward ? QTextDocument::FindBackward : QTextDocument::FindFlags{};
    auto match = m_buffer->document()->find(m_search, backward ? qMax(0, position) : position + 1, flags);
    if (match.isNull()) match = m_buffer->document()->find(m_search,
        backward ? m_buffer->document()->characterCount() - 1 : 0, flags);
    if (match.isNull()) { message("Pattern not found: " + m_search, true); return; }
    QTextEdit::ExtraSelection highlight;
    highlight.cursor = match;
    highlight.format.setBackground(QColor("#645333"));
    highlight.format.setForeground(QColor("#ffffff"));
    m_buffer->setExtraSelections({highlight});
    match.setPosition(match.selectionStart());
    m_buffer->setTextCursor(match);
    m_buffer->ensureCursorVisible();
    message('/' + m_search);
}

void VimEditor::deleteLine() {
    auto cursor = m_buffer->textCursor();
    const auto block = cursor.block();
    m_register = block.text() + '\n';
    m_registerIsLine = true;
    int start = block.position();
    int end = block.next().isValid() ? block.next().position() : m_buffer->document()->characterCount() - 1;
    if (!block.next().isValid() && block.previous().isValid()) --start;
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    m_buffer->setTextCursor(cursor);
    clampNormalCursor();
}

void VimEditor::pasteRegister() {
    if (m_register.isEmpty()) { message("Nothing has been yanked or deleted."); return; }
    auto cursor = m_buffer->textCursor();
    cursor.beginEditBlock();
    int position;
    if (m_registerIsLine) {
        if (m_buffer->toPlainText().isEmpty()) {
            position = 0;
            cursor.insertText(m_register.chopped(1));
        } else if (cursor.block().next().isValid()) {
            cursor.movePosition(QTextCursor::NextBlock);
            cursor.movePosition(QTextCursor::StartOfBlock);
            position = cursor.position();
            cursor.insertText(m_register);
        } else {
            cursor.movePosition(QTextCursor::EndOfBlock);
            position = cursor.position() + 1;
            cursor.insertText('\n' + m_register.chopped(1));
        }
    } else {
        if (!cursor.atBlockEnd()) cursor.movePosition(QTextCursor::NextCharacter);
        position = cursor.position();
        cursor.insertText(m_register);
    }
    cursor.endEditBlock();
    cursor.setPosition(position);
    m_buffer->setTextCursor(cursor);
    clampNormalCursor();
}

bool VimEditor::handleBufferKey(QKeyEvent *event) {
    if (m_closed) return true;
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    if (event->key() == Qt::Key_Escape || (control && event->key() == Qt::Key_BracketLeft)) {
        enterNormal(m_mode == Mode::Insert);
        return true;
    }
    if (event->key() == Qt::Key_F1) {
        message("i insert | Esc normal | :w save | :q quit | :q! discard | :help more keys");
        return true;
    }
    if (event->matches(QKeySequence::Copy)) { m_buffer->copy(); return true; }
    if (event->matches(QKeySequence::Save)) { save(); return true; }
    if (m_mode == Mode::Insert) return false;
    if (m_mode != Mode::Normal) return true;
    if (control && event->key() == Qt::Key_R) {
        m_buffer->redo(); clampNormalCursor(); return true;
    }
    if (control || event->modifiers().testFlag(Qt::MetaModifier) || event->modifiers().testFlag(Qt::AltModifier)) return true;
    clampNormalCursor();
    auto cursor = m_buffer->textCursor();
    const QString key = event->text();
    if (key == ":") { startCommand(Mode::Command); return true; }
    if (key == "/") { startCommand(Mode::Search); return true; }
    if (!m_pending.isEmpty()) {
        const QString sequence = m_pending + key;
        m_pending.clear();
        if (sequence == "dd") deleteLine();
        else if (sequence == "yy") {
            m_register = cursor.block().text() + '\n'; m_registerIsLine = true; message("1 line yanked");
        } else if (sequence == "gg") {
            cursor.movePosition(QTextCursor::Start); m_buffer->setTextCursor(cursor);
        } else message("Supported combinations: dd, yy, gg");
        return true;
    }
    if (key == "d" || key == "y" || key == "g") { m_pending = key; message(key); return true; }
    if (key == "i") { enterInsert(); return true; }
    if (key == "a") {
        if (!cursor.atBlockEnd()) cursor.movePosition(QTextCursor::NextCharacter);
        m_buffer->setTextCursor(cursor); enterInsert(); return true;
    }
    if (key == "I" || key == "A") {
        cursor.movePosition(key == "I" ? QTextCursor::StartOfBlock : QTextCursor::EndOfBlock);
        if (key == "I")
            while (!cursor.atBlockEnd() && m_buffer->document()->characterAt(cursor.position()).isSpace())
                cursor.movePosition(QTextCursor::NextCharacter);
        m_buffer->setTextCursor(cursor); enterInsert(); return true;
    }
    if (key == "o" || key == "O") {
        cursor.beginEditBlock();
        cursor.movePosition(key == "o" ? QTextCursor::EndOfBlock : QTextCursor::StartOfBlock);
        cursor.insertBlock();
        if (key == "O") cursor.movePosition(QTextCursor::PreviousBlock);
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.endEditBlock();
        m_buffer->setTextCursor(cursor); enterInsert(); return true;
    }
    if (key == "u") { m_buffer->undo(); clampNormalCursor(); return true; }
    if (key == "p") { pasteRegister(); return true; }
    if (key == "n" || key == "N") { findNext(key == "N"); return true; }
    if (key == "x" || event->key() == Qt::Key_Delete) {
        if (!cursor.atBlockEnd()) {
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            m_register = cursor.selectedText(); m_registerIsLine = false;
            cursor.removeSelectedText(); m_buffer->setTextCursor(cursor); clampNormalCursor();
        }
        return true;
    }
    if (key == "h" || event->key() == Qt::Key_Left) {
        if (!cursor.atBlockStart()) cursor.movePosition(QTextCursor::PreviousCharacter);
    } else if (key == "l" || event->key() == Qt::Key_Right) {
        if (!cursor.atBlockEnd()) cursor.movePosition(QTextCursor::NextCharacter);
    } else if (key == "j" || event->key() == Qt::Key_Down) cursor.movePosition(QTextCursor::Down);
    else if (key == "k" || event->key() == Qt::Key_Up) cursor.movePosition(QTextCursor::Up);
    else if (key == "0" || event->key() == Qt::Key_Home) cursor.movePosition(QTextCursor::StartOfBlock);
    else if (key == "$" || event->key() == Qt::Key_End) cursor.movePosition(QTextCursor::EndOfBlock);
    else if (key == "w") cursor.movePosition(QTextCursor::NextWord);
    else if (key == "b") cursor.movePosition(QTextCursor::PreviousWord);
    else if (key == "G") cursor.movePosition(QTextCursor::End);
    else if (event->key() == Qt::Key_PageUp) cursor.movePosition(QTextCursor::Up, QTextCursor::MoveAnchor, 15);
    else if (event->key() == Qt::Key_PageDown) cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, 15);
    m_buffer->setTextCursor(cursor);
    clampNormalCursor();
    m_buffer->ensureCursorVisible();
    return true;
}
