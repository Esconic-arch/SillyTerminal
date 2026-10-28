#include "aicameraimageprovider.h"
#include "aicameraprofile.h"
#include "cameracore.h"
#include <QGuiApplication>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <cstdio>
#include <functional>

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool write(const QString &path, const QByteArray &bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QJsonObject object(const QString &path) { return QJsonDocument::fromJson(read(path)).object(); }
bool writeObject(const QString &path, const QJsonObject &value) { return write(path, QJsonDocument(value).toJson()); }
bool until(const std::function<bool()> &predicate, int timeout = 5000) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < timeout) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
    return predicate();
}
QByteArray png(const QColor &color, int width = 64, int height = 36) {
    QImage image(width, height, QImage::Format_RGB32); image.fill(color);
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return bytes;
}
QJsonObject snapshot(const QString &id = newId()) {
    return {{"schema_version", 1}, {"capture_id", id}, {"camera_id", "home-camera"}, {"world_time", "2026-10-20T19:42:00+08:00"},
        {"scene", QJsonObject{{"id", "living-room"}, {"version", "1"},
            {"view", QJsonObject{{"id", "fixed"}, {"description", "A fixed camera, with a sofa in the center."}, {"width", 320}, {"height", 180}}},
            {"visible_facts", QJsonArray{QJsonObject{{"id", "lamp"}, {"description", "Floor lamp is on."}},
                QJsonObject{{"id", "people-count"}, {"description", "Visible people: 0."}}}},
            {"constraints", QJsonArray{"Do not invent people or clues."}}, {"references", QJsonArray{}}}}};
}
QJsonObject profile() {
    const auto binding = [](const QString &input) { return QJsonObject{{"node", "1"}, {"input", input}}; };
    const QJsonObject generator{{"class_type", "TestGenerator"},
        {"inputs", QJsonObject{{"text", ""}, {"negative", ""}, {"width", 64}, {"height", 36}, {"seed", 1}}}};
    const QJsonObject output{{"class_type", "SaveImage"}, {"inputs", QJsonObject{{"images", QJsonArray{"1", 0}}}}};
    return {{"version", 1}, {"width", 64}, {"height", 36}, {"output_node", "9"}, {"instructions", "Use natural indoor camera colors."},
        {"bindings", QJsonObject{{"prompt", binding("text")}, {"negative_prompt", binding("negative")},
            {"width", binding("width")}, {"height", binding("height")}, {"seed", binding("seed")}}},
        {"workflow", QJsonObject{{"1", generator}, {"9", output}}}};
}
class Secrets final : public AiSecretStore {
public:
    QMap<QString, QString> values; int reads = 0;
    bool read(const QString &id, QString *secret, QString *) override { ++reads; *secret = values.value(id); return values.contains(id); }
    bool write(const QString &id, const QString &secret, QString *) override { values[id] = secret; return true; }
    bool remove(const QString &id, QString *) override { values.remove(id); return true; }
};
class Service : public QObject {
public:
    QTcpServer server;
    int texts = 0, submissions = 0, downloads = 0, queries = 0;
    int textStatus = 200, submitStatus = 200, imageStatus = 200;
    bool badText = false, stallText = false, hold = false, executionError = false, badHistory = false, reflectKey = false;
    QSet<QString> remote;
    QList<QJsonObject> textRequests, workflows;
    QByteArray textAuth, comfyAuth;
    QByteArray imageBytes = png(QColor("#667788"));
    Service() {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    const auto bytes = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", bytes);
                    const auto boundary = bytes.indexOf("\r\n\r\n");
                    if (boundary < 0 || socket->property("handled").toBool()) return;
                    const auto header = bytes.left(boundary);
                    const auto match = QRegularExpression("Content-Length: ([0-9]+)", QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(header));
                    const int length = match.hasMatch() ? match.captured(1).toInt() : 0;
                    if (bytes.size() < boundary + 4 + length) return;
                    socket->setProperty("handled", true);
                    const auto request = QJsonDocument::fromJson(bytes.mid(boundary + 4, length)).object();
                    const auto target = header.split(' ').value(1);
                    const auto respond = [socket](int status, const QByteArray &body) {
                        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Length: "
                            + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    };
                    if (target == "/v1/chat/completions") {
                        ++texts; textRequests.append(request); textAuth = header;
                        if (stallText) return;
                        if (textStatus != 200) { respond(textStatus, "reflected synthetic-secret"); return; }
                        const auto source = QJsonDocument::fromJson(request.value("messages").toArray().at(1).toObject().value("content").toString().toUtf8()).object();
                        QJsonArray ids;
                        for (const auto &fact : source.value("scene").toObject().value("visible_facts").toArray()) ids.append(fact.toObject().value("id"));
                        const QString description = QString::fromUtf8(QJsonDocument(QJsonObject{{"prompt", reflectKey ? "synthetic-secret" : "A fixed indoor camera photograph."},
                            {"negative_prompt", "extra people, readable text"}, {"used_fact_ids", ids}}).toJson(QJsonDocument::Compact));
                        respond(200, QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"},
                            {"message", QJsonObject{{"content", badText ? "not-json" : description}}}}}}}).toJson());
                    } else if (target == "/comfy/prompt") {
                        ++submissions; workflows.append(request.value("prompt").toObject()); comfyAuth = header;
                        const QString id = request.value("prompt_id").toString();
                        if (submitStatus >= 500 || submitStatus == 200) remote.insert(id);
                        if (submitStatus != 200) { respond(submitStatus, "reflected synthetic-proxy"); return; }
                        respond(200, QJsonDocument(QJsonObject{{"prompt_id", id}, {"node_errors", QJsonObject{}}}).toJson());
                    } else if (target.startsWith("/comfy/history/")) {
                        ++queries;
                        if (badHistory) { respond(200, "<html>login</html>"); return; }
                        const QString id = QString::fromUtf8(target.mid(QByteArray("/comfy/history/").size()));
                        QJsonObject result;
                        if (remote.contains(id) && !hold) result.insert(id, QJsonObject{
                            {"status", QJsonObject{{"completed", true}, {"status_str", executionError ? "error" : "success"}}},
                            {"outputs", QJsonObject{{"9", QJsonObject{{"images", QJsonArray{QJsonObject{{"filename", "camera.png"}, {"subfolder", ""}, {"type", "output"}}}}}}}}});
                        respond(200, QJsonDocument(result).toJson());
                    } else if (target == "/comfy/queue") {
                        QJsonArray waiting; for (const auto &id : remote) waiting.append(QJsonArray{0, id});
                        respond(200, QJsonDocument(QJsonObject{{"queue_running", waiting}, {"queue_pending", QJsonArray{}}}).toJson());
                    } else if (target.startsWith("/comfy/view?")) {
                        ++downloads; respond(imageStatus, imageBytes);
                    } else respond(404, "{}");
                });
            }
        });
    }
    QString url() const { return "http://127.0.0.1:" + QString::number(server.serverPort()); }
    void reset() { textStatus = submitStatus = imageStatus = 200; badText = stallText = hold = executionError = badHistory = reflectKey = false; imageBytes = png(QColor("#667788")); }
};
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SillyTerminalTests"); QCoreApplication::setApplicationName("AiCamera");
    QTemporaryDir temp(QDir::current().filePath("ai-camera-XXXXXX"));
    if (!temp.isValid()) return 2;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *name) { ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); } };
    Service service; check(service.server.isListening(), "local test services listen on loopback only");
    auto secrets = std::make_shared<Secrets>(); const QString profilePath = temp.path() + "/profile.json";
    writeObject(profilePath, profile());
    AiSettingsStore store(temp.path() + "/services.json", secrets); AiServiceSettings config; QString error;
    config.deepSeekUrl = service.url() + "/v1"; config.comfyUiUrl = service.url() + "/comfy";
    config.deepSeekModel = "fixture-model"; config.cameraProfilePath = profilePath; config.timeoutSeconds = 1;
    check(store.save(&config, "synthetic-secret", "synthetic-proxy", &error), "save service configuration using synthetic credentials");
    const AiCameraOptions options{5, 400, 3000};
    const auto waitState = [&](AiCameraImageProvider &provider, const QString &id, const QString &state) {
        return until([&] { return provider.query(id).state == state; });
    };
    const QString root = temp.path() + "/runtime/ai";
    auto provider = std::make_shared<AiCameraImageProvider>(root, store, options);
    check(!QFileInfo::exists(root) && secrets->reads == 0 && service.texts == 0, "construction performs no credential or service access");
    auto invalid = snapshot("../outside");
    check(provider->submit(invalid).state == "failed" && !QFileInfo::exists(root), "invalid snapshot identifiers cannot create files");
    const auto first = snapshot(); const QString firstId = first.value("capture_id").toString();
    const auto submitted = provider->submit(first);
    check(submitted.id == firstId && submitted.state == "queued" && service.texts == 0,
        "submission promptly returns a durable local task before external I/O");
    check(provider->submit(first).id == firstId, "same capture and snapshot resolve to the existing task");
    auto conflict = first; conflict.insert("world_time", "different");
    check(provider->submit(conflict).state == "failed", "same capture with different facts is rejected");
    check(waitState(*provider, firstId, "completed") && service.texts == 1 && service.submissions == 1,
        "DeepSeek to ComfyUI pipeline completes once for duplicate submissions");
    const auto resource = provider->readImage(firstId); QImage decoded; decoded.loadFromData(resource.bytes);
    check(resource.error.isEmpty() && resource.format == "png" && decoded.size() == QSize(320, 180),
        "saved resource is a real PNG at the requested camera output size");
    const auto inputs = service.workflows.first().value("1").toObject().value("inputs").toObject();
    check(inputs.value("width").toInt() == 64 && inputs.value("height").toInt() == 36
        && inputs.value("text").toString().contains("Floor lamp is on.")
        && inputs.value("text").toString().contains("Do not invent people"),
        "explicit workflow bindings preserve fixed scene facts and constraints");
    check(service.textAuth.contains("Authorization: Bearer synthetic-secret")
        && service.comfyAuth.contains("Authorization: Bearer synthetic-proxy"), "credentials are scoped to the two configured service requests");
    const QString firstJob = root + "/jobs/" + firstId + ".json";
    const auto savedJob = read(firstJob);
    check(!savedJob.contains("synthetic-secret") && !savedJob.contains("synthetic-proxy")
        && object(firstJob).value("snapshot").toObject() == first, "job record persists frozen inputs without service secrets");
    const int oldTexts = service.texts, oldSubmits = service.submissions;
    provider.reset(); provider = std::make_shared<AiCameraImageProvider>(root, store, options);
    check(provider->recover(firstId).state == "completed" && !provider->readImage(firstId).bytes.isEmpty()
        && service.texts == oldTexts && service.submissions == oldSubmits, "completed photos survive provider restart without regeneration");
    const QString resourcePath = root + "/resources/" + firstId + ".png";
    const auto editedImage = png(Qt::blue, 320, 180); write(resourcePath, editedImage);
    check(provider->readImage(firstId).bytes == editedImage, "valid image edits on disk are observed on the next read");
    write(resourcePath, "invalid image");
    check(!provider->readImage(firstId).error.isEmpty() && read(resourcePath) == "invalid image", "invalid external image edits are preserved and reported");
    QFile::remove(resourcePath);
    check(provider->query(firstId).state == "completed" && !provider->readImage(firstId).error.isEmpty()
        && service.submissions == oldSubmits, "missing image is reported without generating a replacement");

    service.submitStatus = 503;
    const auto lostAck = provider->submit(snapshot());
    check(waitState(*provider, lostAck.id, "uncertain"), "unconfirmed ComfyUI submission becomes uncertain");
    const int lostCount = service.submissions; service.submitStatus = 200;
    check(waitState(*provider, lostAck.id, "completed") && service.submissions == lostCount,
        "uncertain submission recovers by the persisted remote ID without resubmission");

    service.hold = true;
    const auto running = provider->submit(snapshot());
    check(until([&] { return !object(root + "/jobs/" + running.id + ".json").value("comfy_prompt_id").toString().isEmpty(); }),
        "remote ID is durably written before submission acknowledgement");
    check(until([&] { return object(root + "/jobs/" + running.id + ".json").value("stage") == "waiting"; }), "job waits in the actual ComfyUI queue");
    const auto workflowBefore = service.workflows.last(); const int beforeRetryTexts = service.texts;
    auto editedProfile = profile(); editedProfile.insert("instructions", "NEW CAPTURES ONLY"); writeObject(profilePath, editedProfile);
    service.executionError = true; service.hold = false;
    check(waitState(*provider, running.id, "failed"), "confirmed ComfyUI execution error is retryable");
    const auto originalSnapshot = object(root + "/jobs/" + running.id + ".json").value("snapshot").toObject();
    service.executionError = false;
    check(provider->retry(running.id, originalSnapshot).state == "queued" && waitState(*provider, running.id, "completed")
        && service.texts == beforeRetryTexts && service.workflows.last() == workflowBefore,
        "image retry reuses the original description, profile, seed and workflow despite external profile edits");
    check(provider->retry(running.id, conflict).state == "uncertain", "retry cannot silently switch to a different snapshot");

    service.hold = true;
    const auto restart = provider->submit(snapshot());
    check(until([&] { return object(root + "/jobs/" + restart.id + ".json").value("stage") == "waiting"; }), "prepare active remote task for restart recovery");
    const int beforeRestartPosts = service.submissions; provider.reset(); service.hold = false;
    provider = std::make_shared<AiCameraImageProvider>(root, store, options);
    check(waitState(*provider, restart.id, "completed") && service.submissions == beforeRestartPosts,
        "restart resumes an acknowledged remote task using GET requests only");

    service.stallText = true;
    auto interruptedProvider = std::make_unique<AiCameraImageProvider>(temp.path() + "/interrupted/ai", store, options);
    const auto interrupted = interruptedProvider->submit(snapshot());
    const int beforeInterrupt = service.texts;
    check(until([&] { return service.texts > beforeInterrupt; }), "begin a text request before simulated application exit");
    interruptedProvider.reset(); service.stallText = false; const int interruptedCalls = service.texts;
    interruptedProvider = std::make_unique<AiCameraImageProvider>(temp.path() + "/interrupted/ai", store, options);
    check(waitState(*interruptedProvider, interrupted.id, "failed") && service.texts == interruptedCalls,
        "restart does not silently repeat an interrupted text request");

    service.imageStatus = 503;
    const auto failedDownload = provider->submit(snapshot());
    check(waitState(*provider, failedDownload.id, "failed"), "image retrieval failure is retained as a task failure");
    const int downloadPosts = service.submissions, downloadTexts = service.texts;
    const auto downloadSnapshot = object(root + "/jobs/" + failedDownload.id + ".json").value("snapshot").toObject();
    service.imageStatus = 200;
    provider->retry(failedDownload.id, downloadSnapshot);
    check(waitState(*provider, failedDownload.id, "completed") && service.submissions == downloadPosts && service.texts == downloadTexts,
        "retrying image retrieval repeats only the GET, not text or image generation");

    service.badText = true;
    const int beforeBadText = service.submissions; const auto badText = provider->submit(snapshot());
    check(waitState(*provider, badText.id, "failed") && service.submissions == beforeBadText, "malformed DeepSeek content cannot reach ComfyUI");
    service.badText = false; service.reflectKey = true;
    const auto leaked = provider->submit(snapshot());
    check(waitState(*provider, leaked.id, "failed") && !read(root + "/jobs/" + leaked.id + ".json").contains("synthetic-secret"),
        "reflected service credentials in generated text are discarded before saving");
    service.reset();
    service.textStatus = 401;
    const int beforeAuthentication = service.submissions;
    const auto rejected = provider->submit(snapshot());
    check(waitState(*provider, rejected.id, "failed") && service.submissions == beforeAuthentication
        && !provider->query(rejected.id).message.contains("synthetic-secret"),
        "rejected text authentication reports a safe error without submitting an image");
    service.reset();

    const auto queuedA = provider->submit(snapshot()), queuedB = provider->submit(snapshot());
    const QString queuedPath = root + "/jobs/" + queuedB.id + ".json";
    const auto frozenQueuedProfile = object(queuedPath).value("profile").toObject();
    auto newerProfile = profile(); newerProfile.insert("instructions", "CHANGED AFTER BOTH CAPTURES"); writeObject(profilePath, newerProfile);
    check(waitState(*provider, queuedA.id, "completed") && waitState(*provider, queuedB.id, "completed")
        && object(queuedPath).value("profile").toObject() == frozenQueuedProfile
        && !service.textRequests.last().value("messages").toArray().first().toObject().value("content").toString().contains("CHANGED AFTER BOTH CAPTURES"),
        "queued captures freeze their selected profile immediately, before either background job starts");

    service.badHistory = true;
    const auto unavailableHistory = provider->submit(snapshot());
    check(waitState(*provider, unavailableHistory.id, "uncertain"), "invalid task history preserves uncertainty instead of reporting a generation failure");
    const int historySubmissions = service.submissions; service.badHistory = false;
    check(waitState(*provider, unavailableHistory.id, "completed") && service.submissions == historySubmissions,
        "status recovery after an invalid history response does not submit again");

    const int beforeSetup = service.texts;
    auto refSnapshot = snapshot(); auto scene = refSnapshot.value("scene").toObject();
    scene.insert("references", QJsonArray{QJsonObject{{"id", "base-room"}, {"version", "1"}}}); refSnapshot.insert("scene", scene);
    const auto referenceTask = provider->submit(refSnapshot);
    check(waitState(*provider, referenceTask.id, "failed") && service.texts == beforeSetup,
        "unsupported reference snapshots fail before a paid text request rather than ignoring references");

    service.hold = true;
    const auto externallyEdited = provider->submit(snapshot());
    const QString editPath = root + "/jobs/" + externallyEdited.id + ".json";
    check(until([&] { return object(editPath).value("stage") == "waiting"; }), "prepare pending job for external edit conflict");
    auto changed = object(editPath); changed.insert("player_note", "external edit"); writeObject(editPath, changed); const auto changedBytes = read(editPath);
    check(waitState(*provider, externallyEdited.id, "uncertain") && read(editPath) == changedBytes,
        "in-flight task updates detect external edits and preserve the entire edited file");
    provider.reset(); service.hold = false;
    provider = std::make_shared<AiCameraImageProvider>(root, store, options);
    check(waitState(*provider, externallyEdited.id, "completed") && object(editPath).value("player_note") == "external edit",
        "fresh recovery preserves unknown external fields while completing an existing task");

    auto cameraProvider = std::make_shared<AiCameraImageProvider>(temp.path() + "/integration/ai", store, options);
    CameraCore camera(temp.path() + "/integration/network", cameraProvider);
    check(camera.connectDevice("192.168.50.23", "viewer", "teacup-27", &error), "real camera core authenticates against its isolated fixture files");
    const auto capture = camera.capture(); CameraCapture finalCapture;
    check(until([&] { finalCapture = camera.refresh(capture.id); return finalCapture.task.state == "completed"; })
        && camera.image(capture.id).error.isEmpty(), "camera capture, provider HTTP chain and image retrieval work together");

    QFile example(":/ai/camera-profile.example.json");
    check(example.open(QIODevice::ReadOnly), "example profile is bundled in the executable's resources");
    auto exampleObject = QJsonDocument::fromJson(example.readAll()).object();
    check(!AiCameraProfile::validate(exampleObject, &error), "unconfigured example checkpoint is detected before service calls");
    auto invalidBindings = profile(); auto bindings = invalidBindings.value("bindings").toObject();
    bindings.insert("prompt", QJsonObject{{"node", "9"}, {"input", "images"}}); invalidBindings.insert("bindings", bindings);
    check(!AiCameraProfile::validate(invalidBindings, &error), "bindings cannot overwrite node connections with model output");
    std::printf("AI camera checks: %d, failures: %d\n", checks, failures);
    return failures ? 1 : 0;
}
