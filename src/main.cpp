#include "terminalwidget.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#ifdef Q_OS_WIN
#include <QLibrary>
#include <qt_windows.h>

// Keep native controls and resizing. Unsupported DWM attributes are ignored.
static void styleNativeTitleBar(QWidget &window) {
    QLibrary dwm(QStringLiteral("dwmapi"));
    using SetAttribute = HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    const auto setAttribute = reinterpret_cast<SetAttribute>(dwm.resolve("DwmSetWindowAttribute"));
    if (!setAttribute) return;
    const HWND handle = reinterpret_cast<HWND>(window.winId());
    const BOOL dark = TRUE;
    if (FAILED(setAttribute(handle, 20, &dark, sizeof(dark))))
        setAttribute(handle, 19, &dark, sizeof(dark));
    const COLORREF caption = RGB(37, 38, 42);
    const COLORREF border = RGB(55, 55, 61);
    setAttribute(handle, 35, &caption, sizeof(caption));
    setAttribute(handle, 34, &border, sizeof(border));
}
#endif

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("SillyTerminal");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCoreApplication::setOrganizationName("Esconic");
    const QStringList arguments = app.arguments();
    const int smokeIndex = arguments.indexOf("--smoke-test");
    if (smokeIndex >= 0) {
        if (smokeIndex + 1 >= arguments.size()) return 2;
        // Exercise preference saving in an isolated build-local INI store.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
            QFileInfo(arguments.at(smokeIndex + 1)).absolutePath() + "/test-settings");
        QSettings settings;
        settings.clear();
        settings.sync();
    }
    app.setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#1c1c1f"));
    palette.setColor(QPalette::WindowText, QColor("#dedee5"));
    palette.setColor(QPalette::Base, QColor("#1c1c1f"));
    palette.setColor(QPalette::Text, QColor("#dedee5"));
    palette.setColor(QPalette::Button, QColor("#29292e"));
    palette.setColor(QPalette::ButtonText, QColor("#dedee5"));
    palette.setColor(QPalette::Highlight, QColor("#45465b"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    app.setPalette(palette);

    QWidget window;
    window.setWindowTitle(QString::fromUtf8("~ — SillyTerminal"));
    window.resize(980, 640);
    window.setMinimumSize(560, 360);
    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(&window);
    header->setObjectName("sessionHeader");
    header->setStyleSheet("#sessionHeader { background: #25262a; border-bottom: 1px solid #36363d; }");
    header->setFixedHeight(40);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 0, 24, 0);
    headerLayout->setSpacing(12);

    auto *icon = new QLabel(">_", header);
    icon->setStyleSheet("color: #a5a5b0; font-family: monospace; font-size: 13px; font-weight: 600;");
    auto *title = new QLabel("guest@silly", header);
    title->setStyleSheet("color: #dedee5; font-size: 12px; font-weight: 500;");
    auto *separator = new QLabel("/", header);
    separator->setStyleSheet("color: #686874; font-size: 12px;");
    auto *session = new QLabel("Local", header);
    session->setStyleSheet("color: #92939f; font-size: 12px;");
    auto *path = new QLabel("~", header);
    path->setObjectName("sessionPath");
    path->setStyleSheet("color: #b2b2bd; font-size: 12px;");
    headerLayout->addWidget(icon);
    headerLayout->addWidget(title);
    headerLayout->addWidget(separator);
    headerLayout->addWidget(session);
    headerLayout->addStretch();
    headerLayout->addWidget(path);
    auto *appearance = new QPushButton("Aa", header);
    appearance->setToolTip("Font & spacing (Ctrl+, / Command+,)");
    appearance->setCursor(Qt::PointingHandCursor);
    appearance->setFixedSize(32, 26);
    appearance->setStyleSheet(
        "QPushButton { color: #b2b2bd; font-size: 13px; background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { color: #dedee5; background: #393940; }"
        "QPushButton:pressed { background: #454550; }");
    headerLayout->addWidget(appearance);
    layout->addWidget(header);

    QTemporaryDir testWorld(smokeIndex >= 0
        ? QFileInfo(arguments.at(smokeIndex + 1)).absolutePath() + "/ui-world-XXXXXX" : QString());
    if (smokeIndex >= 0 && !testWorld.isValid()) return 6;
    auto *terminal = new TerminalWidget(&window,
        smokeIndex >= 0 ? testWorld.path() + "/world" : QString());
    layout->addWidget(terminal, 1);
    QObject::connect(appearance, &QPushButton::clicked, terminal, &TerminalWidget::showAppearanceSettings);
    QObject::connect(terminal, &QTextEdit::textChanged, &window,
        [&window, path, terminal] {
            const QString directory = terminal->currentDirectory();
            if (path->text() == directory) return;
            path->setText(directory);
            window.setWindowTitle(directory + QString::fromUtf8(" — SillyTerminal"));
        });
    terminal->setFocus();

#ifdef Q_OS_WIN
    styleNativeTitleBar(window);
#endif

    if (smokeIndex >= 0) {
        if (smokeIndex + 1 >= arguments.size()) return 2;
        const QString imagePath = arguments.at(smokeIndex + 1);
        window.winId();
        window.ensurePolished();
        layout->activate();
        QTimer::singleShot(100, &app, [&app, &window, terminal, imagePath] {
            if (!terminal->smokeTest(imagePath + ".appearance.png")) { app.exit(3); return; }
            if (!window.grab().save(imagePath)) { app.exit(4); return; }
            QFile result(imagePath + ".ok");
            if (!result.open(QIODevice::WriteOnly | QIODevice::Truncate)) { app.exit(5); return; }
            result.write("UI_CHECK_OK\n");
            result.close();
            app.exit(0);
        });
    } else {
        window.show();
    }
    return app.exec();
}
