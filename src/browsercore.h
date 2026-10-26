#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct BrowserLink { QString label; QString url; };
struct BrowserPage {
    QString url;
    QString title;
    QString subtitle;
    QStringList paragraphs;
    QList<BrowserLink> links;
    bool found = false;
    QString html;
};

class BrowserCore {
public:
    explicit BrowserCore(const QString &stateFile = {});
    bool navigate(const QString &address);
    bool follow(int index);
    bool back();
    bool forward();
    bool canBack() const { return m_index > 0; }
    bool canForward() const { return m_index + 1 < m_history.size(); }
    QString currentUrl() const { return m_history.value(m_index, homeUrl()); }
    BrowserPage currentPage() const;
    QString error() const { return m_error; }
    QString storageError() const { return m_storageError; }
    bool isBookmarked() const;
    bool toggleBookmark();
    static QString homeUrl() { return QStringLiteral("about:bookmarks"); }
    static QString helpUrl() { return QStringLiteral("about:help"); }

private:
    QMap<QString, BrowserPage> m_pages;
    QStringList m_history;
    int m_index = 0;
    QString m_stateFile;
    QString m_error;
    QString m_storageError;
    QList<BrowserLink> m_bookmarks;
    static QString normalize(const QString &address, const QString &base, QString &error);
    void saveState();
};
