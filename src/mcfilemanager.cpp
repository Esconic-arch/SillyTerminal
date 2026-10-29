#include "mcfilemanager.h"

#include <QDateTime>
#include <QFrame>
#include <QFontMetrics>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int nameRole = Qt::UserRole;
constexpr int directoryRole = Qt::UserRole + 1;
const QString help =
    "MIDNIGHT COMMANDER\n\n"
    "Tab / Shift+Tab      Switch panels\n"
    "Up / Down           Select an entry\n"
    "PageUp / PageDown   Scroll the file list\n"
    "Home / End          First / last entry\n"
    "Enter               Enter a directory or view a file\n"
    "Backspace           Parent directory\n"
    "Insert              Mark / unmark an entry and move down\n"
    "Alt+C               Change directory\n"
    "Alt+.               Show / hide dotfiles\n"
    "Ctrl+R              Reload both panels\n\n"
    "F3                  View text, or open an associated image viewer\n"
    "F4                  Edit UTF-8 text with Vim\n"
    "F5                  Copy to the other panel (or enter a destination)\n"
    "F6                  Move / rename (enter a path in the dialog)\n"
    "F7                  Create a directory in the active panel\n"
    "F8                  Delete, after confirmation\n"
    "F10                 Quit\n\n"
    "Alt+1 ... Alt+9, Alt+0 also invoke F1 ... F10.\n"
    "Function labels can be clicked. Double-click opens an entry.\n\n"
    "Copy, move and delete use marked entries, or the highlighted entry\n"
    "when nothing is marked. Directories include their contents.\n"
    "Existing destinations are kept; choose a new name if one exists.\n"
    "A batch stops at the first error; completed operations remain.\n\n"
    "This version has no subshell, user menu, remote filesystem,\n"
    "archive browsing, file masks, executable launching or mcedit.\n"
    "The shell directory is unchanged on exit. The other panel's\n"
    "directory is saved in ~/.config/mc/panels.ini.\n\n"
    "q / Esc exits this help.\n";
}

McFileManager::McFileManager(TerminalCore &core, const QStringList &paths, const QFont &font,
    int spacing, std::function<void()> close, QWidget *parent)
    : QWidget(parent), m_core(core), m_close(std::move(close)) {
    setObjectName("mcFileManager");
    setAutoFillBackground(true);
    setStyleSheet(
        "#mcFileManager { background: #000078; color: #d5d5d5; }"
        "QLabel { background: #000078; color: #d5d5d5; padding: 3px; }"
        "QTreeWidget { background: #000078; color: #d5d5d5; border: 1px solid #8c9eb5;"
        " outline: none; selection-background-color: #23519c; selection-color: white; }"
        "QTreeWidget[active=\"true\"]::item:selected { background: #00aaaa; color: #000020; }"
        "QHeaderView::section { background: #000078; color: #ffff78; border: 0;"
        " border-bottom: 1px solid #8c9eb5; padding: 3px; }"
        "QPushButton { background: #00aaaa; color: #000020; border: 0; padding: 4px; }"
        "QPushButton:focus { background: #d5d5d5; color: #000020; }"
        "QScrollBar:vertical { background: #000060; width: 9px; }"
        "QScrollBar::handle:vertical { background: #8c9eb5; min-height: 18px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "#mcDialog { background: #c6c6c6; border: 2px solid #404040; }"
        "#mcDialog QLabel { background: #c6c6c6; color: #000020; border: 0; }"
        "#mcDialog QLineEdit { background: #000078; color: white; border: 0; padding: 5px; }"
        "#mcDialog QPushButton { border: 1px solid #666666; padding: 5px 16px; }"
        "#mcDialog QPushButton:focus { background: #000078; color: white; }");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 3, 4, 3);
    layout->setSpacing(3);
    auto *heading = new QLabel("Midnight Commander", this);
    heading->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);
    auto *panels = new QHBoxLayout;
    panels->setSpacing(3);
    for (int p = 0; p < 2; ++p) {
        auto *column = new QVBoxLayout;
        column->setSpacing(0);
        m_title[p] = new QLabel(this);
        m_title[p]->setTextFormat(Qt::PlainText);
        m_title[p]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        column->addWidget(m_title[p]);
        m_panel[p] = new QTreeWidget(this);
        m_panel[p]->setObjectName(p ? "mcRightPanel" : "mcLeftPanel");
        m_panel[p]->setHeaderLabels({"Name", "Size", "Modify time"});
        m_panel[p]->setRootIsDecorated(false);
        m_panel[p]->setUniformRowHeights(true);
        m_panel[p]->setSelectionMode(QAbstractItemView::SingleSelection);
        m_panel[p]->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_panel[p]->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_panel[p]->header()->setStretchLastSection(false);
        m_panel[p]->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_panel[p]->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_panel[p]->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_panel[p]->installEventFilter(this);
        connect(m_panel[p], &QTreeWidget::itemDoubleClicked, this, [this, p] { activate(p); enter(); });
        connect(m_panel[p], &QTreeWidget::currentItemChanged, this, [this] { updateStatus(); });
        column->addWidget(m_panel[p], 1);
        panels->addLayout(column, 1);
    }
    layout->addLayout(panels, 1);
    m_status = new QLabel(this);
    m_status->setObjectName("mcStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *keys = new QHBoxLayout;
    keys->setSpacing(2);
    const QList<QPair<int, QString>> actions = {{1, "Help"}, {3, "View"}, {4, "Edit"}, {5, "Copy"},
        {6, "RenMov"}, {7, "Mkdir"}, {8, "Delete"}, {10, "Quit"}};
    for (const auto &action : actions) {
        auto *button = new QPushButton(QString::number(action.first) + " " + action.second, this);
        button->setObjectName("mcAction" + QString::number(action.first));
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, [this, action] {
            if (!m_dialog->isVisible()) operate(Qt::Key_F1 + action.first - 1);
        });
        keys->addWidget(button, 1);
    }
    layout->addLayout(keys);

    m_dialog = new QFrame(this);
    m_dialog->setObjectName("mcDialog");
    auto *dialogLayout = new QVBoxLayout(m_dialog);
    dialogLayout->setContentsMargins(16, 12, 16, 12);
    m_dialogTitle = new QLabel(m_dialog);
    m_dialogTitle->setAlignment(Qt::AlignCenter);
    m_dialogTitle->setTextFormat(Qt::PlainText);
    dialogLayout->addWidget(m_dialogTitle);
    m_dialogPrompt = new QLabel(m_dialog);
    m_dialogPrompt->setTextFormat(Qt::PlainText);
    m_dialogPrompt->setWordWrap(true);
    dialogLayout->addWidget(m_dialogPrompt);
    m_input = new QLineEdit(m_dialog);
    m_input->setObjectName("mcInput");
    m_input->installEventFilter(this);
    dialogLayout->addWidget(m_input);
    auto *dialogButtons = new QHBoxLayout;
    dialogButtons->addStretch();
    m_accept = new QPushButton("OK", m_dialog);
    m_accept->setObjectName("mcAccept");
    m_cancel = new QPushButton("Cancel", m_dialog);
    m_cancel->setObjectName("mcCancel");
    for (auto *button : {m_accept, m_cancel}) {
        button->installEventFilter(this);
        dialogButtons->addWidget(button);
    }
    connect(m_accept, &QPushButton::clicked, this, [this] { acceptDialog(); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { closeDialog(); });
    dialogLayout->addLayout(dialogButtons);
    m_dialog->hide();

    m_path[0] = paths.value(0, core.currentPath());
    m_path[1] = paths.value(1, core.currentPath());
    if (paths.size() < 2) {
        const auto saved = core.readTextFile(core.identity().homePath + "/.config/mc/panels.ini");
        if (!saved.error) {
            for (const auto &line : saved.text.split('\n')) {
                if (!line.startsWith("other_dir=")) continue;
                const auto directory = core.listDirectory(line.mid(10));
                if (directory.error.isEmpty()) m_path[1] = directory.path;
            }
        }
    }
    setAppearance(font, spacing);
    refreshAll();
    activate(0);
}

void McFileManager::setAppearance(const QFont &font, int spacing) {
    setFont(font);
    const int padding = qMax(1, QFontMetrics(font).height() * (spacing - 100) / 200);
    for (auto *panel : m_panel) panel->setStyleSheet(QString("QTreeWidget::item { padding-top: %1px; padding-bottom: %1px; }").arg(padding));
    placeDialog();
}

QString McFileManager::absolute(const QString &path, int panel) const {
    if (path.startsWith('/') || path == "~" || path.startsWith("~/")) return path;
    return (m_path[panel] == "/" ? QString("/") : m_path[panel] + '/') + path;
}

void McFileManager::activate(int panel) {
    m_active = panel;
    for (int p = 0; p < 2; ++p) {
        m_panel[p]->setProperty("active", p == panel);
        m_panel[p]->style()->unpolish(m_panel[p]);
        m_panel[p]->style()->polish(m_panel[p]);
        m_title[p]->setText((p == panel ? "[ " : "  ") + m_path[p] + (p == panel ? " ]" : ""));
        m_title[p]->setToolTip(m_path[p]);
    }
    updateStatus();
}

void McFileManager::focusPanel() { m_panel[m_active]->setFocus(); }

void McFileManager::updateMark(QTreeWidgetItem *item, int panel) {
    const QString name = item->data(0, nameRole).toString();
    const bool marked = m_marked[panel].contains(name);
    item->setText(0, (marked ? "*" : item->data(0, directoryRole).toBool() ? "/" : " ") + name);
    for (int c = 0; c < 3; ++c) item->setForeground(c, marked ? QColor("#ffff55") : QColor("#d5d5d5"));
}

QString McFileManager::refresh(int panel, const QString &select) {
    auto *tree = m_panel[panel];
    QString selected = select;
    if (selected.isEmpty() && tree->currentItem()) selected = tree->currentItem()->data(0, nameRole).toString();
    const int row = tree->indexOfTopLevelItem(tree->currentItem());
    const QSignalBlocker blocker(tree);
    tree->clear();
    auto directory = m_core.listDirectory(m_path[panel]);
    const QString error = directory.error;
    while (!directory.error.isEmpty() && directory.path != "/") directory = m_core.listDirectory(directory.path + "/..");
    m_path[panel] = directory.path;
    QSet<QString> remaining;
    const auto append = [&](const WorldEntry &entry) {
        auto *item = new QTreeWidgetItem(tree);
        item->setData(0, nameRole, entry.name);
        item->setData(0, directoryRole, entry.directory);
        item->setText(1, entry.directory ? "<DIR>" : QString::number(entry.size));
        item->setText(2, entry.modified.isValid() ? entry.modified.toString("MMM dd HH:mm") : QString());
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        updateMark(item, panel);
        if (entry.name == selected) tree->setCurrentItem(item);
    };
    if (directory.path != "/" && directory.error.isEmpty()) append({"..", true, 0, {}});
    for (const auto &entry : directory.entries) {
        if (!m_hidden && entry.name.startsWith('.')) continue;
        append(entry);
        if (m_marked[panel].contains(entry.name)) remaining.insert(entry.name);
    }
    m_marked[panel] = remaining;
    if (!tree->currentItem() && tree->topLevelItemCount())
        tree->setCurrentItem(tree->topLevelItem(qBound(0, row, tree->topLevelItemCount() - 1)));
    return error;
}

void McFileManager::refreshAll() {
    const QString leftError = refresh(0);
    const QString rightError = refresh(1);
    activate(m_active);
    QStringList errors;
    if (!leftError.isEmpty()) errors.append(leftError);
    if (!rightError.isEmpty()) errors.append(rightError);
    if (!errors.isEmpty()) m_status->setText(errors.join("; "));
}

void McFileManager::updateStatus() {
    if (!m_status) return;
    const auto *item = m_panel[m_active]->currentItem();
    if (!m_marked[m_active].isEmpty()) m_status->setText(QString("%1 marked entries").arg(m_marked[m_active].size()));
    else if (item) m_status->setText(item->data(0, nameRole).toString() + "    " + item->text(1) + "    " + item->text(2));
    else m_status->setText("Empty directory");
}

bool McFileManager::navigate(const QString &path) {
    const auto directory = m_core.listDirectory(absolute(path, m_active));
    if (!directory.error.isEmpty()) { m_status->setText(directory.error); return false; }
    const QString previous = m_path[m_active];
    const bool parent = directory.path == m_core.listDirectory(previous + "/..").path;
    m_path[m_active] = directory.path;
    m_marked[m_active].clear();
    m_panel[m_active]->clear();
    refresh(m_active, parent ? previous.section('/', -1) : "..");
    activate(m_active);
    return true;
}

void McFileManager::enter() {
    const auto *item = m_panel[m_active]->currentItem();
    if (!item) return;
    if (item->data(0, directoryRole).toBool()) navigate(item->data(0, nameRole).toString());
    else view(false);
}

void McFileManager::view(bool edit) {
    const auto *item = m_panel[m_active]->currentItem();
    if (!item || item->data(0, nameRole).toString() == "..") return;
    if (item->data(0, directoryRole).toBool()) { m_status->setText("Select a text file"); return; }
    const QString path = absolute(item->data(0, nameRole).toString(), m_active);
    if (!edit && viewFile) {
        hide();
        const bool handled = viewFile(path);
        show();
        raise();
        if (handled) { refreshAll(); focusPanel(); return; }
    }
    const auto text = m_core.readTextFile(path);
    if (text.error) { m_status->setText(text.text); return; }
    const auto callback = edit ? editText : viewText;
    if (!callback) { m_status->setText("No text handler is available"); return; }
    hide();
    callback(path, text.text);
    show();
    raise();
    refreshAll();
    focusPanel();
}

void McFileManager::showHelp() {
    if (!viewText) return;
    hide();
    viewText("Midnight Commander help", help);
    show();
    raise();
    focusPanel();
}

void McFileManager::mark() {
    auto *tree = m_panel[m_active];
    auto *item = tree->currentItem();
    if (!item) return;
    const QString name = item->data(0, nameRole).toString();
    if (name == "..") return;
    if (m_marked[m_active].contains(name)) m_marked[m_active].remove(name);
    else m_marked[m_active].insert(name);
    updateMark(item, m_active);
    const int next = qMin(tree->indexOfTopLevelItem(item) + 1, tree->topLevelItemCount() - 1);
    tree->setCurrentItem(tree->topLevelItem(next));
    updateStatus();
}

QStringList McFileManager::selectedNames() const {
    QStringList names;
    auto *tree = m_panel[m_active];
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        const QString name = tree->topLevelItem(i)->data(0, nameRole).toString();
        if (m_marked[m_active].contains(name)) names.append(name);
    }
    if (names.isEmpty() && tree->currentItem()) {
        const QString name = tree->currentItem()->data(0, nameRole).toString();
        if (name != "..") names.append(name);
    }
    return names;
}

void McFileManager::operate(int key) {
    if (m_dialog->isVisible()) return;
    if (key == Qt::Key_F1) { showHelp(); return; }
    if (key == Qt::Key_F10) { finish(); return; }
    if (key == Qt::Key_F3 || key == Qt::Key_F4) { view(key == Qt::Key_F4); return; }
    if (key == Qt::Key_F7) {
        showDialog("Create a new directory", "Directory name:", {}, true, [this](const QString &name) {
            if (name.isEmpty()) { m_status->setText("Directory name is empty"); return; }
            const auto result = m_core.createDirectory(absolute(name, m_active));
            refreshAll();
            if (result.error) m_status->setText(result.text);
        });
        return;
    }
    if (key != Qt::Key_F5 && key != Qt::Key_F6 && key != Qt::Key_F8) return;
    const auto names = selectedNames();
    if (names.isEmpty()) { m_status->setText("No file selected"); return; }
    const QString description = names.size() == 1 ? names.first() : QString("%1 marked entries").arg(names.size());
    const bool deleting = key == Qt::Key_F8;
    const bool moving = key == Qt::Key_F6;
    const QString action = deleting ? "Delete" : moving ? "Move" : "Copy";
    const QString prompt = deleting ? "Delete " + description + "?\nDirectories will be deleted with all their contents."
        : action + " " + description + " to:";
    showDialog(action, prompt, deleting ? QString() : m_path[1 - m_active] + '/', !deleting,
        [this, names, deleting, moving, action](const QString &destination) {
            if (!deleting && destination.isEmpty()) { m_status->setText("Destination is empty"); return; }
            const QString target = absolute(destination, m_active);
            if (!deleting && names.size() > 1 && !m_core.listDirectory(target).error.isEmpty()) {
                m_status->setText("Multiple entries require an existing destination directory"); return;
            }
            int completed = 0;
            QString error;
            for (const auto &name : names) {
                const QString source = absolute(name, m_active);
                const auto result = deleting ? m_core.removeEntry(source)
                    : moving ? m_core.moveEntry(source, target) : m_core.copyEntry(source, target);
                if (result.error) { error = name + ": " + result.text; break; }
                ++completed;
                m_marked[m_active].remove(name);
            }
            refreshAll();
            m_status->setText(QString("%1: %2 of %3 completed").arg(action).arg(completed).arg(names.size())
                + (error.isEmpty() ? QString() : " — " + error));
        });
}

void McFileManager::showDialog(const QString &title, const QString &prompt, const QString &initial,
    bool input, std::function<void(const QString &)> action) {
    m_dialogTitle->setText(title);
    m_dialogPrompt->setText(prompt);
    m_input->setVisible(input);
    m_input->setText(initial);
    m_accept->setText(input ? "OK" : "Delete");
    m_acceptAction = std::move(action);
    // Disabling a focused panel causes Qt to focus its sibling. Keep the
    // operation's source panel fixed throughout dialog setup and teardown.
    m_modal = true;
    for (auto *panel : m_panel) panel->setEnabled(false);
    m_dialog->show();
    placeDialog();
    m_dialog->raise();
    if (input) { m_input->setFocus(); m_input->selectAll(); }
    else m_cancel->setFocus();
}

void McFileManager::placeDialog() {
    if (!m_dialog || !m_dialog->isVisible()) return;
    m_dialog->setFixedWidth(qMax(160, qMin(600, width() - 24)));
    m_dialog->adjustSize();
    m_dialog->move((width() - m_dialog->width()) / 2, (height() - m_dialog->height()) / 2);
}

void McFileManager::closeDialog() {
    m_dialog->hide();
    m_acceptAction = {};
    for (auto *panel : m_panel) panel->setEnabled(true);
    m_modal = false;
    focusPanel();
}

void McFileManager::acceptDialog() {
    const QString text = m_input->text();
    auto action = m_acceptAction;
    closeDialog();
    if (action) action(text);
}

void McFileManager::finish() {
    const QString config = m_core.identity().homePath + "/.config";
    const auto directory = m_core.listDirectory(config);
    QString warning;
    if (!directory.error.isEmpty()) warning = m_core.createDirectory(config).text;
    if (warning.isEmpty() && !m_core.listDirectory(config + "/mc").error.isEmpty())
        warning = m_core.createDirectory(config + "/mc").text;
    if (warning.isEmpty()) {
        const auto saved = m_core.saveFile(config + "/mc/panels.ini", "[Dirs]\nother_dir=" + m_path[1 - m_active] + "\n");
        if (saved.error) warning = saved.text;
    }
    if (!warning.isEmpty()) {
        showDialog("Cannot save panel directory", warning + "\nQuit without saving?", {}, false,
            [this](const QString &) { m_close(); });
        m_accept->setText("Quit");
        return;
    }
    m_close();
}

bool McFileManager::eventFilter(QObject *object, QEvent *event) {
    if (m_dialog->isVisible()) {
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_F10) { closeDialog(); return true; }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                if (object == m_cancel) closeDialog(); else acceptDialog();
                return true;
            }
        }
        return QWidget::eventFilter(object, event);
    }
    for (int p = 0; p < 2; ++p) {
        if (object != m_panel[p]) continue;
        if (event->type() == QEvent::FocusIn && !m_modal) activate(p);
        if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(object, event);
        auto *key = static_cast<QKeyEvent *>(event);
        const bool alt = key->modifiers().testFlag(Qt::AltModifier);
        const bool control = key->modifiers().testFlag(Qt::ControlModifier);
        if (key->key() >= Qt::Key_F1 && key->key() <= Qt::Key_F10) { operate(key->key()); return true; }
        if (alt && key->key() >= Qt::Key_0 && key->key() <= Qt::Key_9) {
            operate(key->key() == Qt::Key_0 ? Qt::Key_F10 : Qt::Key_F1 + key->key() - Qt::Key_1); return true;
        }
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            activate(1 - m_active); focusPanel(); return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { enter(); return true; }
        if (key->key() == Qt::Key_Backspace) { navigate(".."); return true; }
        if (key->key() == Qt::Key_Insert) { mark(); return true; }
        if (control && key->key() == Qt::Key_R) { refreshAll(); return true; }
        if (alt && key->key() == Qt::Key_Period) { m_hidden = !m_hidden; refreshAll(); return true; }
        if (alt && key->key() == Qt::Key_C) {
            showDialog("Change directory", "Directory:", m_path[m_active], true,
                [this](const QString &path) { if (!path.isEmpty()) navigate(path); });
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

void McFileManager::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    // Keep the filename usable at small terminal sizes.
    for (auto *panel : m_panel) panel->setColumnHidden(2, width() < 760);
    placeDialog();
}
