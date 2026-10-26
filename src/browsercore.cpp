#include "browsercore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {
// Authored XHTML supplies structure; the browser controls typography and links.
bool readDocument(const QByteArray &source, BrowserPage &page) {
    QXmlStreamReader reader(source);
    QXmlStreamWriter writer(&page.html);
    const QSet<QString> tags = {"html", "body", "h1", "h2", "h3", "p", "br", "hr", "b", "strong", "i", "em",
        "ul", "ol", "li", "table", "tr", "td", "th", "pre", "blockquote", "a", "span"};
    const QSet<QString> attributes = {"colspan", "rowspan", "border", "cellpadding", "cellspacing", "width", "align", "class"};
    int link = -1;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isDTD() || reader.isEntityReference()) return false;
        if (reader.isStartElement()) {
            const QString tag = reader.name().toString();
            if (!tags.contains(tag)) return false;
            writer.writeStartElement(tag);
            if (tag == "a") {
                if (link >= 0) return false;
                const QUrl target = QUrl(page.url).resolved(QUrl(reader.attributes().value("href").toString()));
                if (!target.isValid() || (target.scheme() != "https" && target.scheme() != "http" && target.scheme() != "about")) return false;
                link = page.links.size();
                page.links.append({{}, target.toString(QUrl::FullyEncoded)});
                writer.writeAttribute("href", "browser-link:" + QString::number(link));
                writer.writeCharacters(QString("[%1] ").arg(link + 1));
            } else {
                for (const auto &attribute : reader.attributes())
                    if (attributes.contains(attribute.name().toString())) writer.writeAttribute(attribute.name().toString(), attribute.value().toString());
            }
        } else if (reader.isEndElement()) {
            if (reader.name() == QLatin1String("a")) {
                page.links[link].label = page.links.at(link).label.simplified();
                link = -1;
            }
            writer.writeEndElement();
        } else if (reader.isCharacters()) {
            writer.writeCharacters(reader.text().toString());
            if (link >= 0) page.links[link].label += reader.text();
        }
    }
    return !reader.hasError() && !page.html.isEmpty();
}
}

QString BrowserCore::normalize(const QString &address, const QString &base, QString &error) {
    QString input = address.trimmed();
    if (input.isEmpty()) { error = "Enter a website address."; return {}; }
    if (input.compare(homeUrl(), Qt::CaseInsensitive) == 0) return homeUrl();
    if (input.compare(helpUrl(), Qt::CaseInsensitive) == 0) return helpUrl();
    for (const auto ch : input) {
        if (ch.isSpace() || ch.unicode() < 32) { error = "An address cannot contain whitespace."; return {}; }
    }
    if (input.startsWith('/')) input = QUrl(base).resolved(QUrl(input)).toString();
    else if (!input.contains(':')) input.prepend("https://");
    QUrl url(input, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != "http" && scheme != "https" && scheme != "intranet")
        || url.host().isEmpty() || !url.userInfo().isEmpty()
        || (url.port() != -1 && url.port() != 80 && url.port() != 443)) {
        error = "Enter a valid http:// or https:// address.";
        return {};
    }
    QString host = url.host().toLower();
    url = url.adjusted(QUrl::NormalizePathSegments);
    QString path = url.path();
    while (path.size() > 1 && path.endsWith('/')) path.chop(1);
    url.setPath(path.isEmpty() ? "/" : path);
    // Preserve links and history from the earlier prototype.
    if (host == "home" && scheme == "intranet") return homeUrl();
    if (host == "archive") {
        host = "www.daybreak.news";
        const QMap<QString, QString> paths = {{"/notices", "/city/late-train"}, {"/personnel", "/about"},
            {"/history", "/culture/bookshop"}, {"/restricted", "/"}};
        if (paths.contains(url.path())) url.setPath(paths.value(url.path()));
    } else if (host == "commons") {
        host = "bbs.afterhours.net";
        if (url.path() == "/first-message") url.setPath("/t/47");
        if (url.path() == "/visitor-notes") url.setPath("/t/81");
    } else if (host == "manual") {
        host = "linnotes.me";
        if (url.path() == "/navigation") url.setPath("/posts/nightwalk");
        if (url.path() == "/editing") url.setPath("/about");
    }
    if (host == "daybreak.news") host = "www.daybreak.news";
    if (host == "afterhours.net") host = "bbs.afterhours.net";
    if (host == "www.linnotes.me") host = "linnotes.me";
    if (host == "hcmetro.com") host = "www.hcmetro.com";
    if (!host.contains('.')) host += ".invalid";
    url.setScheme("https");
    url.setHost(host);
    url.setPort(-1);
    return url.toString(QUrl::FullyEncoded);
}

BrowserCore::BrowserCore(const QString &stateFile) : m_stateFile(stateFile) {
    static const bool resourcesReady = [] { Q_INIT_RESOURCE(local_sites); return true; }();
    Q_UNUSED(resourcesReady);
    QFile catalog(":/sites/pages.json");
    if (catalog.open(QIODevice::ReadOnly)) {
        const auto data = QJsonDocument::fromJson(catalog.readAll()).object();
        for (const auto &value : data.value("bookmarks").toArray()) {
            const auto link = value.toObject();
            QString error;
            const QString url = normalize(link.value("url").toString(), homeUrl(), error);
            if (error.isEmpty() && QUrl(url).scheme() != "about") m_bookmarks.append({link.value("label").toString(), url});
        }
        for (const auto &value : data.value("pages").toArray()) {
            const auto object = value.toObject();
            QString error;
            BrowserPage page;
            page.url = normalize(object.value("url").toString(), homeUrl(), error);
            if (!error.isEmpty()) continue;
            page.title = object.value("title").toString();
            page.subtitle = object.value("subtitle").toString();
            const QString document = object.value("document").toString();
            if (!document.isEmpty()) {
                QFile body(":/sites/" + document);
                if (!body.open(QIODevice::ReadOnly) || !readDocument(body.readAll(), page)) continue;
            }
            for (const auto &paragraph : object.value("paragraphs").toArray()) page.paragraphs.append(paragraph.toString());
            for (const auto &linkValue : object.value("links").toArray()) {
                const auto link = linkValue.toObject();
                page.links.append({link.value("label").toString(), link.value("url").toString()});
            }
            page.found = true;
            m_pages.insert(page.url, page);
        }
    }
    m_history = {homeUrl()};
    if (!m_stateFile.isEmpty()) {
        const QFileInfo info(m_stateFile);
        QFile state(m_stateFile);
        if (!info.isSymLink() && !info.isJunction() && state.open(QIODevice::ReadOnly) && state.size() < 1024 * 1024) {
            const auto saved = QJsonDocument::fromJson(state.readAll()).object();
            if (saved.value("bookmarks").isArray()) {
                m_bookmarks.clear();
                QSet<QString> seen;
                for (const auto &entry : saved.value("bookmarks").toArray()) {
                    const auto link = entry.toObject();
                    QString error;
                    const QString url = normalize(link.value("url").toString(), homeUrl(), error);
                    if (!error.isEmpty() || QUrl(url).scheme() == "about" || seen.contains(url) || m_bookmarks.size() >= 100) continue;
                    seen.insert(url);
                    m_bookmarks.append({link.value("label").toString(url), url});
                }
            }
            const auto history = saved.value("history").toArray();
            QStringList valid;
            for (const auto &entry : history) {
                QString error;
                const QString url = normalize(entry.toString(), homeUrl(), error);
                if (!error.isEmpty() || valid.size() == 100) { valid.clear(); break; }
                valid.append(url);
            }
            const int index = saved.value("index").toInt(-1);
            if (!valid.isEmpty() && index >= 0 && index < valid.size()) {
                m_history = valid;
                m_index = index;
            }
        }
    }
}

bool BrowserCore::navigate(const QString &address) {
    m_error.clear();
    const QString url = normalize(address, currentUrl(), m_error);
    if (!m_error.isEmpty()) return false;
    if (url != currentUrl()) {
        m_history = m_history.mid(0, m_index + 1);
        m_history.append(url);
        if (m_history.size() > 100) m_history.removeFirst();
        m_index = m_history.size() - 1;
    }
    saveState();
    return true;
}

bool BrowserCore::follow(int index) {
    const auto links = currentPage().links;
    if (index < 0 || index >= links.size()) { m_error = "No link selected."; return false; }
    const QString target = QUrl(currentUrl()).resolved(QUrl(links.at(index).url)).toString();
    return navigate(target);
}

bool BrowserCore::back() {
    m_error.clear();
    if (!canBack()) return false;
    --m_index;
    saveState();
    return true;
}

bool BrowserCore::forward() {
    m_error.clear();
    if (!canForward()) return false;
    ++m_index;
    saveState();
    return true;
}

BrowserPage BrowserCore::currentPage() const {
    if (currentUrl() == homeUrl()) return {homeUrl(), "Favorites", {},
        m_bookmarks.isEmpty() ? QStringList{"No bookmarks."} : QStringList{}, m_bookmarks, true};
    if (currentUrl() == helpUrl()) return {helpUrl(), "Browser help", {},
        {"Navigation\nTab / Shift+Tab, Up / Down: select link\nEnter / Right: follow link\nDigits, then Enter: follow link number\nEsc: cancel link number\nLeft / Backspace / Alt+Left: back\nAlt+Right: forward",
         "Address and bookmarks\nG / Ctrl+L / Command+L: edit address\nH: bookmarks\nCtrl+D / Command+D: add or remove bookmark\nR / F5: reload",
         "Reading\nSpace / PageDown: next screen\nPageUp: previous screen\nHome / End: top / bottom\nF1 / ?: this help page\nQ / Esc: return to terminal"}, {{"Bookmarks", homeUrl()}}, true};
    const QUrl url(currentUrl());
    const QString key = url.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment).toString(QUrl::FullyEncoded);
    if (m_pages.contains(key)) {
        auto page = m_pages.value(key);
        page.url = currentUrl();
        return page;
    }
    bool knownHost = false;
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it)
        if (QUrl(it.key()).host() == url.host()) { knownHost = true; break; }
    if (knownHost) return {currentUrl(), "404 — Page not found", url.host(),
        {"This page may have moved or been removed."}, {{"Home", "https://" + url.host() + '/'}}, false};
    return {currentUrl(), "This site can't be reached", url.host(),
        {"Check the address and try again.", "ERR_NAME_NOT_RESOLVED"}, {{"Favorites", homeUrl()}}, false};
}

bool BrowserCore::isBookmarked() const {
    for (const auto &link : m_bookmarks) if (link.url == currentUrl()) return true;
    return false;
}

bool BrowserCore::toggleBookmark() {
    m_error.clear();
    if (QUrl(currentUrl()).scheme() == "about" || !currentPage().found) { m_error = "This page can't be bookmarked."; return false; }
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        if (m_bookmarks.at(i).url == currentUrl()) { m_bookmarks.removeAt(i); saveState(); return true; }
    }
    if (m_bookmarks.size() >= 100) { m_error = "Bookmark limit reached."; return false; }
    m_bookmarks.append({currentPage().title, currentUrl()});
    saveState();
    return true;
}

void BrowserCore::saveState() {
    m_storageError.clear();
    if (m_stateFile.isEmpty()) return;
    const QFileInfo info(m_stateFile);
    if (info.isSymLink() || info.isJunction()) { m_storageError = "Browser history could not be saved."; return; }
    QSaveFile state(m_stateFile);
    if (!state.open(QIODevice::WriteOnly)) { m_storageError = "Browser history could not be saved."; return; }
    QJsonArray history;
    for (const auto &url : m_history) history.append(url);
    QJsonArray bookmarks;
    for (const auto &link : m_bookmarks) bookmarks.append(QJsonObject{{"label", link.label}, {"url", link.url}});
    const QByteArray data = QJsonDocument(QJsonObject{{"version", 2}, {"history", history},
        {"index", m_index}, {"bookmarks", bookmarks}}).toJson();
    if (state.write(data) != data.size() || !state.commit()) m_storageError = "Browser history could not be saved.";
}
