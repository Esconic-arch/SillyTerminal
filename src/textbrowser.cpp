#include "textbrowser.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextBlockFormat>
#include <QUrl>
#include <QVBoxLayout>
#include <utility>

namespace {
class LocalPageView : public QTextBrowser {
public:
    using QTextBrowser::QTextBrowser;
    QVariant loadResource(int, const QUrl &) override { return {}; }
};
QString escaped(const QString &text) { return text.toHtmlEscaped().replace('\n', "<br>"); }
}

TextBrowser::TextBrowser(const QString &stateFile, const QString &initialAddress, const QFont &font,
    int spacing, std::function<void()> close, QWidget *parent)
    : QWidget(parent), m_core(stateFile), m_spacing(spacing), m_close(std::move(close)) {
    setObjectName("textBrowser");
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet("#textBrowser { background: #1c1c1f; }"
        "QPushButton { background: transparent; color: #b2b2bd; border: none; border-radius: 4px; padding: 5px 9px; }"
        "QPushButton:hover { background: #393940; color: #dedee5; }"
        "QPushButton:disabled { color: #565661; }");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *toolbar = new QWidget(this);
    toolbar->setStyleSheet("background: #25262a;");
    auto *row = new QHBoxLayout(toolbar);
    row->setContentsMargins(12, 8, 16, 8);
    row->setSpacing(6);
    m_back = new QPushButton(QString::fromUtf8("←"), toolbar);
    m_back->setToolTip("Back (Left / Backspace / Alt+Left)");
    m_forward = new QPushButton(QString::fromUtf8("→"), toolbar);
    m_forward->setToolTip("Forward (Alt+Right)");
    auto *home = new QPushButton("Home", toolbar);
    home->setToolTip("Favorites (H)");
    auto *reload = new QPushButton(QString::fromUtf8("↻"), toolbar);
    reload->setObjectName("browserReload");
    reload->setToolTip("Reload (R / F5)");
    m_address = new QLineEdit(toolbar);
    m_address->setObjectName("browserAddress");
    m_address->setPlaceholderText("Enter website address");
    m_address->setClearButtonEnabled(true);
    m_address->setStyleSheet("QLineEdit { background: #1c1c1f; color: #d2d2df; border: 1px solid #3c3c46; border-radius: 4px; padding: 6px 10px; selection-background-color: #45465b; }");
    auto *quit = new QPushButton("Quit", toolbar);
    quit->setToolTip("Return to the terminal (Q / Esc)");
    m_bookmark = new QPushButton(toolbar);
    m_bookmark->setObjectName("browserBookmark");
    for (auto *button : {m_back, m_forward, home, reload, m_bookmark, quit}) {
        button->setCursor(Qt::PointingHandCursor);
        button->installEventFilter(this);
    }
    row->addWidget(m_back);
    row->addWidget(m_forward);
    row->addWidget(reload);
    row->addWidget(home);
    row->addWidget(m_address, 1);
    row->addWidget(m_bookmark);
    row->addWidget(quit);
    layout->addWidget(toolbar);
    m_view = new LocalPageView(this);
    m_view->setObjectName("browserPage");
    m_view->setOpenLinks(false);
    m_view->setOpenExternalLinks(false);
    m_view->setReadOnly(true);
    m_view->setFrameStyle(QFrame::NoFrame);
    m_view->setAcceptDrops(false);
    m_view->document()->setDocumentMargin(28);
    m_view->setStyleSheet("QTextBrowser { color: #dedee5; background: #1c1c1f; selection-background-color: #45465b; selection-color: #fff; }"
        "QScrollBar:vertical { background: #1c1c1f; width: 7px; }"
        "QScrollBar::handle:vertical { background: #45454d; min-height: 28px; border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }");
    m_view->installEventFilter(this);
    m_address->installEventFilter(this);
    layout->addWidget(m_view, 1);
    m_status = new QLabel(this);
    m_status->setObjectName("browserStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setMinimumWidth(0);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_status->setStyleSheet("background: #25262a; color: #92939f; padding: 9px 16px;");
    layout->addWidget(m_status);
    QObject::connect(m_back, &QPushButton::clicked, this, [this] { goBack(); });
    QObject::connect(m_forward, &QPushButton::clicked, this, [this] { goForward(); });
    QObject::connect(home, &QPushButton::clicked, this, [this] { open(BrowserCore::homeUrl()); });
    QObject::connect(reload, &QPushButton::clicked, this, [this] { render(true); });
    QObject::connect(m_bookmark, &QPushButton::clicked, this, [this] { toggleBookmark(); });
    QObject::connect(quit, &QPushButton::clicked, this, [this] { m_close(); });
    QObject::connect(m_address, &QLineEdit::returnPressed, this, [this] { open(m_address->text()); });
    QObject::connect(m_view, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) { open(url.toString()); });
    QObject::connect(m_view, &QTextBrowser::highlighted, this, [this](const QUrl &url) {
        if (m_core.storageError().isEmpty()) status(url.isEmpty() ? m_core.currentUrl() : url.toString());
    });
    setAppearance(font, spacing);
    if (!initialAddress.isEmpty()) open(initialAddress);
    else open(BrowserCore::homeUrl());
}

void TextBrowser::setAppearance(const QFont &font, int spacing) {
    m_spacing = spacing;
    m_view->setFont(font);
    m_view->document()->setDefaultFont(font);
    QFont small = font;
    small.setPointSize(qMax(9, font.pointSize() - 2));
    m_address->setFont(small);
    m_status->setFont(small);
    render(true);
}

void TextBrowser::focusPage() { m_view->setFocus(); }
void TextBrowser::status(const QString &text, bool error) {
    m_status->setText(text);
    m_status->setToolTip(text);
    m_status->setStyleSheet(QString("background: #25262a; color: %1; padding: 9px 16px;").arg(error ? "#f0a6a9" : "#92939f"));
}

void TextBrowser::render(bool preserveScroll) {
    const int scroll = preserveScroll ? m_view->verticalScrollBar()->value() : 0;
    const auto page = m_core.currentPage();
    if (m_selected >= page.links.size()) m_selected = 0;
    QString html = QString("<html><body style='color:#dedee5; line-height:%1%;'>").arg(m_spacing);
    const bool favorites = page.url == BrowserCore::homeUrl();
    if (!page.subtitle.isEmpty())
        html += "<p style='color:#92939f; margin-top:0; margin-bottom:24px;'>" + escaped(page.subtitle) + "</p>";
    html += "<p style='font-size:18pt; color:#e4e4ed; margin-bottom:24px;'>" + escaped(page.title) + "</p>";
    for (const auto &paragraph : page.paragraphs)
        html += "<p style='margin-top:0; margin-bottom:18px;'>" + escaped(paragraph) + "</p>";
    if (!favorites && !page.links.isEmpty())
        html += "<p style='color:#64646f; margin-top:26px; margin-bottom:12px;'>────────────────────────────────────</p>";
    for (int i = 0; i < page.links.size(); ++i) {
        const QString url = QUrl(page.url).resolved(QUrl(page.links.at(i).url)).toString();
        const QString label = QString("[%1] %2").arg(i + 1).arg(page.links.at(i).label);
        html += "<p style='margin-top:0; margin-bottom:12px;'><a style='color:#c0c5ed; text-decoration:none;";
        if (i == m_selected) html += " background-color:#414356;";
        html += "' href='" + url.toHtmlEscaped() + "'>" + escaped(label) + "</a>";
        if (favorites) html += "<br><span style='font-size:10pt; color:#92939f;'>" + escaped(url) + "</span>";
        html += "</p>";
    }
    html += "</body></html>";
    m_view->setHtml(html);
    QTextCursor formatting(m_view->document());
    formatting.select(QTextCursor::Document);
    QTextBlockFormat lineFormat;
    lineFormat.setLineHeight(m_spacing, QTextBlockFormat::ProportionalHeight);
    formatting.mergeBlockFormat(lineFormat);
    m_view->verticalScrollBar()->setValue(scroll);
    m_address->setText(page.url);
    m_back->setEnabled(m_core.canBack());
    m_forward->setEnabled(m_core.canForward());
    m_bookmark->setText(QString::fromUtf8(m_core.isBookmarked() ? "★" : "☆"));
    m_bookmark->setToolTip(m_core.isBookmarked() ? "Remove bookmark (Ctrl+D / Cmd+D)" : "Bookmark this page (Ctrl+D / Cmd+D)");
    m_bookmark->setEnabled(!favorites && page.found);
    status(m_core.storageError().isEmpty()
        ? page.url : m_core.storageError(),
        !m_core.storageError().isEmpty());
}

void TextBrowser::open(const QString &address) {
    if (!m_core.navigate(address)) { status(m_core.error(), true); return; }
    m_selected = 0;
    render();
    focusPage();
}

void TextBrowser::selectLink(int delta) {
    const auto page = m_core.currentPage();
    if (page.links.isEmpty()) return;
    m_selected = (m_selected + delta + page.links.size()) % page.links.size();
    render(true);
    const QString label = QString("[%1] %2").arg(m_selected + 1).arg(page.links.at(m_selected).label);
    const auto found = m_view->document()->find(label);
    if (!found.isNull()) { m_view->setTextCursor(found); m_view->ensureCursorVisible(); }
}

void TextBrowser::followLink(int index) {
    if (!m_core.follow(index)) { status(m_core.error(), true); return; }
    m_selected = 0;
    render();
    focusPage();
}

void TextBrowser::goBack() {
    if (!m_core.back()) return;
    m_selected = 0;
    render();
    focusPage();
}

void TextBrowser::goForward() {
    if (!m_core.forward()) return;
    m_selected = 0;
    render();
    focusPage();
}

void TextBrowser::focusAddress() { m_address->setFocus(); m_address->selectAll(); }

void TextBrowser::toggleBookmark() {
    if (!m_core.toggleBookmark()) { status(m_core.error(), true); return; }
    const bool added = m_core.isBookmarked();
    render(true);
    if (m_core.storageError().isEmpty()) status(added ? "Bookmark added" : "Bookmark removed");
}

void TextBrowser::keyPressEvent(QKeyEvent *event) { event->accept(); }

bool TextBrowser::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(watched, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    const auto modifiers = key->modifiers();
    const bool control = modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier);
    if (control && key->key() == Qt::Key_L) { focusAddress(); return true; }
    if (control && key->key() == Qt::Key_D) { toggleBookmark(); return true; }
    if (key->key() == Qt::Key_F5) { render(true); return true; }
    if (key->key() == Qt::Key_F1) {
        status("↑↓ / Tab links · Enter open · Alt+←/→ history · Ctrl+L address · Ctrl+D bookmark · H favorites · Q quit");
        return true;
    }
    if (modifiers.testFlag(Qt::AltModifier) && key->key() == Qt::Key_Left) { goBack(); return true; }
    if (modifiers.testFlag(Qt::AltModifier) && key->key() == Qt::Key_Right) { goForward(); return true; }
    if (watched == m_address) {
        if (key->key() == Qt::Key_Escape) { m_address->setText(m_core.currentUrl()); focusPage(); return true; }
        return false;
    }
    if (watched != m_view || control || modifiers.testFlag(Qt::AltModifier)) return false;
    const QString text = key->text().toLower();
    if (text == "q" || key->key() == Qt::Key_Escape) { m_close(); return true; }
    if (text == "g") { focusAddress(); return true; }
    if (text == "h") { open(BrowserCore::homeUrl()); return true; }
    if (text == "r") { render(true); return true; }
    if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Backspace) { goBack(); return true; }
    if (key->key() == Qt::Key_Right || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { followLink(m_selected); return true; }
    if (key->key() == Qt::Key_Tab) { selectLink(modifiers.testFlag(Qt::ShiftModifier) ? -1 : 1); return true; }
    if (key->key() == Qt::Key_Backtab || key->key() == Qt::Key_Up) { selectLink(-1); return true; }
    if (key->key() == Qt::Key_Down) { selectLink(1); return true; }
    if (text.size() == 1 && text.at(0) >= '1' && text.at(0) <= '9') { followLink(text.toInt() - 1); return true; }
    return false;
}
