#pragma once

#include "terminalcore.h"
#include <QSet>
#include <QWidget>
#include <functional>

class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class McFileManager : public QWidget {
public:
    McFileManager(TerminalCore &core, const QStringList &paths, const QFont &font, int spacing,
        std::function<void()> close, QWidget *parent = nullptr);
    void focusPanel();
    void setAppearance(const QFont &font, int spacing);
    std::function<void(const QString &, const QString &)> viewText;
    std::function<void(const QString &, const QString &)> editText;
    // The host may handle a non-text file with an installed viewer.
    // Return false to fall back to the text pager.
    std::function<bool(const QString &)> viewFile;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    TerminalCore &m_core;
    QTreeWidget *m_panel[2];
    QLabel *m_title[2];
    QString m_path[2];
    QSet<QString> m_marked[2];
    QLabel *m_status = nullptr;
    QFrame *m_dialog = nullptr;
    QLabel *m_dialogTitle;
    QLabel *m_dialogPrompt;
    QLineEdit *m_input;
    QPushButton *m_accept;
    QPushButton *m_cancel;
    int m_active = 0;
    bool m_hidden = true;
    bool m_modal = false;
    std::function<void()> m_close;
    std::function<void(const QString &)> m_acceptAction;

    void activate(int panel);
    QString refresh(int panel, const QString &select = {});
    void refreshAll();
    bool navigate(const QString &path);
    void enter();
    void view(bool edit);
    void mark();
    void operate(int key);
    void showHelp();
    void finish();
    void updateStatus();
    void updateMark(QTreeWidgetItem *item, int panel);
    QStringList selectedNames() const;
    QString absolute(const QString &path, int panel) const;
    void showDialog(const QString &title, const QString &prompt, const QString &initial,
        bool input, std::function<void(const QString &)> action);
    void acceptDialog();
    void closeDialog();
    void placeDialog();
};
