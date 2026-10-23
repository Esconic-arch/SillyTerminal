#pragma once

#include "browsercore.h"
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;
class QKeyEvent;

class TextBrowser : public QWidget {
public:
    TextBrowser(const QString &stateFile, const QString &initialAddress, const QFont &font,
        int spacing, std::function<void()> close, QWidget *parent = nullptr);
    void focusPage();
    void setAppearance(const QFont &font, int spacing);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    BrowserCore m_core;
    QTextBrowser *m_view;
    QLineEdit *m_address;
    QPushButton *m_back;
    QPushButton *m_forward;
    QPushButton *m_bookmark;
    QLabel *m_status;
    int m_selected = 0;
    int m_spacing;
    std::function<void()> m_close;
    void render(bool preserveScroll = false);
    void open(const QString &address);
    void selectLink(int delta);
    void followLink(int index);
    void goBack();
    void goForward();
    void focusAddress();
    void toggleBookmark();
    void status(const QString &text, bool error = false);
};
