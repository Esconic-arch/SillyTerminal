#pragma once

#include <QWidget>
#include <QRegularExpression>
#include <functional>

class QTextEdit;
class QLineEdit;
class QLabel;

class TextPager : public QWidget {
public:
    TextPager(const QString &title, const QString &content, const QFont &font, int spacing,
        std::function<void()> close, QWidget *parent = nullptr);
    void focusPage();
    void setAppearance(const QFont &font, int spacing);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    QTextEdit *m_view;
    QLineEdit *m_search;
    QLabel *m_status;
    QString m_title;
    QRegularExpression m_pattern;
    bool m_hasPattern = false;
    bool m_backward = false;
    std::function<void()> m_close;
    void find(bool backward, bool first);
    void refreshStatus();
};
