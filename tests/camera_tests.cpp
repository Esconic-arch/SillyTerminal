#include "cameracore.h"
#include "cameradialog.h"
#include "terminalwidget.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>
#include <functional>

namespace {
QByteArray read(const QString &name) { QFile file(name); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool write(const QString &name, const QByteArray &bytes) {
    QFile file(name); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QJsonObject json(const QString &name) { return QJsonDocument::fromJson(read(name)).object(); }
bool writeJson(const QString &name, const QJsonObject &object) { return write(name, QJsonDocument(object).toJson()); }

// Test-only fixture: never linked into the executable or offered as a real photo.
class Images final : public CameraImageProvider {
public:
    int submitted = 0, queried = 0, retried = 0, recovered = 0;
    QString firstState = "queued";
    QMap<QString, QJsonObject> snapshots;
    QMap<QString, CameraImageTask> tasks;
    std::function<void(const QJsonObject &)> afterSubmit;
    CameraImageTask submit(const QJsonObject &snapshot) override {
        const QString id = snapshot.value("capture_id").toString();
        if (snapshots.contains(id)) {
            if (snapshots.value(id) != snapshot) return {{}, "uncertain", {}, "Snapshot conflict."};
            return tasks.value(id);
        }
        ++submitted; snapshots.insert(id, snapshot);
        tasks.insert(id, {"test-job-" + id, firstState, {}, "Test image provider"});
        if (afterSubmit) afterSubmit(snapshot);
        return tasks.value(id);
    }
    CameraImageTask query(const QString &taskId) override {
        ++queried;
        const QString id = taskId.mid(9);
        auto task = tasks.value(id);
        task.state = "completed"; task.resourceId = "test-image-" + id;
        tasks.insert(id, task); return task;
    }
    CameraImageTask recover(const QString &captureId) override {
        ++recovered;
        return tasks.value(captureId, {{}, "uncertain", {}, "No confirmed job."});
    }
    CameraImageTask retry(const QString &taskId, const QJsonObject &snapshot) override {
        ++retried;
        const QString id = snapshot.value("capture_id").toString();
        if (snapshots.value(id) != snapshot) return {taskId, "uncertain", {}, "Snapshot conflict."};
        auto task = tasks.value(id); task.state = "queued"; tasks.insert(id, task); return task;
    }
    CameraImageData readImage(const QString &) override {
        QImage image(320, 180, QImage::Format_RGB32); image.fill(QColor("#586a75"));
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
        return {bytes, "png", {}};
    }
};
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SillyTerminalTests");
    QCoreApplication::setApplicationName("CameraTests");
    app.setStyle("Fusion");
    QTemporaryDir temp(QDir::current().filePath("camera-tests-XXXXXX"));
    if (!temp.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path() + "/settings");
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *name) {
        ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    auto images = std::make_shared<Images>();
    const QString root = temp.path() + "/network", scenePath = root + "/scene.json", devicePath = root + "/device.json";
    CameraCore camera(root, images);
    check(camera.initializationError().isEmpty() && QFileInfo::exists(scenePath) && QFileInfo::exists(devicePath), "target initializes as real isolated files");
    const auto originalScene = json(scenePath), originalDevice = json(devicePath);
    const QByteArray originalSceneBytes = read(scenePath);
    QString error;
    check(camera.captures(&error).isEmpty() && error.isEmpty() && images->submitted == 0, "opening and history do not take photographs");
    check(!camera.capture().error.isEmpty(), "photograph requires authentication");
    check(!camera.connectDevice("192.168.50.99", "viewer", "teacup-27", &error), "unknown virtual address fails without host networking");
    check(!camera.connectDevice("http://viewer:teacup-27@192.168.50.23", "viewer", "teacup-27", &error), "credentials in address are rejected");
    check(!camera.connectDevice("192.168.50.23", "viewer", "wrong", &error) && !camera.connected(), "wrong password does not create a session");
    check(camera.connectDevice("192.168.50.23", "monitor", "status-only", &error), "device has a distinct status-only account");
    check(!camera.deviceStatus(&error).isEmpty() && !camera.capture().error.isEmpty() && images->submitted == 0, "snapshot permission enforced before submitting");
    check(camera.connectDevice("http://192.168.50.23:80/", "viewer", "teacup-27", &error), "viewer authenticates at normalized address");
    check(!camera.deviceStatus(&error).contains("accounts") && images->submitted == 0, "status hides credentials and does not generate");
    for (const QString &flag : {QStringLiteral("online"), QStringLiteral("http_enabled"), QStringLiteral("privacy_mode")}) {
        auto device = originalDevice; device.insert(flag, flag == "privacy_mode"); writeJson(devicePath, device);
        check(!camera.capture().error.isEmpty() && images->submitted == 0, "live device conditions reject capture before image work");
    }
    writeJson(devicePath, originalDevice);
    images->firstState = "failed";
    const auto first = camera.capture();
    check(first.error.isEmpty() && first.task.state == "failed" && first.attempt == 1 && images->submitted == 1, "explicit photograph persists confirmed image failure");
    const auto firstSnapshot = first.snapshot;
    const QByteArray submitted = QJsonDocument(firstSnapshot).toJson();
    check(submitted.contains("Floor lamp is on") && !submitted.contains("password") && !submitted.contains("teacup-27"), "frozen image context contains only visible facts");
    check(!camera.deviceStatus(&error).isEmpty(), "image failure does not disconnect camera");
    auto changed = originalScene;
    changed.insert("state", QJsonObject{{"door", "open"}, {"curtains", "closed"}, {"lamp", "off"}});
    changed.insert("world_time", "2026-10-20T19:43:00+08:00");
    auto view = changed.value("view").toObject(); view.insert("private_note", "secret-view-note"); changed.insert("view", view);
    changed.insert("people", QJsonArray{
        QJsonObject{{"id", "resident"}, {"description", "a person in a green jacket"}, {"location", "beside the door"}, {"action", "standing"}, {"visible", true}},
        QJsonObject{{"id", "hidden"}, {"description", "secret-hidden-person"}, {"location", "outside the room"}, {"action", "waiting"}, {"visible", false}}});
    check(writeJson(scenePath, changed), "external editor changes real room state");
    images->firstState = "queued";
    const auto second = camera.capture();
    const auto secondJson = QJsonDocument(second.snapshot).toJson();
    check(second.error.isEmpty() && second.id != first.id && secondJson.contains("Floor lamp is off")
        && secondJson.contains("Room door is open") && secondJson.contains("green jacket")
        && !secondJson.contains("secret-hidden-person") && !secondJson.contains("secret-view-note"),
        "next photograph refreshes edited scene and excludes hidden entities");
    check(camera.readCapture(first.id).snapshot == firstSnapshot, "external scene edit leaves old snapshot unchanged");
    const auto retried = camera.retry(first.id);
    check(retried.error.isEmpty() && retried.attempt == 2 && images->retried == 1 && retried.snapshot == firstSnapshot,
        "retry retains original world time and visible scene");
    const auto completed = camera.refresh(first.id);
    check(completed.task.state == "completed" && camera.image(first.id).format == "png", "completed resource can be read through provider");
    const int queries = images->queried;
    camera.refresh(first.id); camera.readCapture(first.id); camera.image(first.id);
    check(images->submitted == 2 && images->queried == queries, "opening completed photograph never regenerates or queries it");
    check(!camera.retry(first.id).error.isEmpty(), "completed image cannot be resubmitted through retry");
    const int submittedBeforeDuplicate = images->submitted;
    images->submit(firstSnapshot);
    auto duplicateConflict = firstSnapshot; duplicateConflict.insert("world_time", "different");
    check(images->submitted == submittedBeforeDuplicate && images->submit(duplicateConflict).state == "uncertain",
        "test adapter exercises idempotency and conflicting snapshot contract");
    CameraCore reloaded(root, images);
    check(reloaded.captures(&error).size() == 2 && !reloaded.connected()
        && reloaded.readCapture(first.id).snapshot == firstSnapshot && !reloaded.image(first.id).bytes.isEmpty(),
        "restart preserves photos and snapshots while requiring fresh device login");
    check(read(scenePath) != originalSceneBytes, "restart does not overwrite edited room state");
    check(reloaded.refresh(second.id).task.state == "completed" && images->submitted == 2, "pending task resumes by querying existing job after restart");
    auto accountChange = originalDevice;
    auto accounts = accountChange.value("accounts").toArray(); auto viewer = accounts[0].toObject();
    viewer.insert("can_capture", false); accounts[0] = viewer; accountChange.insert("accounts", accounts); writeJson(devicePath, accountChange);
    check(!camera.capture().error.isEmpty() && images->submitted == 2, "permission changes apply to an existing session");
    accountChange.insert("accounts", QJsonArray{}); writeJson(devicePath, accountChange);
    check(camera.deviceStatus(&error).isEmpty() && error.contains("account changed"), "deleted remote account invalidates session");
    writeJson(devicePath, originalDevice);
    check(write(scenePath, "{ broken"), "create malformed external room file");
    check(!camera.capture().error.isEmpty() && read(scenePath) == "{ broken" && images->submitted == 2, "invalid scene is reported and preserved");
    QFile::remove(scenePath);
    CameraCore missingScene(root, images);
    check(!QFileInfo::exists(scenePath) && missingScene.connectDevice("192.168.50.23", "viewer", "teacup-27", &error)
        && !missingScene.capture().error.isEmpty(), "deleted scene is not silently restored at restart");
    writeJson(scenePath, changed);
    check(!camera.readCapture("../../outside").error.isEmpty(), "capture identifiers cannot escape storage");
    const QString firstPath = root + "/captures/" + first.id + ".json";
    auto tampered = json(firstPath); auto snapshot = tampered.value("snapshot").toObject(); snapshot.insert("world_time", "changed");
    tampered.insert("snapshot", snapshot); writeJson(firstPath, tampered);
    check(!camera.readCapture(first.id).error.isEmpty() && json(firstPath) == tampered, "modified frozen snapshot is rejected without overwriting file");
    const auto remaining = camera.captures(&error);
    check(remaining.size() == 1 && !error.isEmpty(), "bad history record is reported while valid photographs remain available");

    // The image adapter edits a record between submission and acknowledgement.
    images->afterSubmit = [&](const QJsonObject &sent) {
        const QString file = root + "/captures/" + sent.value("capture_id").toString() + ".json";
        auto object = json(file); object.insert("external_note", "keep this edit"); writeJson(file, object);
    };
    const auto conflict = camera.capture();
    check(!conflict.error.isEmpty() && conflict.error.contains("changed externally"), "in-flight external record edit prevents lost update");
    images->afterSubmit = {};
    const int submittedBeforeRecovery = images->submitted;
    const auto recovered = camera.refresh(conflict.id);
    check(recovered.error.isEmpty() && recovered.task.state == "queued" && images->recovered == 1
        && images->submitted == submittedBeforeRecovery, "uncertain submission recovers by capture id without blind resubmit");
    check(json(root + "/captures/" + conflict.id + ".json").value("external_note") == "keep this edit", "unknown external metadata survives later updates");
    check(!camera.retry(conflict.id).error.isEmpty(), "uncertain or active photograph cannot be blindly retried");

    // File links and junctions must not redirect target state outside its boundary.
    const QString outside = temp.path() + "/outside.json";
    write(outside, originalSceneBytes); QFile::remove(scenePath);
#ifdef Q_OS_WIN
    const QString linked = root + "/scene.json.lnk";
#else
    const QString linked = scenePath;
#endif
    if (QFile::link(outside, linked)) {
#ifdef Q_OS_WIN
        QFile::rename(linked, scenePath);
#endif
        check(!camera.capture().error.isEmpty() && read(outside) == originalSceneBytes, "linked scene cannot read or overwrite files outside target");
        QFile::remove(scenePath);
    }
    writeJson(scenePath, changed);

    CameraCore unavailable(temp.path() + "/unavailable");
    unavailable.connectDevice("192.168.50.23", "viewer", "teacup-27", &error);
    const auto failed = unavailable.capture();
    check(failed.error.isEmpty() && failed.task.state == "failed" && failed.task.resourceId.isEmpty()
        && failed.task.message.contains("not available") && !unavailable.deviceStatus(&error).isEmpty(),
        "production default reports image service unavailable without fake successful image");
    const QString preparedPath = temp.path() + "/unavailable/captures/" + failed.id + ".json";
    auto prepared = json(preparedPath); prepared.insert("attempt", 0);
    prepared.insert("task", QJsonObject{{"id", ""}, {"state", "prepared"}, {"resource_id", ""}, {"message", ""}});
    writeJson(preparedPath, prepared);
    check(unavailable.refresh(failed.id).task.state == "prepared" && json(preparedPath) == prepared,
        "opening a never-submitted record is read-only");
    check(unavailable.retry(failed.id).task.state == "failed" && unavailable.readCapture(failed.id).attempt == 1,
        "explicit submission can resume a prepared record after restart");

    TerminalCore terminal(temp.path() + "/client/world");
    check(terminal.execute("camera 192.168.50.23").openCamera && terminal.complete("camer").replacement == "camera ", "camera command and command completion exist");
    check(!terminal.execute("camera --help").openCamera && terminal.execute("camera --help").text.contains("Usage:"), "camera help does not launch or connect");
    check(terminal.execute("camera a b").error && terminal.execute("camera --bad").error, "unsupported camera arguments rejected");
    check(terminal.execute("camera > must-not-exist").error && terminal.execute("echo x | camera").error
        && !QFileInfo::exists(terminal.worldRoot() + "/home/guest/must-not-exist"), "interactive command rejects pipelines and redirection before filesystem changes");
    const auto data = images->readImage("test");
    check(!terminal.saveNewBinaryFile("photo.png", data.bytes).error
        && read(terminal.worldRoot() + "/home/guest/photo.png") == data.bytes, "image export creates actual binary world file");
    check(terminal.saveNewBinaryFile("photo.png", "replacement").error
        && read(terminal.worldRoot() + "/home/guest/photo.png") == data.bytes, "export preserves existing external files");
    check(terminal.saveNewBinaryFile("absent/image.png", data.bytes).error
        && terminal.saveNewBinaryFile("C:/outside.png", data.bytes).error, "image export uses normal bounded filesystem rules");

    auto uiImages = std::make_shared<Images>();
    CameraDialog dialog(terminal, "192.168.50.23", nullptr, uiImages);
    dialog.show(); app.processEvents();
    auto *user = dialog.findChild<QLineEdit *>("cameraUser");
    auto *password = dialog.findChild<QLineEdit *>("cameraPassword");
    auto *connectButton = dialog.findChild<QPushButton *>("cameraConnect");
    auto *captureButton = dialog.findChild<QPushButton *>("cameraCapture");
    auto *history = dialog.findChild<QTreeWidget *>("cameraHistory");
    check(user && password && connectButton && captureButton && !captureButton->isEnabled() && uiImages->submitted == 0, "client opens disconnected without generating");
    user->setText("viewer"); password->setText("teacup-27"); connectButton->click();
    check(captureButton->isEnabled() && password->text().isEmpty() && uiImages->submitted == 0, "UI sign-in clears password and waits for explicit snapshot");
    captureButton->click();
    check(uiImages->submitted == 1 && history->topLevelItemCount() == 1
        && history->currentItem()->text(2) == "queued", "explicit snapshot shows asynchronous task in history");
    QTimer::singleShot(1000, &dialog, &QDialog::accept); dialog.exec();
    check(dialog.findChild<QPushButton *>("cameraSave")->isEnabled()
        && !dialog.findChild<QLabel *>("cameraPreview")->pixmap().isNull(), "client polls result and displays test resource");
    if (app.arguments().contains("--preview")) {
        const int index = app.arguments().indexOf("--preview");
        if (index + 1 < app.arguments().size()) {
            dialog.show(); app.processEvents();
            check(dialog.grab().save(app.arguments().at(index + 1)), "save isolated camera UI preview");
            dialog.close();
        }
    }
    bool launched = false;
    TerminalWidget widget(nullptr, temp.path() + "/terminal-ui/world");
    QTimer::singleShot(0, &widget, [&] {
        auto *active = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (active && active->objectName() == "cameraDialog") { launched = true; active->accept(); }
    });
    widget.runCommand("camera 192.168.50.23");
    check(launched && widget.toPlainText().endsWith("$ "), "terminal launches camera and restores input on close");
    std::printf("Camera checks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
