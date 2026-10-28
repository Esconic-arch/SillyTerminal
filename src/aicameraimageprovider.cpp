#include "aicameraimageprovider.h"
#include "aicameraprofile.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkReply>
#include <QQueue>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QUrlQuery>
#include <QUuid>

namespace {
constexpr qint64 jobLimit = 8 * 1024 * 1024, imageLimit = 20 * 1024 * 1024;
QString hash(const QJsonObject &object) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
bool uuid(const QString &id) { return !QUuid(id).isNull() && QUuid(id).toString(QUuid::WithoutBraces) == id; }
bool link(const QFileInfo &info) { return info.isSymLink() || info.isJunction(); }
bool imageDescriptor(const QJsonObject &image) {
    const QString filename = image.value("filename").toString(), folder = image.value("subfolder").toString();
    return !filename.isEmpty() && filename.size() <= 1024 && !filename.contains('/') && !filename.contains('\\')
        && !filename.contains(':') && !filename.contains(QChar::Null) && filename != "." && filename != ".."
        && folder.size() <= 2048 && !folder.contains('\\') && !folder.startsWith('/') && !folder.contains(':')
        && !folder.contains(QChar::Null) && !folder.split('/').contains("..")
        && (image.value("type") == "output" || image.value("type") == "temp");
}
bool sizeValue(const QJsonValue &value) {
    return value.isDouble() && value.toInt() >= 16 && value.toInt() <= 4096 && value.toInt() == value.toDouble();
}
bool validSnapshot(const QJsonObject &snapshot) {
    const auto scene = snapshot.value("scene").toObject(), view = scene.value("view").toObject();
    if (snapshot.value("schema_version").toInt() != 1 || !uuid(snapshot.value("capture_id").toString())
        || snapshot.value("camera_id").toString().isEmpty() || snapshot.value("world_time").toString().isEmpty()
        || scene.value("id").toString().isEmpty() || scene.value("version").toString().isEmpty()
        || view.value("description").toString().isEmpty() || !sizeValue(view.value("width")) || !sizeValue(view.value("height"))
        || !scene.value("visible_facts").isArray() || !scene.value("constraints").isArray() || !scene.value("references").isArray()
        || QJsonDocument(snapshot).toJson(QJsonDocument::Compact).size() > 1024 * 1024) return false;
    QSet<QString> ids;
    const auto facts = scene.value("visible_facts").toArray();
    if (facts.isEmpty() || facts.size() > 256) return false;
    for (const auto &value : facts) {
        const auto fact = value.toObject(); const QString id = fact.value("id").toString();
        if (id.isEmpty() || id.size() > 256 || ids.contains(id) || !fact.value("description").isString()
            || fact.value("description").toString().size() > 8000) return false;
        ids.insert(id);
    }
    for (const auto &constraint : scene.value("constraints").toArray()) if (!constraint.isString()) return false;
    return true;
}
CameraImageTask task(const QJsonObject &job) {
    return {job.value("id").toString(), job.value("state").toString(),
        job.value("resource_id").toString(), job.value("message").toString()};
}
CameraImageTask unknown(const QString &id, const QString &message) { return {id, "uncertain", {}, message}; }
QJsonObject parseObject(const QByteArray &bytes, bool *valid = nullptr) {
    QJsonParseError error; const auto doc = QJsonDocument::fromJson(bytes, &error);
    if (valid) *valid = error.error == QJsonParseError::NoError && doc.isObject();
    return error.error == QJsonParseError::NoError && doc.isObject() ? doc.object() : QJsonObject{};
}
bool decode(const QByteArray &bytes, QImage *image, QString *format) {
    if (bytes.isEmpty() || bytes.size() > imageLimit) return false;
    QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer); const auto type = reader.format().toLower(); const auto size = reader.size();
    if ((type != "png" && type != "jpeg" && type != "jpg") || !size.isValid()
        || size.width() < 16 || size.height() < 16 || size.width() > 4096 || size.height() > 4096) return false;
    *image = reader.read(); *format = type == "png" ? "png" : "jpeg";
    return !image->isNull();
}
}

class AiCameraImageProvider::Impl : public QObject {
public:
    QString directory;
    AiSettingsStore settings;
    AiCameraOptions options;
    QNetworkAccessManager network;
    QNetworkReply *reply = nullptr;
    QTimer timer;
    QQueue<QPair<QString, bool>> pending;
    QString active;
    QJsonObject job;
    QByteArray expected;
    std::unique_ptr<QLockFile> lock;
    AiServiceSettings config;
    QString deepKey, comfyKey;
    QMap<QString, QString> conflicts;

    Impl(const QString &path, const AiSettingsStore &store, const AiCameraOptions &opts)
        : directory(QFileInfo(path).absoluteFilePath()), settings(store), options(opts) { timer.setSingleShot(true); }
    ~Impl() override {
        timer.stop();
        if (reply) { reply->disconnect(this); reply->abort(); }
    }

    bool safe(bool create = false) {
        // The runtime parent and all managed children remain real directories.
        for (const QString &path : {QFileInfo(directory).absolutePath(), directory, directory + "/jobs", directory + "/resources"}) {
            if (link(QFileInfo(path))) return false;
            if (create && !QDir().mkpath(path)) return false;
            if (QFileInfo::exists(path) && !QFileInfo(path).isDir()) return false;
        }
        return true;
    }
    QString jobPath(const QString &id) const { return directory + "/jobs/" + id + ".json"; }
    QString imagePath(const QString &id) const { return directory + "/resources/" + id + ".png"; }
    QJsonObject read(const QString &id, QByteArray *bytes, QString *error) {
        if (!uuid(id) || !safe() || link(QFileInfo(jobPath(id)))) { *error = "Invalid AI job path or linked runtime file."; return {}; }
        QFile file(jobPath(id));
        if (!file.open(QIODevice::ReadOnly) || file.size() > jobLimit) { *error = "AI task record is missing, unreadable or too large."; return {}; }
        *bytes = file.readAll(); bool valid;
        auto object = parseObject(*bytes, &valid);
        const QSet<QString> states{"queued", "generating", "completed", "failed", "uncertain"};
        if (!valid || object.value("version").toInt() != 1 || object.value("id") != id
            || !states.contains(object.value("state").toString()) || object.value("message").toString().size() > 2048
            || !validSnapshot(object.value("snapshot").toObject())
            || object.value("snapshot").toObject().value("capture_id") != id
            || object.value("snapshot_sha256").toString() != hash(object.value("snapshot").toObject())
            || (!object.value("profile").toObject().isEmpty() && object.value("profile_sha256").toString() != hash(object.value("profile").toObject()))
            || (!object.value("workflow").toObject().isEmpty() && object.value("workflow_sha256").toString() != hash(object.value("workflow").toObject()))
            || (!object.value("comfy_prompt_id").toString().isEmpty() && !uuid(object.value("comfy_prompt_id").toString()))
            || (!object.value("image").toObject().isEmpty() && !imageDescriptor(object.value("image").toObject()))
            || (object.value("state") == "completed" && object.value("resource_id") != id)) {
            *error = "AI task record is invalid or was changed inconsistently. Existing files were preserved."; return {};
        }
        QString profileError;
        if (!object.value("profile").toObject().isEmpty()
            && !AiCameraProfile::validate(object.value("profile").toObject(), &profileError)) {
            *error = "The task's frozen camera profile is invalid. Existing content was preserved."; return {};
        }
        return object;
    }
    bool write(const QJsonObject &object, const QByteArray &before) {
        const QString id = object.value("id").toString(), path = jobPath(id);
        if (!safe() || link(QFileInfo(path))) return false;
        const auto unchanged = [&] {
            if (before.isEmpty()) return !QFileInfo::exists(path) && !link(QFileInfo(path));
            QFile input(path); return !link(QFileInfo(path)) && input.open(QIODevice::ReadOnly)
                && input.size() <= jobLimit && input.readAll() == before;
        };
        if (!unchanged()) return false;
        QSaveFile output(path); output.setDirectWriteFallback(false);
        const auto data = QJsonDocument(object).toJson();
        return data.size() <= jobLimit && output.open(QIODevice::WriteOnly) && output.write(data) == data.size()
            && unchanged() && output.commit();
    }
    bool persist() {
        if (!write(job, expected)) {
            conflicts.insert(active, "AI task changed externally or could not be saved. Reopen the application before recovering this record.");
            stop(); return false;
        }
        expected = QJsonDocument(job).toJson(); return true;
    }
    bool state(const QString &value, const QString &message, const QString &stage = {}) {
        job.insert("state", value); job.insert("message", message);
        if (!stage.isEmpty()) job.insert("stage", stage);
        return persist();
    }
    void fail(const QString &message, const QString &stage = {}) {
        if (state("failed", message, stage)) stop();
    }
    void uncertain(const QString &message) { if (state("uncertain", message)) stop(); }
    void stop() {
        timer.stop(); active.clear(); job = {}; expected.clear(); lock.reset(); deepKey.clear(); comfyKey.clear();
        QTimer::singleShot(0, this, [this] { pump(); });
    }
    void enqueue(const QString &id, bool allowPost) {
        if (id == active) return;
        for (const auto &entry : pending) if (entry.first == id) return;
        pending.enqueue({id, allowPost});
        QTimer::singleShot(0, this, [this] { pump(); });
    }
    CameraImageTask lookup(const QString &id) {
        QByteArray bytes; QString error; const auto object = read(id, &bytes, &error);
        if (!error.isEmpty()) return unknown(id, error);
        // Keep the failed writer stopped. A new provider/application instance
        // reads and validates the externally edited record before recovery.
        if (conflicts.contains(id)) return unknown(id, conflicts.value(id));
        const QString status = object.value("state").toString();
        if (status == "queued" || status == "generating" || status == "uncertain") enqueue(id, false);
        return task(object);
    }
    bool freezeProfile(QJsonObject *record, QString *error) {
        AiServiceSettings current;
        QJsonObject profile, ignored;
        QUrl ds, comfy;
        if (!settings.load(&current, error)
            || !AiConnectionClient::normalizeBaseUrl(current.deepSeekUrl, &ds, error)
            || !AiConnectionClient::normalizeBaseUrl(current.comfyUiUrl, &comfy, error)
            || !AiCameraProfile::load(current.cameraProfilePath, &profile, error)
            || !AiCameraProfile::bind(profile, record->value("snapshot").toObject(), {}, 1, &ignored, error)) return false;
        record->insert("profile", profile); record->insert("profile_sha256", hash(profile));
        record->insert("deepseek_url", ds.toString()); record->insert("deepseek_model", current.deepSeekModel);
        record->insert("comfyui_url", comfy.toString());
        return true;
    }
    CameraImageTask submit(const QJsonObject &snapshot) {
        const QString id = snapshot.value("capture_id").toString();
        if (!validSnapshot(snapshot)) return {{}, "failed", {}, "Invalid camera snapshot."};
        if (!safe(true)) return {{}, "failed", {}, "Cannot use the AI runtime directory."};
        QLockFile guard(jobPath(id) + ".lock"); guard.setStaleLockTime(0);
        if (!guard.tryLock(0)) return lookup(id);
        if (QFileInfo::exists(jobPath(id)) || link(QFileInfo(jobPath(id)))) {
            QByteArray bytes; QString error; const auto old = read(id, &bytes, &error);
            if (!error.isEmpty()) return unknown(id, error);
            if (old.value("snapshot").toObject() != snapshot) return {id, "failed", {}, "Capture ID conflicts with a different AI snapshot."};
            return task(old);
        }
        QJsonObject object{{"version", 1}, {"id", id}, {"snapshot", snapshot}, {"snapshot_sha256", hash(snapshot)},
            {"state", "queued"}, {"stage", "ready"}, {"attempt", 1}, {"message", "Queued for camera image generation."},
            {"created_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
        QString error;
        if (!freezeProfile(&object, &error)) { object.insert("state", "failed"); object.insert("message", error); }
        if (!write(object, {})) return {{}, "failed", {}, "Cannot save the AI task record."};
        if (object.value("state") == "queued") enqueue(id, true);
        return task(object);
    }
    CameraImageTask retry(const QString &id, const QJsonObject &snapshot) {
        if (!uuid(id) || !safe()) return unknown(id, "Invalid AI task ID.");
        QLockFile guard(jobPath(id) + ".lock"); guard.setStaleLockTime(0);
        if (!guard.tryLock(0)) return lookup(id);
        QByteArray bytes; QString error; auto object = read(id, &bytes, &error);
        if (!error.isEmpty()) return unknown(id, error);
        if (object.value("snapshot").toObject() != snapshot) return unknown(id, "Retry snapshot differs from the original capture.");
        if (object.value("state") != "failed") return task(object);
        object.insert("attempt", object.value("attempt").toInt() + 1);
        object.insert("state", "queued"); object.insert("stage", "ready");
        object.insert("message", "Retry queued using the original capture.");
        if (object.value("profile").toObject().isEmpty() && !freezeProfile(&object, &error)) {
            object.insert("state", "failed"); object.insert("message", error);
        }
        if (object.value("image").toObject().isEmpty()) object.remove("comfy_prompt_id");
        object.remove("poll_started_at");
        if (!write(object, bytes)) return unknown(id, "AI task changed externally; retry was not submitted.");
        conflicts.remove(id);
        if (object.value("state") == "queued") enqueue(id, true);
        return task(object);
    }
    bool configure(bool needsText) {
        QString error;
        const auto unavailable = [this](const QString &text) {
            if (!job.value("comfy_prompt_id").toString().isEmpty()) uncertain(text); else fail(text);
        };
        if (!settings.load(&config, &error)) { unavailable(error); return false; }
        QUrl ds, comfy;
        if (!AiConnectionClient::normalizeBaseUrl(config.deepSeekUrl, &ds, &error)
            || !AiConnectionClient::normalizeBaseUrl(config.comfyUiUrl, &comfy, &error)) { unavailable(error); return false; }
        if (!job.value("comfyui_url").toString().isEmpty() && job.value("comfyui_url").toString() != comfy.toString()) {
            uncertain("Restore this task's original ComfyUI URL in AI services before recovering it."); return false;
        }
        if (needsText && !job.value("deepseek_url").toString().isEmpty() && job.value("deepseek_url").toString() != ds.toString()) {
            fail("Restore the original DeepSeek URL before retrying this capture."); return false;
        }
        if (!settings.readKey(config.comfyUiKeyRef, &comfyKey, &error)
            || (needsText && !settings.readKey(config.deepSeekKeyRef, &deepKey, &error))) { unavailable(error); return false; }
        if (needsText && deepKey.trimmed().isEmpty()) { fail("Configure a DeepSeek API key in AI services first."); return false; }
        for (const auto &key : {deepKey, comfyKey}) if (key.contains('\r') || key.contains('\n') || key.toUtf8().size() > 2048) {
            unavailable("Invalid service credential."); return false;
        }
        if (job.value("comfyui_url").toString().isEmpty()) job.insert("comfyui_url", comfy.toString());
        if (needsText && job.value("deepseek_url").toString().isEmpty()) {
            job.insert("deepseek_url", ds.toString()); job.insert("deepseek_model", config.deepSeekModel);
        }
        return true;
    }
    void pump() {
        if (!active.isEmpty() || pending.isEmpty()) return;
        const auto entry = pending.dequeue();
        auto held = std::make_unique<QLockFile>(jobPath(entry.first) + ".lock"); held->setStaleLockTime(0);
        if (!held->tryLock(0)) { QTimer::singleShot(0, this, [this] { pump(); }); return; }
        QString error; auto object = read(entry.first, &expected, &error);
        if (!error.isEmpty()) { conflicts.insert(entry.first, error); QTimer::singleShot(0, this, [this] { pump(); }); return; }
        active = entry.first; lock = std::move(held); job = object;
        if (job.value("state") == "completed" || job.value("state") == "failed") { stop(); return; }
        if (!job.value("image").toObject().isEmpty()) {
            if (configure(false)) download();
        } else if (entry.second) {
            if (!configure(job.value("description").toObject().isEmpty())) return;
            if (job.value("profile").toObject().isEmpty()) {
                QJsonObject profile;
                if (!AiCameraProfile::load(config.cameraProfilePath, &profile, &error)) { fail(error); return; }
                job.insert("profile", profile); job.insert("profile_sha256", hash(profile));
            }
            QJsonObject workflow;
            if (!AiCameraProfile::bind(job.value("profile").toObject(), job.value("snapshot").toObject(),
                {}, 1, &workflow, &error)) { fail(error); return; }
            if (!persist()) return;
            if (job.value("description").toObject().isEmpty()) describe(); else submitImage();
        } else if (!job.value("comfy_prompt_id").toString().isEmpty()) {
            if (configure(false)) poll();
        } else if (job.value("state") != "uncertain") {
            fail("Generation was interrupted before an image was submitted. Retry explicitly.");
        } else stop();
    }

    using HttpCallback = std::function<void(int, const QByteArray &, bool, bool)>;
    void http(const QString &base, const QString &path, const QString &key, const QJsonObject *payload,
        qint64 limit, int timeoutMs, HttpCallback callback, const QUrlQuery &query = {}) {
        QUrl url; QString error;
        if (!AiConnectionClient::normalizeBaseUrl(base, &url, &error)) { fail(error); return; }
        url.setPath(url.path() + path); url.setQuery(query);
        QNetworkRequest request(url);
        request.setRawHeader("Accept", "application/json, image/png, image/jpeg");
        request.setRawHeader("User-Agent", "SillyTerminal/0.1");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        if (!key.isEmpty()) request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
        if (payload) request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = payload ? network.post(request, QJsonDocument(*payload).toJson(QJsonDocument::Compact)) : network.get(request);
        auto *current = reply;
        auto data = std::make_shared<QByteArray>(); auto timeout = std::make_shared<bool>(false); auto oversized = std::make_shared<bool>(false);
        auto *deadline = new QTimer(current); deadline->setSingleShot(true);
        connect(deadline, &QTimer::timeout, this, [current, timeout] { *timeout = true; current->abort(); });
        connect(current, &QIODevice::readyRead, this, [current, data, oversized, limit] {
            *data += current->read(limit + 1 - data->size());
            if (data->size() > limit) { *oversized = true; current->abort(); }
        });
        connect(current, &QNetworkReply::finished, this, [this, current, data, timeout, oversized, limit, callback, deadline] {
            deadline->stop();
            const int code = current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool transport = current->error() != QNetworkReply::NoError;
            if (current->isOpen()) *data += current->read(limit + 1 - data->size());
            reply = nullptr; current->deleteLater();
            callback(code, *data, transport || *timeout, *oversized || data->size() > limit);
        });
        deadline->start(qBound(1, timeoutMs, 3600000));
    }
    QString httpError(const QString &service, int status, bool oversized) {
        if (oversized) return service + " response exceeded the allowed size.";
        if (status >= 300 && status < 400) return service + " redirected the request. Configure its final API URL.";
        if (status == 401 || status == 403) return service + " authentication or access was rejected.";
        if (status == 429) return service + " is rate limited; retry later.";
        if (status) return service + QString(" returned HTTP %1. Check the service and workflow configuration.").arg(status);
        return service + " connection failed or timed out.";
    }
    void describe() {
        if (!state("generating", "DeepSeek is describing the frozen camera scene.", "describing")) return;
        const auto profile = job.value("profile").toObject();
        const QString instructions = AiCameraProfile::defaultInstructions()
            + "\nAdditional visual style guidance:\n" + profile.value("instructions").toString();
        const QJsonArray messages{QJsonObject{{"role", "system"}, {"content", instructions}},
            QJsonObject{{"role", "user"}, {"content", QString::fromUtf8(QJsonDocument(job.value("snapshot").toObject()).toJson())}}};
        const QJsonObject body{{"model", job.value("deepseek_model")}, {"messages", messages},
            {"response_format", QJsonObject{{"type", "json_object"}}}, {"stream", false},
            {"max_tokens", 2048}, {"thinking", QJsonObject{{"type", "disabled"}}}};
        http(job.value("deepseek_url").toString(), "/chat/completions", deepKey, &body, 1024 * 1024,
            options.textTimeoutMs, [this](int status, const QByteArray &bytes, bool transport, bool oversized) {
            if (status != 200 || transport || oversized) { fail(httpError("DeepSeek", status, oversized)); return; }
            const auto choices = parseObject(bytes).value("choices").toArray();
            const auto choice = choices.isEmpty() ? QJsonObject{} : choices.first().toObject();
            bool valid; const auto result = parseObject(choice.value("message").toObject().value("content").toString().toUtf8(), &valid);
            const QString prompt = result.value("prompt").toString();
            const QString negative = result.value("negative_prompt").toString();
            for (const auto &secret : {deepKey, comfyKey}) if (!secret.isEmpty() && (prompt.contains(secret) || negative.contains(secret))) {
                fail("The text response contained service credentials and was discarded."); return;
            }
            QSet<QString> required, used;
            for (const auto &fact : job.value("snapshot").toObject().value("scene").toObject().value("visible_facts").toArray())
                required.insert(fact.toObject().value("id").toString());
            const auto ids = result.value("used_fact_ids").toArray();
            for (const auto &id : ids) { if (!id.isString()) valid = false; used.insert(id.toString()); }
            if (!valid || choice.value("finish_reason") != "stop" || prompt.trimmed().isEmpty() || prompt.size() > 16000
                || !result.value("used_fact_ids").isArray() || used != required || used.size() != ids.size()
                || (result.contains("negative_prompt") && (!result.value("negative_prompt").isString()
                    || result.value("negative_prompt").toString().size() > 4000))) {
                fail("DeepSeek returned an empty, truncated or invalid scene description. No image was submitted."); return;
            }
            job.insert("description", QJsonObject{{"prompt", prompt}, {"negative_prompt", result.value("negative_prompt").toString()}, {"used_fact_ids", ids}});
            if (persist()) submitImage();
        });
    }
    void submitImage() {
        QString error; QJsonObject workflow = job.value("workflow").toObject();
        if (workflow.isEmpty()) {
            const quint32 seed = QRandomGenerator::global()->generate();
            if (!AiCameraProfile::bind(job.value("profile").toObject(), job.value("snapshot").toObject(),
                job.value("description").toObject(), seed, &workflow, &error)) { fail(error); return; }
            job.insert("seed", double(seed)); job.insert("workflow", workflow); job.insert("workflow_sha256", hash(workflow));
        }
        const QString remoteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        job.insert("comfy_prompt_id", remoteId);
        job.insert("poll_started_at", QDateTime::currentMSecsSinceEpoch());
        if (!state("generating", "Submitting the camera workflow to ComfyUI.", "submitting")) return;
        const QJsonObject body{{"prompt", workflow}, {"prompt_id", remoteId}, {"client_id", active}};
        http(job.value("comfyui_url").toString(), "/prompt", comfyKey, &body, 1024 * 1024,
            config.timeoutSeconds * 1000, [this](int status, const QByteArray &bytes, bool transport, bool oversized) {
            if (transport || status >= 500 || !status || oversized) {
                uncertain("ComfyUI submission was not confirmed. Refresh status to recover; this task will not be resubmitted automatically."); return;
            }
            if (status != 200) { job.remove("comfy_prompt_id"); fail(httpError("ComfyUI", status, oversized)); return; }
            const auto response = parseObject(bytes);
            const QString acknowledged = response.value("prompt_id").toString();
            if (acknowledged != job.value("comfy_prompt_id").toString()) {
                // Older servers may ignore client-provided IDs. Preserve their
                // confirmed ID; a lost acknowledgement still remains uncertain.
                if (!uuid(acknowledged)) { uncertain("ComfyUI returned an invalid submission acknowledgement."); return; }
                job.insert("comfy_prompt_id", acknowledged);
            }
            if (!response.value("node_errors").toObject().isEmpty() || response.contains("error")) {
                job.remove("comfy_prompt_id"); fail("ComfyUI rejected the configured workflow."); return;
            }
            if (state("generating", "ComfyUI is processing the camera image.", "waiting")) pollLater();
        });
    }
    void pollLater() {
        timer.disconnect(this);
        connect(&timer, &QTimer::timeout, this, [this] { poll(); });
        timer.start(qMax(1, options.pollIntervalMs));
    }
    void poll(bool confirmAbsent = false) {
        const QString remote = job.value("comfy_prompt_id").toString();
        http(job.value("comfyui_url").toString(), "/history/" + remote, comfyKey, nullptr, 4 * 1024 * 1024,
            config.timeoutSeconds * 1000, [this, remote, confirmAbsent](int status, const QByteArray &bytes, bool transport, bool oversized) {
            if (status != 200 || transport || oversized) { uncertain(httpError("ComfyUI history", status, oversized)); return; }
            bool valid; const auto history = parseObject(bytes, &valid);
            if (!valid) { uncertain("ComfyUI returned invalid task history."); return; }
            if (history.contains(remote)) {
                const auto record = history.value(remote).toObject(), statusObject = record.value("status").toObject();
                const QString statusText = statusObject.value("status_str").toString();
                if (statusText == "error") { job.remove("image"); fail("ComfyUI could not execute the camera workflow."); return; }
                if (!statusObject.value("completed").toBool()) { uncertain("ComfyUI task history does not confirm completion."); return; }
                const auto output = record.value("outputs").toObject().value(job.value("profile").toObject().value("output_node").toString()).toObject();
                const auto images = output.value("images").toArray();
                if (images.size() != 1) { fail("The selected ComfyUI output must contain exactly one saved image."); return; }
                const auto image = images.first().toObject();
                if (!imageDescriptor(image)) {
                    fail("ComfyUI returned an invalid image resource descriptor."); return;
                }
                job.insert("image", image);
                if (state("generating", "Saving the generated camera image.", "downloading")) download();
                return;
            }
            http(job.value("comfyui_url").toString(), "/queue", comfyKey, nullptr, 4 * 1024 * 1024,
                config.timeoutSeconds * 1000, [this, remote, confirmAbsent](int code, const QByteArray &data, bool failed, bool large) {
                if (code != 200 || failed || large) { uncertain(httpError("ComfyUI queue", code, large)); return; }
                bool valid; const auto queue = parseObject(data, &valid); bool found = false;
                if (!valid || !queue.value("queue_running").isArray() || !queue.value("queue_pending").isArray()) {
                    uncertain("ComfyUI returned invalid queue information."); return;
                }
                for (const auto &name : {"queue_running", "queue_pending"})
                    for (const auto &entry : queue.value(name).toArray()) {
                        const auto row = entry.toArray();
                        if (row.size() > 1 && row.at(1).toString() == remote) found = true;
                    }
                // A task can finish between the history and queue reads.
                if (!found && !confirmAbsent) { poll(true); return; }
                if (!found) { uncertain("Task is absent from ComfyUI queue and history. No automatic resubmission was made."); return; }
                const qint64 started = qint64(job.value("poll_started_at").toDouble());
                if (QDateTime::currentMSecsSinceEpoch() - started > options.maximumWaitMs) {
                    uncertain("ComfyUI is still processing this task. Refresh status later; no new task is needed."); return;
                }
                if (state("generating", "ComfyUI is processing the camera image.", "waiting")) pollLater();
            });
        });
    }
    void download() {
        const auto image = job.value("image").toObject();
        QUrlQuery query;
        for (const auto &key : {"filename", "subfolder", "type"}) query.addQueryItem(key, image.value(key).toString());
        http(job.value("comfyui_url").toString(), "/view", comfyKey, nullptr, imageLimit,
            config.timeoutSeconds * 1000, [this](int status, const QByteArray &bytes, bool transport, bool oversized) {
            if (status != 200 || transport || oversized) { fail(httpError("ComfyUI image", status, oversized)); return; }
            QImage image; QString format;
            if (!decode(bytes, &image, &format)) { fail("ComfyUI did not return a supported PNG/JPEG image within camera limits."); return; }
            const auto profile = job.value("profile").toObject();
            if (qint64(image.width()) * profile.value("height").toInt() != qint64(image.height()) * profile.value("width").toInt()) {
                fail("ComfyUI output has a different aspect ratio from the camera profile."); return;
            }
            const auto view = job.value("snapshot").toObject().value("scene").toObject().value("view").toObject();
            image = image.scaled(view.value("width").toInt(), view.value("height").toInt(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
            if (!image.save(&buffer, "PNG") || png.size() > imageLimit) { fail("Cannot encode camera image within the 20 MiB limit."); return; }
            const QString path = imagePath(active);
            if (!safe() || link(QFileInfo(path))) { fail("Cannot save the camera image to the AI resource directory."); return; }
            QFile existing(path);
            if (existing.exists()) {
                if (!existing.open(QIODevice::ReadOnly) || existing.size() > imageLimit || existing.readAll() != png) {
                    fail("An existing camera resource differs from this result. It was not overwritten."); return;
                }
            } else {
                QSaveFile output(path); output.setDirectWriteFallback(false);
                if (!output.open(QIODevice::WriteOnly) || output.write(png) != png.size()
                    || QFileInfo::exists(path) || link(QFileInfo(path)) || !output.commit()) {
                    fail("Cannot commit the camera image resource."); return;
                }
            }
            job.insert("resource_id", active);
            if (state("completed", "Camera image saved.", "completed")) stop();
        }, query);
    }
    CameraImageData readImage(const QString &id) {
        QByteArray record; QString error; const auto object = read(id, &record, &error);
        if (!error.isEmpty()) return {{}, {}, error};
        if (object.value("state") != "completed" || !safe() || link(QFileInfo(imagePath(id))))
            return {{}, {}, "Camera image is not available or its resource path is invalid."};
        QFile file(imagePath(id));
        if (!file.open(QIODevice::ReadOnly) || file.size() > imageLimit) return {{}, {}, "Saved camera image is missing, unreadable or too large."};
        const auto data = file.readAll(); QImage image; QString format;
        if (!decode(data, &image, &format)) return {{}, {}, "Saved camera image is invalid. Existing content was preserved."};
        return {data, format, {}};
    }
};

AiCameraImageProvider::AiCameraImageProvider(const QString &directory, const AiSettingsStore &settings, const AiCameraOptions &options)
    : m(std::make_unique<Impl>(directory, settings, options)) {}
AiCameraImageProvider::~AiCameraImageProvider() = default;
CameraImageTask AiCameraImageProvider::submit(const QJsonObject &snapshot) { return m->submit(snapshot); }
CameraImageTask AiCameraImageProvider::query(const QString &id) { return m->lookup(id); }
CameraImageTask AiCameraImageProvider::recover(const QString &id) { return m->lookup(id); }
CameraImageTask AiCameraImageProvider::retry(const QString &id, const QJsonObject &snapshot) { return m->retry(id, snapshot); }
CameraImageData AiCameraImageProvider::readImage(const QString &id) { return m->readImage(id); }
