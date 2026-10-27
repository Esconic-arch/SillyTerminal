#include "terminalwidget.h"
#include "sessionwidget.h"
#include "loginwidget.h"
#include "aisettingsdialog.h"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QSslSocket>
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
    auto *title = new QLabel(header);
    title->setObjectName("sessionIdentity");
    title->setStyleSheet("color: #dedee5; font-size: 12px; font-weight: 500;");
    auto *separator = new QLabel("/", header);
    separator->setStyleSheet("color: #686874; font-size: 12px;");
    auto *sessionLabel = new QLabel("Local", header);
    sessionLabel->setStyleSheet("color: #92939f; font-size: 12px;");
    auto *path = new QLabel(header);
    path->setObjectName("sessionPath");
    path->setStyleSheet("color: #b2b2bd; font-size: 12px;");
    headerLayout->addWidget(icon);
    headerLayout->addWidget(title);
    headerLayout->addWidget(separator);
    headerLayout->addWidget(sessionLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(path);
    auto *services = new QPushButton("AI", header);
    services->setObjectName("aiServicesButton");
    services->setToolTip("Configure DeepSeek and ComfyUI services");
    services->setCursor(Qt::PointingHandCursor);
    services->setFixedSize(32, 26);
    services->setStyleSheet(
        "QPushButton { color: #b2b2bd; font-size: 12px; background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { color: #dedee5; background: #393940; }");
    headerLayout->addWidget(services);
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
    const QString worldRoot = smokeIndex >= 0 ? testWorld.path() + "/world" : QString();
    // The smoke process opens AI settings against an empty, isolated config.
    // It must never read the user's service config or existing credentials.
    const AiSettingsStore aiSettings(smokeIndex >= 0 ? testWorld.path() + "/ai-services.json" : QString());
    QObject::connect(services, &QPushButton::clicked, &window, [&window, aiSettings] {
        AiSettingsDialog dialog(&window, aiSettings);
        dialog.exec();
    });
    if (smokeIndex >= 0) {
        const auto created = AccountStore(testWorld.path()).createAndLogin("isolated-preview-password");
        if (!created.authenticated) return 7;
    }
    auto *session = new SessionWidget(worldRoot, &window);
    layout->addWidget(session, 1);
    QObject::connect(appearance, &QPushButton::clicked, session, &SessionWidget::showAppearanceSettings);
    session->onStateChanged = [&window, title, path, appearance, session] {
        title->setText(session->loggedIn() ? session->identity().label() : session->identity().hostName);
        path->setText(session->loggedIn() ? session->currentDirectory() : "tty1");
        appearance->setEnabled(session->loggedIn());
        window.setWindowTitle((session->loggedIn() ? session->currentDirectory() : QStringLiteral("Login"))
            + QString::fromUtf8(" — SillyTerminal"));
    };
    session->onStateChanged();
    session->setFocus();

#ifdef Q_OS_WIN
    styleNativeTitleBar(window);
#endif

    if (smokeIndex >= 0) {
        if (smokeIndex + 1 >= arguments.size()) return 2;
        const QString imagePath = arguments.at(smokeIndex + 1);
        window.winId();
        window.ensurePolished();
        layout->activate();
        QTimer::singleShot(100, &app, [&app, &window, session, services, aiSettings, imagePath] {
            if (!QSslSocket::supportsSsl()) { qWarning("UI smoke: TLS backend unavailable"); app.exit(12); return; }
            bool aiDialogChecked = false;
            QTimer::singleShot(100, &window, [&aiDialogChecked, imagePath] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog || dialog->objectName() != "aiSettingsDialog") return;
                auto *deepSeekKey = dialog->findChild<QLineEdit *>("deepSeekKey");
                auto *comfyUiKey = dialog->findChild<QLineEdit *>("comfyUiKey");
                aiDialogChecked = deepSeekKey && comfyUiKey && deepSeekKey->text().isEmpty()
                    && comfyUiKey->text().isEmpty() && dialog->grab().save(imagePath + ".ai.png");
                dialog->reject();
            });
            services->click();
            if (!aiDialogChecked || QFileInfo::exists(aiSettings.filePath())) { app.exit(13); return; }
            if (!window.grab().save(imagePath + ".login.png")) { app.exit(8); return; }
            auto *input = session->loginPage()->findChild<QLineEdit *>("loginInput");
            input->setText("guest");
            QMetaObject::invokeMethod(input, "returnPressed");
            input->setText("isolated-preview-password");
            QMetaObject::invokeMethod(input, "returnPressed");
            QEventLoop loginLoop;
            QTimer timeout, poll;
            timeout.setSingleShot(true);
            QObject::connect(&timeout, &QTimer::timeout, &loginLoop, &QEventLoop::quit);
            QObject::connect(&poll, &QTimer::timeout, &loginLoop, [&] {
                if (session->loggedIn()) loginLoop.quit();
            });
            timeout.start(12000);
            poll.start(10);
            loginLoop.exec();
            auto *terminal = session->terminal();
            if (!terminal) { app.exit(9); return; }
            if (window.findChild<QLabel *>("sessionIdentity")->text() != session->identity().label()) {
                app.exit(11); return;
            }
            if (!terminal->smokeTest(imagePath + ".appearance.png")) { app.exit(3); return; }
            if (!window.grab().save(imagePath)) { app.exit(4); return; }
            terminal->runCommand("logout");
            if (session->loggedIn()
                || window.findChild<QLabel *>("sessionIdentity")->text() != session->identity().hostName) {
                app.exit(10); return;
            }
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
