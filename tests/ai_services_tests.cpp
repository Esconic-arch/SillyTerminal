#include "aiservices.h"
#include "aisettingsdialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPalette>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <cstdio>
#include <functional>

namespace {
bool until(const std::function<bool()> &condition, int timeout = 3000) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
class MemorySecrets final : public AiSecretStore {
public:
    QMap<QString, QString> data;
    int writes = 0;
    int failAt = -1;
    std::function<void()> afterWrite;
    bool read(const QString &id, QString *secret, QString *error) override {
        if (!data.contains(id)) { if (error) *error = "Test credential is unavailable."; return false; }
        *secret = data.value(id); return true;
    }
    bool write(const QString &id, const QString &secret, QString *error) override {
        if (++writes == failAt) { if (error) *error = "Test credential storage is locked."; return false; }
        data.insert(id, secret);
        if (afterWrite) afterWrite();
        return true;
    }
    bool remove(const QString &id, QString *) override { data.remove(id); return true; }
};
class HttpFixture : public QObject {
public:
    QTcpServer server;
    QList<QByteArray> requests;
    int status = 200;
    QByteArray body = R"({"data":[{"id":"test-model"},{"id":"deepseek-flash"}]})";
    QByteArray extraHeaders;
    bool stall = false;
    HttpFixture() {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    auto buffer = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", buffer);
                    if (!buffer.contains("\r\n\r\n") || socket->property("handled").toBool()) return;
                    socket->setProperty("handled", true);
                    requests.append(buffer);
                    if (stall) return;
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\n"
                        "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size())
                        + "\r\nConnection: close\r\n" + extraHeaders + "\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString url() const { return "http://127.0.0.1:" + QString::number(server.serverPort()); }
};
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    QCoreApplication::setOrganizationName("SillyTerminalTests");
    QCoreApplication::setApplicationName("AiServices");
    QTemporaryDir temp(QDir::current().filePath("ai-tests-XXXXXX"));
    if (!temp.isValid()) return 2;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *message) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
    };
    if (app.arguments().contains("--native-secret-check")) {
        // Opt-in integration check: a new random entry containing a synthetic
        // key, always removed. No existing credential entry is accessed.
        auto native = nativeAiSecretStore();
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString value, nativeError;
        const bool stored = native->write(id, "synthetic-native-store-check", &nativeError);
        const bool restored = stored && native->read(id, &value, &nativeError)
            && value == "synthetic-native-store-check";
        const bool removed = native->remove(id, &nativeError);
        check(stored && restored && removed, "native credential store round trip and cleanup");
        std::printf("Native credential checks: %d, failures: %d\n", checks, failures);
        return failures ? 1 : 0;
    }
    auto secrets = std::make_shared<MemorySecrets>();
    const QString configPath = temp.path() + "/settings/ai.json";
    AiSettingsStore store(configPath, secrets);
    AiServiceSettings settings;
    QString error, key;
    check(store.load(&settings, &error) && settings.deepSeekUrl == "https://api.deepseek.com",
        "missing config uses defaults without looking up any real credentials");
    settings.deepSeekUrl += "/v1/";
    settings.deepSeekModel = "test-model";
    check(store.save(&settings, "synthetic-deepseek-secret", "synthetic-comfy-secret", &error),
        "save configuration with an injected credential store");
    const auto before = read(configPath);
    check(!before.contains("synthetic-") && secrets->data.size() == 2,
        "configuration contains credential references and no API keys");
    AiServiceSettings reopened;
    check(store.load(&reopened, &error) && reopened.deepSeekUrl.endsWith("/v1")
        && reopened.deepSeekModel == "test-model" && store.readKey(reopened.deepSeekKeyRef, &key, &error)
        && key == "synthetic-deepseek-secret", "saved configuration and keys can be reopened independently");
    const auto oldSecrets = secrets->data;
    secrets->failAt = secrets->writes + 2;
    check(!store.save(&settings, "replacement-one", "replacement-two", &error)
        && read(configPath) == before && secrets->data == oldSecrets,
        "partial credential write failure rolls back and preserves the previous usable configuration");
    secrets->failAt = -1;
    check(!store.save(&settings, "bad\nheader", "", &error) && read(configPath) == before,
        "newline in a key is rejected before persisting anything");
    check(store.save(&settings, "replacement", "", &error) && secrets->data.size() == 1
        && settings.comfyUiKeyRef.isEmpty(), "replacing and clearing keys removes obsolete credential entries");
    const QString broken = temp.path() + "/corrupt.json";
    write(broken, "not-json");
    AiSettingsStore brokenStore(broken, secrets);
    check(!brokenStore.load(&reopened, &error)
        && !brokenStore.save(&settings, "replacement", "", &error) && read(broken) == "not-json",
        "invalid saved configuration is reported and never silently overwritten");
    auto commitSecrets = std::make_shared<MemorySecrets>();
    const QString blocked = temp.path() + "/blocked.json";
    AiSettingsStore blockedStore(blocked, commitSecrets);
    commitSecrets->afterWrite = [&] { QDir().mkpath(blocked); };
    check(!blockedStore.save(&settings, "synthetic", "", &error) && commitSecrets->data.isEmpty(),
        "failed file commit removes newly written credentials");

    HttpFixture fixture;
    check(fixture.server.isListening(), "loopback HTTP fixture is listening");
    AiConnectionClient client;
    AiConnectionResult result;
    int completions = 0;
    client.onFinished = [&](const AiConnectionResult &value) {
        result = value; ++completions;
    };
    int expected = completions + 1;
    client.checkDeepSeek(fixture.url() + "/v1/", "synthetic-key");
    check(until([&] { return completions == expected; }) && result.success && result.models.contains("test-model")
        && fixture.requests.last().startsWith("GET /v1/models HTTP/")
        && fixture.requests.last().contains("Authorization: Bearer synthetic-key"),
        "DeepSeek check preserves API prefix and authenticates a read-only model request");
    fixture.body = R"({"system":{"comfyui_version":"test"},"devices":[]})";
    expected = completions + 1;
    client.checkComfyUi(fixture.url());
    check(until([&] { return completions == expected; }) && result.success
        && fixture.requests.last().startsWith("GET /system_stats HTTP/")
        && !fixture.requests.last().contains("Authorization:"),
        "local ComfyUI connection check works without an API key or image job");
    expected = completions + 1;
    client.checkComfyUi(fixture.url() + "/comfy/", "synthetic-proxy-token");
    check(until([&] { return completions == expected; }) && result.success
        && fixture.requests.last().startsWith("GET /comfy/system_stats HTTP/")
        && fixture.requests.last().contains("Authorization: Bearer synthetic-proxy-token"),
        "ComfyUI supports a reverse-proxy path and optional bearer token");
    fixture.status = 401; fixture.body = "reflected synthetic-key";
    expected = completions + 1;
    client.checkDeepSeek(fixture.url(), "synthetic-key");
    check(until([&] { return completions == expected; }) && !result.success
        && result.message.contains("Authentication") && !result.message.contains("synthetic-key"),
        "authentication errors do not echo an untrusted response body or secret");
    fixture.status = 200; fixture.body = "<html>Welcome</html>";
    expected = completions + 1; client.checkComfyUi(fixture.url());
    check(until([&] { return completions == expected; }) && !result.success,
        "a reachable HTML web page is not reported as a working API");
    fixture.body = R"({"data":[]})";
    expected = completions + 1; client.checkDeepSeek(fixture.url(), "synthetic");
    check(until([&] { return completions == expected; }) && !result.success,
        "an empty model list cannot pass DeepSeek connection validation");
    fixture.body = "{}";
    expected = completions + 1; client.checkComfyUi(fixture.url());
    check(until([&] { return completions == expected; }) && !result.success,
        "an unrelated JSON service is not reported as ComfyUI");
    HttpFixture redirectTarget;
    fixture.status = 302;
    fixture.extraHeaders = "Location: " + redirectTarget.url().toUtf8() + "/stolen\r\n";
    expected = completions + 1; client.checkDeepSeek(fixture.url(), "synthetic-key");
    check(until([&] { return completions == expected; }) && !result.success
        && result.message.contains("redirected") && redirectTarget.requests.isEmpty(),
        "redirects are refused without forwarding API credentials");
    fixture.status = 200; fixture.extraHeaders.clear(); fixture.body = QByteArray(1024 * 1024 + 32, 'x');
    expected = completions + 1; client.checkComfyUi(fixture.url());
    check(until([&] { return completions == expected; }) && !result.success && result.message.contains("limit"),
        "large responses are bounded rather than retained without limit");
    fixture.stall = true;
    expected = completions + 1; client.checkComfyUi(fixture.url(), {}, 80);
    bool responsive = false;
    QTimer::singleShot(0, &app, [&] { responsive = true; });
    check(until([&] { return completions == expected; }) && !result.success
        && result.message.contains("timed out") && responsive && !client.busy(),
        "an absolute timeout ends a stalled request while the UI stays responsive");
    client.checkComfyUi(fixture.url());
    client.cancel();
    check(!client.busy(), "cancel detaches and aborts a pending connection check");
    const int previousRequests = fixture.requests.size();
    expected = completions + 1; client.checkDeepSeek("https://user:pass@example.invalid?secret=x", "key");
    check(completions == expected && !result.success && fixture.requests.size() == previousRequests,
        "credentials in the URL and query parameters are rejected locally");

    fixture.stall = false;
    fixture.body = R"({"data":[{"id":"test-model"},{"id":"deepseek-flash"}]})";
    const QString uiPath = temp.path() + "/ui.json";
    auto uiSecrets = std::make_shared<MemorySecrets>();
    AiSettingsStore uiStore(uiPath, uiSecrets);
    {
        const int beforeOpen = fixture.requests.size();
        AiSettingsDialog dialog(nullptr, uiStore);
        dialog.findChild<QLineEdit *>("deepSeekUrl")->setText(fixture.url());
        dialog.findChild<QLineEdit *>("deepSeekKey")->setText("synthetic-ui-key");
        dialog.findChild<QComboBox *>("deepSeekModel")->setEditText("test-model");
        QCoreApplication::processEvents();
        check(!QFileInfo::exists(uiPath) && uiSecrets->data.isEmpty() && fixture.requests.size() == beforeOpen,
            "opening and editing settings never saves keys or makes automatic requests");
        auto *test = dialog.findChild<QPushButton *>("testDeepSeek");
        auto *status = dialog.findChild<QLabel *>("deepSeekStatus");
        test->click();
        check(until([&] { return test->isEnabled(); }) && status->text().startsWith("Connected.")
            && dialog.findChild<QComboBox *>("deepSeekModel")->count() == 2,
            "settings dialog asynchronously loads and displays models");
        check(!QFileInfo::exists(uiPath), "connection testing does not implicitly save draft configuration");
        dialog.reject();
        check(uiSecrets->data.isEmpty() && !QFileInfo::exists(uiPath), "Cancel discards settings and entered keys");
    }
    {
        AiSettingsDialog dialog(nullptr, uiStore);
        dialog.findChild<QLineEdit *>("deepSeekKey")->setText("synthetic-ui-key");
        dialog.findChild<QPushButton *>("saveAiSettings")->click();
        check(dialog.result() == QDialog::Accepted && !read(uiPath).contains("synthetic-ui-key"),
            "Save commits a settings draft without storing its key in JSON");
    }
    {
        AiSettingsDialog dialog(nullptr, uiStore);
        check(dialog.findChild<QLineEdit *>("deepSeekKey")->text() == "synthetic-ui-key"
            && dialog.findChild<QLineEdit *>("deepSeekKey")->echoMode() == QLineEdit::Password,
            "reopening settings restores a saved key in a masked field");
        QPalette palette;
        palette.setColor(QPalette::Window, QColor("#1c1c1f"));
        palette.setColor(QPalette::WindowText, QColor("#dedee5"));
        palette.setColor(QPalette::Base, QColor("#1c1c1f"));
        palette.setColor(QPalette::Text, QColor("#dedee5"));
        palette.setColor(QPalette::Button, QColor("#29292e"));
        palette.setColor(QPalette::ButtonText, QColor("#dedee5"));
        palette.setColor(QPalette::PlaceholderText, QColor("#92939f"));
        dialog.setPalette(palette);
        dialog.show();
        QCoreApplication::processEvents();
        check(dialog.grab().save(QCoreApplication::applicationDirPath() + "/ai-settings-preview.png"),
            "capture settings preview using synthetic configuration only");
    }
    const auto savedUi = read(uiPath);
    uiSecrets->data.clear();
    {
        AiSettingsDialog dialog(nullptr, uiStore);
        dialog.findChild<QPushButton *>("saveAiSettings")->click();
        check(dialog.result() != QDialog::Accepted && read(uiPath) == savedUi,
            "unreadable credentials are not silently erased by saving unrelated settings");
        dialog.findChild<QPushButton *>("deepSeekKeyClear")->click();
        dialog.findChild<QPushButton *>("saveAiSettings")->click();
        check(dialog.result() == QDialog::Accepted, "explicit Clear can remove an unavailable saved key");
    }
    for (const auto &request : fixture.requests)
        check(request.startsWith("GET "), "all connection checks are read-only requests");
    std::printf("AI service checks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
