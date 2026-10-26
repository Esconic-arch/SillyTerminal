#include "browsercore.h"
#include "terminalcore.h"
#include "textbrowser.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QScrollBar>
#include <QTextTable>
#include <QTextBlock>
#include <QUrl>
#include <cstdio>

namespace {
void key(QWidget *target, int code, const QString &text = {}, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::KeyPress, code, modifiers, text);
    QCoreApplication::sendEvent(target, &event);
}
int linkTo(const BrowserCore &core, const QString &suffix) {
    const auto links = core.currentPage().links;
    for (int i = 0; i < links.size(); ++i) if (links.at(i).url.endsWith(suffix)) return i;
    return -1;
}
void go(QLineEdit *address, const QString &url) {
    address->setText(url);
    key(address, Qt::Key_Return);
}
void number(TextBrowser &browser, QTextBrowser *view, int value) {
    key(view, Qt::Key_0, "0");
    auto *input = browser.findChild<QLineEdit *>("browserLinkNumber");
    input->setText(QString::number(value));
    key(input, Qt::Key_Return);
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    BrowserCore core;
    check(core.currentUrl() == "about:bookmarks" && core.currentPage().found, "starts on favorites");
    check(core.currentPage().links.size() == 3, "favorites links three sites");
    check(!core.canBack() && !core.canForward(), "initial navigation boundaries");
    check(core.navigate("daybreak.news"), "short host address");
    check(core.currentUrl() == "https://www.daybreak.news/", "host normalized to root");
    check(core.follow(linkTo(core, "/city/late-train")) && core.currentUrl() == "https://www.daybreak.news/city/late-train", "relative hyperlink navigation");
    check(core.back() && core.currentUrl() == "https://www.daybreak.news/", "back returns to previous page");
    check(core.forward() && core.currentUrl() == "https://www.daybreak.news/city/late-train", "forward navigation");
    core.back();
    check(core.navigate("/culture/bookshop") && !core.canForward(), "new navigation prunes forward branch");
    check(core.navigate("HTTPS://WWW.DAYBREAK.NEWS/culture/bookshop/"), "case-insensitive scheme and host");
    check(core.currentUrl() == "https://www.daybreak.news/culture/bookshop", "trailing slash normalized");
    check(core.navigate("daybreak.news/culture/../culture/bookshop"), "path normalization");
    const QString previous = core.currentUrl();
    for (const auto &address : {"file:///etc/passwd", "javascript:alert(1)",
             "data:text/html,test", "https://user@daybreak.news/", "https://daybreak.news:8080/",
             "https://day break.news/", "about:config", ""}) {
        check(!core.navigate(address) && core.currentUrl() == previous, "invalid address preserves current page");
    }
    check(core.navigate("http://DAYBREAK.NEWS:80/?source=bookmark#top") && core.currentPage().found,
        "standard URL with query and fragment resolves page");
    check(core.currentUrl() == "https://www.daybreak.news/?source=bookmark#top", "canonical domain retains query and fragment");
    check(core.navigate("www.daybreak.news/missing") && !core.currentPage().found
        && core.currentPage().title.contains("404"), "missing path shows site 404");
    check(core.follow(0) && core.currentUrl() == "https://www.daybreak.news/", "404 links to site home");
    check(core.navigate("unknown.example/missing") && !core.currentPage().found
        && core.currentPage().title.contains("can't be reached"), "unknown domain shows connection error");
    check(!core.toggleBookmark(), "cannot bookmark error page");
    check(core.follow(0) && core.currentUrl() == BrowserCore::homeUrl(), "connection error links to favorites");
    check(!core.toggleBookmark(), "internal page cannot be bookmarked");
    check(!core.follow(-1) && !core.follow(99), "invalid link indices rejected");

    QSet<QString> visited;
    QStringList pending = {BrowserCore::homeUrl()};
    while (!pending.isEmpty() && visited.size() < 50) {
        const QString url = pending.takeFirst();
        if (visited.contains(url)) continue;
        visited.insert(url);
        core.navigate(url);
        const auto page = core.currentPage();
        check(page.found && !page.title.isEmpty(), "every linked page exists");
        for (int i = 0; i < page.links.size(); ++i) {
            core.navigate(url);
            check(core.follow(i) && core.currentPage().found, "every catalog link resolves locally");
            if (!visited.contains(core.currentUrl())) pending.append(core.currentUrl());
        }
    }
    check(visited.size() == 30, "all authored pages and favorites are reachable");
    BrowserCore transit;
    check(transit.navigate("hcmetro.com") && transit.currentPage().found
        && transit.currentUrl() == "https://www.hcmetro.com/", "transit domain opens without www or scheme");
    check(!transit.isBookmarked(), "transit website is not a default bookmark");
    transit.navigate("daybreak.news/city/late-train");
    check(transit.follow(linkTo(transit, "/notices/2026-10-22-late-service"))
        && transit.currentUrl() == "https://www.hcmetro.com/notices/2026-10-22-late-service"
        && transit.currentPage().found, "news article links to the operator's original notice");
    BrowserCore bounded;
    for (int i = 0; i < 130; ++i) bounded.navigate(i % 2 ? BrowserCore::homeUrl() : "daybreak.news");
    int backCount = 0;
    while (bounded.back()) ++backCount;
    check(backCount == 99, "history bounded to one hundred entries");

    QTemporaryDir temporary(QDir::current().filePath("browser-world-XXXXXX"));
    if (!temporary.isValid()) return 2;
    const QString statePath = temporary.path() + "/browser.json";
    {
        BrowserCore saved(statePath);
        saved.navigate("daybreak.news");
        saved.follow(linkTo(saved, "/culture/bookshop"));
        saved.back();
        check(saved.storageError().isEmpty() && QFile::exists(statePath), "history saved on disk");
    }
    BrowserCore restored(statePath);
    check(restored.currentUrl() == "https://www.daybreak.news/" && restored.canForward(), "restart restores position and forward history");
    restored.forward();
    check(restored.currentUrl() == "https://www.daybreak.news/culture/bookshop", "restored forward entry works");
    check(!restored.isBookmarked() && restored.toggleBookmark() && restored.isBookmarked(), "article bookmark added");
    BrowserCore restoredBookmarks(statePath);
    check(restoredBookmarks.isBookmarked(), "bookmark survives restart");
    restoredBookmarks.navigate(BrowserCore::homeUrl());
    check(restoredBookmarks.currentPage().links.size() == 4
        && restoredBookmarks.follow(3) && restoredBookmarks.currentPage().found, "saved article appears as working favorite");
    for (const auto &url : {"www.daybreak.news/culture/bookshop", "www.daybreak.news", "bbs.afterhours.net", "linnotes.me"}) {
        restoredBookmarks.navigate(url);
        check(restoredBookmarks.isBookmarked() && restoredBookmarks.toggleBookmark(), "remove bookmark");
    }
    BrowserCore emptyFavorites(statePath);
    emptyFavorites.navigate(BrowserCore::homeUrl());
    check(emptyFavorites.currentPage().links.isEmpty(), "intentionally empty favorites survives restart");
    QFile legacy(temporary.path() + "/legacy.json");
    check(legacy.open(QIODevice::WriteOnly), "open legacy state fixture");
    legacy.write(QJsonDocument(QJsonObject{{"version", 1}, {"index", 2},
        {"history", QJsonArray{"intranet://home/", "intranet://archive/", "intranet://archive/personnel/",
            "intranet://commons/visitor-notes", "intranet://manual/editing"}}}).toJson());
    legacy.close();
    BrowserCore migrated(legacy.fileName());
    check(migrated.currentUrl() == "https://www.daybreak.news/about" && migrated.currentPage().found,
        "legacy current URL migrated after slash normalization");
    check(migrated.forward() && migrated.currentUrl() == "https://bbs.afterhours.net/t/81", "legacy forum entry migrated");
    check(migrated.forward() && migrated.currentUrl() == "https://linnotes.me/about", "legacy manual entry migrated");
    migrated.navigate(BrowserCore::homeUrl());
    check(migrated.currentPage().links.size() == 3, "legacy history receives default favorites");
    QFile invalid(statePath);
    check(invalid.open(QIODevice::WriteOnly | QIODevice::Truncate), "open isolated state fixture");
    invalid.write("not json"); invalid.close();
    BrowserCore corrupted(statePath);
    check(corrupted.currentUrl() == BrowserCore::homeUrl(), "invalid saved state falls back to home");
    BrowserCore unavailable(temporary.path());
    check(unavailable.navigate("daybreak.news") && !unavailable.storageError().isEmpty(), "history write failure does not prevent reading pages");

    bool closed = false;
    TextBrowser widget(temporary.path() + "/ui.json", {}, QFont("monospace", 12), 115, [&] { closed = true; });
    widget.resize(980, 600);
    auto *view = widget.findChild<QTextBrowser *>("browserPage");
    auto *address = widget.findChild<QLineEdit *>("browserAddress");
    check(view && address && view->isReadOnly(), "read-only browser UI created");
    check(view->toPlainText().contains("Favorites") && view->toPlainText().contains("https://linnotes.me/"), "favorites rendered with website addresses");
    check(widget.findChild<QLabel *>("browserStatus")->text() == "https://www.daybreak.news/", "status bar shows selected link target");
    auto *star = widget.findChild<QPushButton *>("browserBookmark");
    check(star && !star->isEnabled(), "internal page bookmark control disabled");
    key(view, Qt::Key_Return);
    check(address->text() == "https://www.daybreak.news/", "enter opens selected link");
    key(view, Qt::Key_Down);
    key(view, Qt::Key_Return);
    check(address->text() == "https://www.daybreak.news/about", "arrow selects inline navigation link");
    key(view, Qt::Key_Left);
    check(address->text() == "https://www.daybreak.news/", "left goes back");
    key(view, Qt::Key_Right, {}, Qt::AltModifier);
    check(address->text() == "https://www.daybreak.news/about", "alt-right goes forward");
    key(view, Qt::Key_H, "h");
    number(widget, view, 2);
    check(address->text() == "https://bbs.afterhours.net/", "number prompt follows bookmark");
    bool uniformSize = true;
    for (auto block = view->document()->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (fragment.isValid() && (fragment.charFormat().fontPointSize() > view->font().pointSizeF()
                || fragment.charFormat().intProperty(QTextFormat::FontSizeAdjustment) != 0)) uniformSize = false;
        }
    }
    check(uniformSize, "document headings use terminal text size");
    check(view->document()->rootFrame()->childFrames().size() > 0, "forum list renders a table");
    check(view->toPlainText().contains("bottleneck") && view->toPlainText().contains("23:16"), "forum list shows authors and activity");
    check(!view->extraSelections().isEmpty() && view->extraSelections().first().cursor.selectedText().contains("[1]"), "selected link highlighted in page");
    BrowserCore forum;
    forum.navigate("bbs.afterhours.net");
    const int profileIndex = linkTo(forum, "/u/side_b");
    check(profileIndex >= 9, "fixture includes link numbers greater than nine");
    number(widget, view, profileIndex + 1);
    check(address->text() == "https://bbs.afterhours.net/u/side_b", "multi-digit link number opens user profile");
    go(address, "bbs.afterhours.net/t/47");
    check(view->toPlainText().contains("#6") && view->toPlainText().contains("slowtrain"), "thread shows complete reply sequence");
    widget.show();
    QCoreApplication::processEvents();
    key(view, Qt::Key_Down);
    key(view, Qt::Key_End);
    const int bottom = view->verticalScrollBar()->value();
    check(bottom > 0 && widget.findChild<QLabel *>("browserPosition")->text().endsWith("100%"), "reading position reflects scroll percentage");
    key(view, Qt::Key_PageUp);
    check(view->verticalScrollBar()->value() < bottom, "PageUp scrolls text browser");
    key(view, Qt::Key_End);
    go(address, "linnotes.me/about");
    key(view, Qt::Key_Left);
    check(address->text() == "https://bbs.afterhours.net/t/47" && view->verticalScrollBar()->value() == bottom
        && widget.findChild<QLabel *>("browserPosition")->text().startsWith("2/"), "back restores reading position and selected link");
    go(address, "linnotes.me/about");
    key(view, Qt::Key_G, "g");
    check(address->selectedText() == address->text(), "G selects address for editing");
    address->setText("linnotes.me/about");
    key(address, Qt::Key_Return);
    check(address->text() == "https://linnotes.me/about", "address bar navigates");
    check(star->isEnabled(), "website can be bookmarked");
    star->click();
    check(widget.findChild<QLabel *>("browserStatus")->text() == "Bookmark added", "star adds bookmark");
    key(address, Qt::Key_D, "d", Qt::ControlModifier);
    check(widget.findChild<QLabel *>("browserStatus")->text() == "Bookmark removed", "Ctrl+D toggles bookmark while editing address");
    address->setText("javascript:alert(1)");
    key(address, Qt::Key_Return);
    check(address->text() == "javascript:alert(1)" && view->toPlainText().contains("lin@linnotes.me"), "invalid protocol leaves displayed page unchanged");
    key(address, Qt::Key_Escape);
    check(address->text() == "https://linnotes.me/about" && !closed, "escape restores address without quitting");
    address->setText("www.daybreak.news/missing");
    key(address, Qt::Key_Return);
    check(view->toPlainText().contains("404"), "404 rendered in UI");
    number(widget, view, 1);
    check(address->text() == "https://www.daybreak.news/", "404 returns to website home");
    key(view, Qt::Key_F1);
    check(address->text() == BrowserCore::helpUrl() && view->toPlainText().contains("Ctrl+D") && !star->isEnabled(), "help has an internal page separate from websites");
    key(view, Qt::Key_Left);
    key(view, Qt::Key_Q, "q");
    check(closed, "Q quits browser");

    BrowserCore reopenedUi(temporary.path() + "/ui.json");
    check(reopenedUi.currentUrl() == "https://www.daybreak.news/", "widget persists last navigation");
    TextBrowser freshWindow(temporary.path() + "/ui.json", {}, QFont("monospace", 12), 115, [] {});
    auto *freshAddress = freshWindow.findChild<QLineEdit *>("browserAddress");
    check(freshAddress->text() == BrowserCore::homeUrl(), "new browser opens favorites despite saved history");
    key(freshWindow.findChild<QTextBrowser *>("browserPage"), Qt::Key_Left);
    check(freshAddress->text() == "https://www.daybreak.news/", "saved page remains in back history");
    TextBrowser direct(temporary.path() + "/direct.json", "linnotes.me", QFont("monospace", 12), 115, [] {});
    check(direct.findChild<QLineEdit *>("browserAddress")->text() == "https://linnotes.me/", "explicit launch URL overrides start page");

    TerminalCore terminal(temporary.path() + "/world");
    if (!terminal.initializationError().isEmpty())
        std::fprintf(stderr, "World initialization failed: %s\n", qPrintable(terminal.initializationError()));
    const auto launched = terminal.execute("browser https://www.daybreak.news/");
    check(launched.openBrowser && launched.browserUrl == "https://www.daybreak.news/", "terminal opens browser at supplied URL");
    check(terminal.execute("browser").openBrowser, "terminal opens browser without an explicit address");
    check(terminal.execute("browser one two").error, "extra arguments rejected");
    check(terminal.complete("brow").replacement == "browser ", "browser command completion");
    std::fprintf(stderr, "%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
