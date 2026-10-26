#include "textpager.h"

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QTextEdit>
#include <QTextBlockFormat>
#include <QVBoxLayout>

TextPager::TextPager(const QString &title, const QString &content, const QFont &font, int spacing,
    std::function<void()> close, QWidget *parent)
    : QWidget(parent), m_title(title), m_close(std::move(close)) {
    setObjectName("textPager");
    setAutoFillBackground(true);
    setStyleSheet("#textPager { background: #1c1c1f; } QLabel, QLineEdit { background: #25262a; color: #dedee5; padding: 5px; }");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_view = new QTextEdit(this);
    m_view->setObjectName("pagerPage");
    m_view->setReadOnly(true);
    m_view->setAcceptRichText(false);
    m_view->setFrameStyle(QFrame::NoFrame);
    m_view->setStyleSheet("QTextEdit { background: #1c1c1f; color: #dedee5; selection-background-color: #45465b; }");
    m_view->document()->setDocumentMargin(20);
    m_view->setPlainText(content);
    m_view->installEventFilter(this);
    layout->addWidget(m_view, 1);
    m_search = new QLineEdit(this);
    m_search->setObjectName("pagerSearch");
    m_search->installEventFilter(this);
    m_search->hide();
    layout->addWidget(m_search);
    m_status = new QLabel(this);
    m_status->setObjectName("pagerStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { refreshStatus(); });
    connect(m_view->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { refreshStatus(); });
    setAppearance(font, spacing);
    refreshStatus();
}

void TextPager::setAppearance(const QFont &font, int spacing) {
    m_view->setFont(font);
    m_search->setFont(font);
    m_status->setFont(font);
    QTextCursor all(m_view->document());
    all.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(spacing, QTextBlockFormat::ProportionalHeight);
    all.mergeBlockFormat(format);
}

void TextPager::focusPage() { m_view->setFocus(); }

void TextPager::refreshStatus() {
    const auto *bar = m_view->verticalScrollBar();
    m_status->setText(m_title + (bar->value() == bar->maximum() ? "  (END)" : "") + "    / search    n/N repeat    q quit");
}

void TextPager::find(bool backward, bool first) {
    if (!m_hasPattern) { m_status->setText("No previous search pattern"); return; }
    int position;
    if (first) position = m_view->cursorForPosition(QPoint(int(m_view->document()->documentMargin()) + 1,
        qMax(1, int(m_view->document()->documentMargin()) - m_view->verticalScrollBar()->value()))).position();
    else {
        const auto cursor = m_view->textCursor();
        position = backward ? cursor.selectionStart() : cursor.selectionEnd();
        if (!cursor.hasSelection()) position += backward ? -1 : 1;
    }
    if (position < 0 || position >= m_view->document()->characterCount()) {
        m_status->setText("Pattern not found"); return;
    }
    const auto match = m_view->document()->find(m_pattern, position,
        backward ? QTextDocument::FindBackward : QTextDocument::FindFlags{});
    if (match.isNull()) { m_status->setText("Pattern not found"); return; }
    m_view->setTextCursor(match);
    m_view->ensureCursorVisible();
    refreshStatus();
}

bool TextPager::eventFilter(QObject *object, QEvent *event) {
    if (object == m_view && event->type() == QEvent::InputMethod) return true;
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    if (object == m_search) {
        if (key->key() == Qt::Key_Escape) { m_search->hide(); layout()->activate(); focusPage(); refreshStatus(); return true; }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            const QString text = m_search->text().mid(1);
            if (!text.isEmpty()) {
                QRegularExpression pattern(text);
                if (!pattern.isValid()) { m_status->setText("Invalid search pattern: " + pattern.errorString()); return true; }
                m_pattern = pattern;
                m_hasPattern = true;
            }
            m_search->hide(); layout()->activate(); focusPage(); find(m_backward, true); return true;
        }
        return QWidget::eventFilter(object, event);
    }
    if (object != m_view) return QWidget::eventFilter(object, event);
    if (key->matches(QKeySequence::Copy)) return false;
    const QString text = key->text();
    auto *bar = m_view->verticalScrollBar();
    const bool control = key->modifiers().testFlag(Qt::ControlModifier);
    if (text == "q" || key->key() == Qt::Key_Escape) { m_close(); return true; }
    if (text == "/" || text == "?") {
        m_backward = text == "?";
        m_search->setText(text);
        m_search->show(); m_search->setFocus(); return true;
    }
    if (text == "n" || text == "N") { find(text == "N" ? !m_backward : m_backward, false); return true; }
    if (text == "g" || key->key() == Qt::Key_Home) bar->setValue(bar->minimum());
    else if (text == "G" || key->key() == Qt::Key_End) bar->setValue(bar->maximum());
    else if (text == " " || text == "f" || key->key() == Qt::Key_PageDown || (control && key->key() == Qt::Key_F))
        bar->setValue(bar->value() + bar->pageStep());
    else if (text == "b" || key->key() == Qt::Key_PageUp || (control && key->key() == Qt::Key_B))
        bar->setValue(bar->value() - bar->pageStep());
    else if (text == "j" || key->key() == Qt::Key_Down || key->key() == Qt::Key_Return)
        bar->setValue(bar->value() + m_view->fontMetrics().height());
    else if (text == "k" || key->key() == Qt::Key_Up)
        bar->setValue(bar->value() - m_view->fontMetrics().height());
    return true;
}
