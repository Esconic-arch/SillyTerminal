#include "textbrowser.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QIntValidator>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextBlock>
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
        "QPushButton { background: transparent; color: #b7bcc7; border: none; padding: 2px 4px; }"
        "QPushButton:hover, QPushButton:focus { background: #3a414c; color: #fff; }"
        "QPushButton:disabled { color: #555b65; }");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *toolbar = new QWidget(this);
    toolbar->setStyleSheet("background: #292d33;");
    auto *row = new QHBoxLayout(toolbar);
    row->setContentsMargins(8, 3, 8, 3);
    row->setSpacing(2);
    m_title = new QLabel(toolbar);
    m_title->setObjectName("browserTitle");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_back = new QPushButton("[<]", toolbar);
    m_back->setToolTip("Back (Left / Backspace / Alt+Left)");
    m_forward = new QPushButton("[>]", toolbar);
    m_forward->setToolTip("Forward (Alt+Right)");
    auto *home = new QPushButton("[Home]", toolbar);
    home->setToolTip("Bookmarks (H)");
    auto *reload = new QPushButton("[R]", toolbar);
    reload->setObjectName("browserReload");
    reload->setToolTip("Reload (R / F5)");
    auto *quit = new QPushButton("[Q]", toolbar);
    quit->setToolTip("Return to the terminal (Q / Esc)");
    m_bookmark = new QPushButton(toolbar);
    m_bookmark->setObjectName("browserBookmark");
    row->addWidget(m_title, 1);
    for (auto *button : {m_back, m_forward, reload, home, m_bookmark, quit}) {
        button->setCursor(Qt::PointingHandCursor);
        button->installEventFilter(this);
        row->addWidget(button);
    }
    layout->addWidget(toolbar);
    auto *addressRow = new QWidget(this);
    addressRow->setStyleSheet("background: #1c1c1f;");
    auto *addressLayout = new QHBoxLayout(addressRow);
    addressLayout->setContentsMargins(10, 2, 10, 3);
    addressLayout->setSpacing(4);
    auto *addressLabel = new QLabel("URL:", addressRow);
    addressLabel->setStyleSheet("color: #929ca9;");
    m_address = new QLineEdit(addressRow);
    m_address->setObjectName("browserAddress");
    m_address->setPlaceholderText("Enter website address");
    m_address->setStyleSheet("QLineEdit { background: #1c1c1f; color: #c6cbd4; border: none; padding: 2px 0; selection-background-color: #445c76; }");
    addressLayout->addWidget(addressLabel);
    addressLayout->addWidget(m_address, 1);
    layout->addWidget(addressRow);
    m_view = new LocalPageView(this);
    m_view->setObjectName("browserPage");
    m_view->setOpenLinks(false);
    m_view->setOpenExternalLinks(false);
    m_view->setReadOnly(true);
    m_view->setFrameStyle(QFrame::NoFrame);
    m_view->setAcceptDrops(false);
    m_view->document()->setDocumentMargin(12);
    m_view->setStyleSheet("QTextBrowser { color: #dedee5; background: #1c1c1f; selection-background-color: #45465b; selection-color: #fff; }"
        "QScrollBar:vertical { background: #1c1c1f; width: 7px; }"
        "QScrollBar::handle:vertical { background: #45454d; min-height: 28px; border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }");
    m_view->installEventFilter(this);
    m_address->installEventFilter(this);
    layout->addWidget(m_view, 1);
    m_linkPrompt = new QWidget(this);
    auto *promptLayout = new QHBoxLayout(m_linkPrompt);
    promptLayout->setContentsMargins(10, 2, 10, 2);
    promptLayout->addWidget(new QLabel("Follow link:", m_linkPrompt));
    m_linkNumber = new QLineEdit(m_linkPrompt);
    m_linkNumber->setObjectName("browserLinkNumber");
    m_linkNumber->setValidator(new QIntValidator(1, 10000, m_linkNumber));
    m_linkNumber->installEventFilter(this);
    m_linkNumber->setStyleSheet("background:#1c1c1f; color:#d1d5db; border:none;");
    promptLayout->addWidget(m_linkNumber, 1);
    layout->addWidget(m_linkPrompt);
    m_linkPrompt->hide();
    QObject::connect(m_linkNumber, &QLineEdit::returnPressed, this, [this] {
        const int index = m_linkNumber->text().toInt() - 1;
        m_linkPrompt->hide();
        focusPage();
        followLink(index);
    });
    auto *statusRow = new QWidget(this);
    statusRow->setStyleSheet("background: #292d33;");
    auto *statusLayout = new QHBoxLayout(statusRow);
    statusLayout->setContentsMargins(10, 3, 10, 3);
    m_status = new QLabel(statusRow);
    m_status->setObjectName("browserStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setMinimumWidth(0);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_position = new QLabel(statusRow);
    m_position->setObjectName("browserPosition");
    m_position->setStyleSheet("color: #b7bcc7;");
    statusLayout->addWidget(m_status, 1);
    statusLayout->addWidget(m_position);
    layout->addWidget(statusRow);
    QObject::connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { updatePosition(); });
    QObject::connect(m_back, &QPushButton::clicked, this, [this] { goBack(); });
    QObject::connect(m_forward, &QPushButton::clicked, this, [this] { goForward(); });
    QObject::connect(home, &QPushButton::clicked, this, [this] { open(BrowserCore::homeUrl()); });
    QObject::connect(reload, &QPushButton::clicked, this, [this] { render(true); });
    QObject::connect(m_bookmark, &QPushButton::clicked, this, [this] { toggleBookmark(); });
    QObject::connect(quit, &QPushButton::clicked, this, [this] { m_close(); });
    QObject::connect(m_address, &QLineEdit::returnPressed, this, [this] { open(m_address->text()); });
    QObject::connect(m_view, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == "browser-link") followLink(url.path().toInt());
    });
    QObject::connect(m_view, &QTextBrowser::highlighted, this, [this](const QUrl &url) {
        if (url.isEmpty()) { updateLinkStatus(); return; }
        const auto links = m_core.currentPage().links;
        const int index = url.path().toInt();
        if (url.scheme() == "browser-link" && index >= 0 && index < links.size() && m_core.storageError().isEmpty())
            status(QUrl(m_core.currentUrl()).resolved(QUrl(links.at(index).url)).toString());
    });
    setAppearance(font, spacing);
    if (!initialAddress.isEmpty()) open(initialAddress);
    else open(BrowserCore::homeUrl());
}

void TextBrowser::setAppearance(const QFont &font, int spacing) {
    m_spacing = spacing;
    QFont pageFont = font;
    QStringList families = font.families();
    families << "Microsoft YaHei UI" << "PingFang SC" << "Noto Sans Mono CJK SC";
    pageFont.setFamilies(families);
    m_view->setFont(pageFont);
    m_view->document()->setDefaultFont(pageFont);
    QFont small = font;
    small.setFamilies(pageFont.families());
    small.setPointSize(qBound(9, font.pointSize() - 2, 12));
    m_address->setFont(small);
    m_status->setFont(small);
    m_title->setFont(small);
    m_position->setFont(small);
    m_linkNumber->setFont(font);
    for (auto *button : findChildren<QPushButton *>()) button->setFont(small);
    m_view->document()->setDefaultStyleSheet(QString(
        "body { color:#d1d5db; } p { margin-top:0; margin-bottom:10px; }"
        "h1, h2, h3 { font-size:%1pt; font-weight:bold; margin-top:12px; margin-bottom:12px; }"
        "a { color:#9bc4e2; text-decoration:underline; } .meta { color:#929ca9; }"
        "hr { color:#606873; } td, th { padding:4px; } blockquote { margin-left:20px; color:#929ca9; }").arg(font.pointSize()));
    render(true);
}

void TextBrowser::focusPage() { m_view->setFocus(); }
void TextBrowser::status(const QString &text, bool error) {
    m_status->setText(text);
    m_status->setToolTip(text);
    m_status->setStyleSheet(QString("background: #292d33; color: %1;").arg(error ? "#f0a6a9" : "#92939f"));
}

void TextBrowser::render(bool preserveScroll) {
    const auto page = m_core.currentPage();
    int scroll = preserveScroll ? m_view->verticalScrollBar()->value() : 0;
    if (!preserveScroll && m_readingPositions.contains(page.url)) {
        const auto position = m_readingPositions.value(page.url);
        m_selected = position.first;
        scroll = position.second;
    }
    m_displayedUrl = page.url;
    if (m_selected >= page.links.size()) m_selected = 0;
    QString html = page.html;
    if (html.isEmpty()) {
        html = "<html><body><h1>" + escaped(page.title) + "</h1>";
        if (!page.subtitle.isEmpty()) html += "<p class='meta'>" + escaped(page.subtitle) + "</p>";
        for (const auto &paragraph : page.paragraphs) html += "<p>" + escaped(paragraph) + "</p>";
        for (int i = 0; i < page.links.size(); ++i) {
            const auto &link = page.links.at(i);
            html += QString("<p><a href='browser-link:%1'>[%2] %3</a>").arg(i).arg(i + 1).arg(escaped(link.label));
            if (page.url == BrowserCore::homeUrl()) html += "<br><span class='meta'>" + escaped(link.url) + "</span>";
            html += "</p>";
        }
        html += "</body></html>";
    }
    m_view->setHtml(html);
    QTextCursor formatting(m_view->document());
    formatting.select(QTextCursor::Document);
    QTextCharFormat type;
    type.setFontFamilies(m_view->font().families());
    type.setFontPointSize(m_view->font().pointSizeF());
    type.setProperty(QTextFormat::FontSizeAdjustment, 0);
    formatting.mergeCharFormat(type);
    QTextBlockFormat lineFormat;
    lineFormat.setLineHeight(m_spacing, QTextBlockFormat::ProportionalHeight);
    formatting.mergeBlockFormat(lineFormat);
    // Headings remain on the same character grid as body text. Qt's default
    // HTML heading sizes and margins otherwise imitate a graphical browser.
    for (auto block = m_view->document()->begin(); block.isValid(); block = block.next()) {
        QTextCursor cursor(block);
        auto format = block.blockFormat();
        format.setTopMargin(format.headingLevel() ? 8 : 0);
        format.setBottomMargin(8);
        cursor.setBlockFormat(format);
    }
    m_view->verticalScrollBar()->setValue(scroll);
    m_address->setText(page.url);
    m_title->setText(page.title);
    m_title->setToolTip(page.title);
    m_back->setEnabled(m_core.canBack());
    m_forward->setEnabled(m_core.canForward());
    m_bookmark->setText(m_core.isBookmarked() ? "[*]" : "[+]");
    m_bookmark->setToolTip(m_core.isBookmarked() ? "Remove bookmark (Ctrl+D / Cmd+D)" : "Bookmark this page (Ctrl+D / Cmd+D)");
    m_bookmark->setEnabled(QUrl(page.url).scheme() != "about" && page.found);
    highlightLink();
    updateLinkStatus();
    updatePosition();
}

void TextBrowser::highlightLink(bool scrollToLink) {
    const QString href = "browser-link:" + QString::number(m_selected);
    QList<QTextEdit::ExtraSelection> selections;
    // Link markers are inserted during document parsing. Using the href avoids
    // matching a quoted title or a duplicate label elsewhere in the page.
    const QString markerText = QString("[%1] ").arg(m_selected + 1);
    auto marker = m_view->document()->find(markerText);
    while (!marker.isNull() && marker.charFormat().anchorHref() != href)
        marker = m_view->document()->find(markerText, marker);
    if (!marker.isNull()) {
        QTextCursor selection = marker;
        selection.setPosition(marker.selectionStart());
        QTextCursor probe = selection;
        while (probe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor)) {
            if (probe.charFormat().anchorHref() != href) break;
            selection.setPosition(probe.position(), QTextCursor::KeepAnchor);
            probe.clearSelection();
        }
        QTextEdit::ExtraSelection item;
        item.cursor = selection;
        item.format.setBackground(QColor("#c4d4df"));
        item.format.setForeground(QColor("#18212a"));
        selections.append(item);
        if (scrollToLink) { m_view->setTextCursor(selection); m_view->ensureCursorVisible(); }
    }
    m_view->setExtraSelections(selections);
}

void TextBrowser::updatePosition() {
    const auto *bar = m_view->verticalScrollBar();
    const int percent = bar->maximum() > 0 ? 100 * bar->value() / bar->maximum() : 100;
    const int links = m_core.currentPage().links.size();
    m_position->setText(QString("%1/%2  %3%").arg(links ? m_selected + 1 : 0).arg(links).arg(percent));
}

void TextBrowser::updateLinkStatus() {
    if (!m_core.storageError().isEmpty()) { status(m_core.storageError(), true); return; }
    const auto page = m_core.currentPage();
    const QString target = page.links.isEmpty() ? page.url
        : QUrl(page.url).resolved(QUrl(page.links.at(m_selected).url)).toString();
    status(target);
}

void TextBrowser::open(const QString &address) {
    rememberPosition();
    if (!m_core.navigate(address)) { status(m_core.error(), true); return; }
    m_linkPrompt->hide();
    m_selected = 0;
    render();
    focusPage();
}

void TextBrowser::selectLink(int delta) {
    const auto page = m_core.currentPage();
    if (page.links.isEmpty()) return;
    m_selected = (m_selected + delta + page.links.size()) % page.links.size();
    highlightLink(true);
    updateLinkStatus();
    updatePosition();
}

void TextBrowser::followLink(int index) {
    rememberPosition();
    if (!m_core.follow(index)) { status(m_core.error(), true); return; }
    m_selected = 0;
    render();
    focusPage();
}

void TextBrowser::goBack() {
    rememberPosition();
    if (!m_core.back()) return;
    m_linkPrompt->hide();
    m_selected = 0;
    render();
    focusPage();
}

void TextBrowser::goForward() {
    rememberPosition();
    if (!m_core.forward()) return;
    m_linkPrompt->hide();
    m_selected = 0;
    render();
    focusPage();
}

void TextBrowser::rememberPosition() {
    if (!m_displayedUrl.isEmpty()) m_readingPositions[m_displayedUrl] = {m_selected, m_view->verticalScrollBar()->value()};
}

void TextBrowser::focusAddress() { m_linkPrompt->hide(); m_address->setFocus(); m_address->selectAll(); }

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
    if (key->key() == Qt::Key_F1 || (watched == m_view && key->text() == "?")) {
        open(BrowserCore::helpUrl());
        return true;
    }
    if (modifiers.testFlag(Qt::AltModifier) && key->key() == Qt::Key_Left) { goBack(); return true; }
    if (modifiers.testFlag(Qt::AltModifier) && key->key() == Qt::Key_Right) { goForward(); return true; }
    if (watched == m_linkNumber) {
        if (key->key() == Qt::Key_Escape) { m_linkPrompt->hide(); focusPage(); return true; }
        return false;
    }
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
    if (key->key() == Qt::Key_Space || key->key() == Qt::Key_PageDown || key->key() == Qt::Key_PageUp) {
        auto *bar = m_view->verticalScrollBar();
        const int direction = key->key() == Qt::Key_PageUp ? -1 : 1;
        bar->setValue(bar->value() + direction * bar->pageStep());
        return true;
    }
    if (key->key() == Qt::Key_Home || key->key() == Qt::Key_End) {
        auto *bar = m_view->verticalScrollBar();
        bar->setValue(key->key() == Qt::Key_Home ? 0 : bar->maximum());
        return true;
    }
    if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Backspace) { goBack(); return true; }
    if (key->key() == Qt::Key_Right || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { followLink(m_selected); return true; }
    if (key->key() == Qt::Key_Tab) { selectLink(modifiers.testFlag(Qt::ShiftModifier) ? -1 : 1); return true; }
    if (key->key() == Qt::Key_Backtab || key->key() == Qt::Key_Up) { selectLink(-1); return true; }
    if (key->key() == Qt::Key_Down) { selectLink(1); return true; }
    if (text.size() == 1 && text.at(0) >= '0' && text.at(0) <= '9') {
        m_linkPrompt->show();
        m_linkNumber->setText(text == "0" ? QString() : text);
        m_linkNumber->setFocus();
        return true;
    }
    return false;
}
