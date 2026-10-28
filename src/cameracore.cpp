#include "cameracore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QPasswordDigestor>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>

namespace {
bool fail(const QString &message, QString *error) { if (error) *error = message; return false; }
bool isLink(const QFileInfo &info) { return info.isSymLink() || info.isJunction(); }
bool safeAncestors(QString name) {
    for (;;) {
        const QFileInfo info(name);
        if (isLink(info) || (info.exists() && !info.isDir())) return false;
        const QString parent = info.absolutePath();
        if (parent == name) return true;
        name = parent;
    }
}
bool text(const QJsonValue &v, int max = 2048) {
    return v.isString() && !v.toString().trimmed().isEmpty() && v.toString().size() <= max;
}
bool strings(const QJsonValue &v) {
    if (!v.isArray() || v.toArray().size() > 64) return false;
    for (const auto &item : v.toArray()) if (!text(item)) return false;
    return true;
}
QString endpoint(QString address) {
    if (!address.contains("://")) address.prepend("http://");
    const QUrl url(address, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "http" || url.host().isEmpty()
        || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()
        || (url.path() != "" && url.path() != "/") || url.port(80) != 80) return {};
    return "http://" + url.host().toLower();
}
bool validId(const QString &id) {
    static const QRegularExpression pattern("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    return pattern.match(id).hasMatch();
}
QString digest(const QJsonObject &object) {
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(object).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
const QStringList states{"prepared", "queued", "generating", "completed", "failed", "uncertain"};
}

CameraCore::CameraCore(const QString &networkDirectory, std::shared_ptr<CameraImageProvider> images)
    : m_directory(QDir::cleanPath(QFileInfo(networkDirectory).absoluteFilePath())),
      m_images(images ? std::move(images) : std::make_shared<UnavailableCameraImageProvider>()) {
    initialize();
}

void CameraCore::initialize() {
    static const bool ready = [] { Q_INIT_RESOURCE(initial_network); return true; }();
    Q_UNUSED(ready);
    if (!safeAncestors(m_directory)) { m_initializationError = "Camera data directory contains an unsupported path."; return; }
    if (!QFileInfo::exists(m_directory)) {
        const QString parent = QFileInfo(m_directory).absolutePath();
        if (!QDir().mkpath(parent)) { m_initializationError = "Cannot create camera data directory."; return; }
        QTemporaryDir staged(parent + "/.network-init-XXXXXX");
        if (!staged.isValid() || !QDir(staged.path()).mkdir("captures")) {
            m_initializationError = "Cannot initialize camera data."; return;
        }
        for (const QString &name : {QStringLiteral("device.json"), QStringLiteral("scene.json")}) {
            QFile source(":/initial-network/" + name), destination(staged.path() + '/' + name);
            if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
                m_initializationError = "Cannot read initial camera files."; return;
            }
            const QByteArray bytes = source.readAll();
            if (source.error() != QFileDevice::NoError || destination.write(bytes) != bytes.size() || !destination.flush()) {
                m_initializationError = "Cannot write initial camera files."; return;
            }
            destination.close();
        }
        if (!QDir().rename(staged.path(), m_directory) && !QFileInfo::exists(m_directory)) {
            m_initializationError = "Cannot publish camera data directory."; return;
        }
    }
    if (!safeAncestors(m_directory)) { m_initializationError = "Camera data directory has changed."; return; }
    m_directory = QFileInfo(m_directory).canonicalFilePath();
}

QString CameraCore::path(const QString &relative, QString *error) const {
    if (error) error->clear();
    if (!m_initializationError.isEmpty()) { fail(m_initializationError, error); return {}; }
    if (m_directory.isEmpty() || !safeAncestors(m_directory) || !QFileInfo(m_directory).isDir()
        || QFileInfo(m_directory).canonicalFilePath() != m_directory) {
        fail("Camera data directory is missing or has changed.", error); return {};
    }
    QString result = m_directory;
    const auto parts = relative.split('/');
    for (int i = 0; i < parts.size(); ++i) {
        const auto &part = parts.at(i);
        static const QRegularExpression component("^[A-Za-z0-9_-]+(?:\\.[A-Za-z0-9_-]+)*$");
        if (!component.match(part).hasMatch()) { fail("Unsupported camera data path.", error); return {}; }
        result += '/' + part;
        const QFileInfo info(result);
        if (isLink(info) || (i + 1 < parts.size() && !info.isDir())
            || (info.exists() && !info.isDir() && !info.isFile())) {
            fail("Camera data path is missing or unsupported: " + relative, error); return {};
        }
    }
    return result;
}

QJsonObject CameraCore::readJson(const QString &relative, QString *error, QByteArray *bytes) const {
    const QString filename = path(relative, error);
    if (filename.isEmpty()) return {};
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        fail("Cannot read camera file: " + relative, error); return {};
    }
    const QByteArray data = file.readAll();
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(data, &parse);
    if (file.error() != QFileDevice::NoError || parse.error != QJsonParseError::NoError || !document.isObject()) {
        fail("Invalid camera file; existing content preserved: " + relative, error); return {};
    }
    if (bytes) *bytes = data;
    return document.object();
}

QJsonObject CameraCore::loadDevice(QString *error) const {
    const auto device = readJson("device.json", error);
    if (!error->isEmpty()) return {};
    bool valid = device.value("version").toInt() == 1 && device.value("id") == "home-camera"
        && text(device.value("name")) && text(device.value("manufacturer")) && text(device.value("model"))
        && text(device.value("firmware")) && !endpoint(device.value("address").toString()).isEmpty()
        && device.value("online").isBool() && device.value("http_enabled").isBool()
        && device.value("privacy_mode").isBool() && device.value("accounts").isArray()
        && device.value("accounts").toArray().size() <= 16;
    QStringList names;
    static const QRegularExpression salt("^[0-9a-f]{32}$"), hash("^[0-9a-f]{64}$");
    for (const auto &value : device.value("accounts").toArray()) {
        const auto account = value.toObject();
        const QString name = account.value("username").toString();
        valid = valid && text(account.value("username"), 64) && !names.contains(name)
            && account.value("algorithm") == "pbkdf2-sha256" && account.value("iterations").toInt() == 100000
            && salt.match(account.value("salt").toString()).hasMatch()
            && hash.match(account.value("password_hash").toString()).hasMatch()
            && account.value("can_capture").isBool();
        names.append(name);
    }
    if (!valid) { fail("Invalid device.json; existing content preserved.", error); return {}; }
    return device;
}

bool CameraCore::connectDevice(const QString &address, const QString &user, const QString &password, QString *error) {
    disconnectDevice();
    QString message;
    const auto device = loadDevice(&message);
    const QString requested = endpoint(address);
    if (message.isEmpty() && requested.isEmpty()) message = "Use an HTTP camera address without credentials, path, or query.";
    if (message.isEmpty() && requested != endpoint(device.value("address").toString())) message = "No camera responds at that address.";
    if (message.isEmpty() && !device.value("online").toBool()) message = "Camera is offline.";
    if (message.isEmpty() && !device.value("http_enabled").toBool()) message = "Connection refused: HTTP service is disabled.";
    if (message.isEmpty() && password.size() <= 1024) {
        for (const auto &value : device.value("accounts").toArray()) {
            const auto account = value.toObject();
            if (account.value("username").toString() != user) continue;
            const auto hash = QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
                password.toUtf8(), QByteArray::fromHex(account.value("salt").toString().toLatin1()), 100000, 32).toHex();
            if (hash != account.value("password_hash").toString().toLatin1()) break;
            m_address = requested; m_user = user;
            m_passwordHash = account.value("password_hash").toString() + ':' + account.value("salt").toString();
            if (error) error->clear();
            return true;
        }
    }
    return fail(message.isEmpty() ? "Authentication failed." : message, error);
}

void CameraCore::disconnectDevice() { m_address.clear(); m_user.clear(); m_passwordHash.clear(); }

QJsonObject CameraCore::authorizedDevice(bool capture, QString *error) const {
    auto device = loadDevice(error);
    if (!error->isEmpty()) return {};
    if (!connected()) { fail("Connect and sign in to the camera first.", error); return {}; }
    if (endpoint(device.value("address").toString()) != m_address) { fail("Camera address changed; reconnect.", error); return {}; }
    if (!device.value("online").toBool()) { fail("Camera is offline.", error); return {}; }
    if (!device.value("http_enabled").toBool()) { fail("Connection refused: HTTP service is disabled.", error); return {}; }
    for (const auto &value : device.value("accounts").toArray()) {
        const auto account = value.toObject();
        if (account.value("username") != m_user || account.value("password_hash").toString()
            + ':' + account.value("salt").toString() != m_passwordHash) continue;
        if (capture && !account.value("can_capture").toBool()) { fail("Permission denied: this account cannot capture images.", error); return {}; }
        if (capture && device.value("privacy_mode").toBool()) { fail("Privacy mode is active.", error); return {}; }
        device.remove("accounts");
        device.insert("signed_in_as", m_user);
        device.insert("can_capture", account.value("can_capture"));
        return device;
    }
    fail("Camera account changed; sign in again.", error); return {};
}

QJsonObject CameraCore::deviceStatus(QString *error) const {
    QString message;
    const auto device = authorizedDevice(false, &message);
    if (error) *error = message;
    return device;
}

QJsonObject CameraCore::loadScene(QString *error) const {
    const auto scene = readJson("scene.json", error);
    if (!error->isEmpty()) return {};
    const auto view = scene.value("view").toObject(), state = scene.value("state").toObject();
    bool valid = scene.value("version").toInt() == 1 && text(scene.value("id"), 128)
        && text(scene.value("revision"), 128) && text(scene.value("world_time"), 64)
        && QDateTime::fromString(scene.value("world_time").toString(), Qt::ISODate).isValid()
        && text(view.value("id"), 128) && text(view.value("description"))
        && view.value("width").toInt() >= 16 && view.value("width").toInt() <= 4096
        && view.value("height").toInt() >= 16 && view.value("height").toInt() <= 4096
        && QStringList{"open", "closed"}.contains(state.value("door").toString())
        && QStringList{"open", "closed"}.contains(state.value("curtains").toString())
        && QStringList{"on", "off"}.contains(state.value("lamp").toString())
        && strings(scene.value("constraints")) && scene.value("references").isArray()
        && scene.value("references").toArray().size() <= 16;
    for (const QString &group : {QStringLiteral("objects"), QStringLiteral("people")}) {
        const auto list = scene.value(group);
        valid = valid && list.isArray() && list.toArray().size() <= 64;
        QStringList ids;
        for (const auto &value : list.toArray()) {
            const auto item = value.toObject();
            valid = valid && text(item.value("id"), 128) && !ids.contains(item.value("id").toString())
                && text(item.value("description")) && text(item.value("location")) && item.value("visible").isBool();
            if (group == "people") valid = valid && text(item.value("action"));
            ids.append(item.value("id").toString());
        }
    }
    for (const auto &value : scene.value("references").toArray()) {
        const auto ref = value.toObject();
        valid = valid && text(ref.value("id"), 128) && text(ref.value("version"), 128);
    }
    if (!valid) { fail("Invalid scene.json; no photograph was requested. Existing content preserved.", error); return {}; }
    return scene;
}

CameraCapture CameraCore::readRecord(const QString &id, QByteArray *bytes) const {
    CameraCapture record;
    if (!validId(id)) { record.error = "Invalid capture identifier."; return record; }
    const auto object = readJson("captures/" + id + ".json", &record.error, bytes);
    if (!record.error.isEmpty()) return record;
    record.id = id;
    record.snapshot = object.value("snapshot").toObject();
    const auto task = object.value("task").toObject();
    record.task = {task.value("id").toString(), task.value("state").toString(),
        task.value("resource_id").toString(), task.value("message").toString()};
    record.attempt = object.value("attempt").toInt(-1);
    record.extraFields = object;
    for (const QString &key : {QStringLiteral("version"), QStringLiteral("id"), QStringLiteral("snapshot"),
        QStringLiteral("snapshot_sha256"), QStringLiteral("attempt"), QStringLiteral("task")}) record.extraFields.remove(key);
    if (object.value("version").toInt() != 1 || object.value("id") != id
        || record.snapshot.value("capture_id") != id || record.snapshot.value("schema_version").toInt() != 1
        || record.snapshot.value("camera_id") != "home-camera" || !record.snapshot.value("scene").isObject()
        || object.value("snapshot_sha256").toString() != digest(record.snapshot)
        || record.attempt < 0 || record.attempt > 1000000 || !states.contains(record.task.state)
        || record.task.id.size() > 256 || record.task.resourceId.size() > 256 || record.task.message.size() > 2048
        || (record.task.state == "completed" && record.task.resourceId.isEmpty()))
        record.error = "Invalid or externally modified capture record; file preserved: " + id;
    return record;
}

CameraCapture CameraCore::readCapture(const QString &id) const { return readRecord(id, nullptr); }

bool CameraCore::writeRecord(const CameraCapture &record, const QByteArray *expected, QString *error) {
    const QString filename = path("captures/" + record.id + ".json", error);
    if (filename.isEmpty()) return false;
    QLockFile lock(filename + ".lock");
    if (isLink(QFileInfo(filename + ".lock")) || !lock.tryLock(0)) return fail("Capture record is busy.", error);
    const auto task = record.task;
    QJsonObject object = record.extraFields;
    const QJsonObject fields{{"version", 1}, {"id", record.id}, {"snapshot", record.snapshot},
        {"snapshot_sha256", digest(record.snapshot)}, {"attempt", record.attempt},
        {"task", QJsonObject{{"id", task.id}, {"state", task.state}, {"resource_id", task.resourceId}, {"message", task.message}}}};
    for (auto it = fields.begin(); it != fields.end(); ++it) object.insert(it.key(), it.value());
    const QByteArray data = QJsonDocument(object).toJson();
    if (!expected) {
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(data) != data.size() || !file.flush())
            return fail("Cannot create capture record.", error);
        return true;
    }
    const auto unchanged = [&] {
        QString message;
        if (path("captures/" + record.id + ".json", &message).isEmpty()) return false;
        QFile current(filename);
        return current.open(QIODevice::ReadOnly) && current.size() <= 1024 * 1024
            && current.readAll() == *expected && current.error() == QFileDevice::NoError;
    };
    if (!unchanged()) return fail("Capture record changed externally; reload before continuing.", error);
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) return fail("Cannot save capture record.", error);
    if (!unchanged()) return fail("Capture record changed externally; reload before continuing.", error);
    return file.commit() || fail("Cannot commit capture record.", error);
}

CameraCapture CameraCore::capture() {
    CameraCapture record;
    const auto device = authorizedDevice(true, &record.error);
    if (!record.error.isEmpty()) return record;
    const auto scene = loadScene(&record.error);
    if (!record.error.isEmpty()) return record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto state = scene.value("state").toObject();
    QJsonArray facts;
    const auto fact = [&facts](const QString &id, const QString &description) {
        facts.append(QJsonObject{{"id", id}, {"description", description}});
    };
    fact("door", "Room door is " + state.value("door").toString() + '.');
    fact("curtains", "Curtains are " + state.value("curtains").toString() + '.');
    fact("lamp", "Floor lamp is " + state.value("lamp").toString() + '.');
    int people = 0;
    for (const QString &group : {QStringLiteral("objects"), QStringLiteral("people")}) {
        for (const auto &value : scene.value(group).toArray()) {
            const auto item = value.toObject();
            if (!item.value("visible").toBool()) continue;
            QString description = item.value("description").toString() + ", " + item.value("location").toString();
            if (group == "people") { ++people; description += ", " + item.value("action").toString(); }
            fact(group + '/' + item.value("id").toString(), description + '.');
        }
    }
    fact("people-count", QString("Visible people: %1.").arg(people));
    const auto sourceView = scene.value("view").toObject();
    const QJsonObject view{{"id", sourceView.value("id")}, {"description", sourceView.value("description")},
        {"width", sourceView.value("width")}, {"height", sourceView.value("height")}};
    QJsonArray references;
    for (const auto &value : scene.value("references").toArray()) {
        const auto ref = value.toObject();
        references.append(QJsonObject{{"id", ref.value("id")}, {"version", ref.value("version")}});
    }
    record.snapshot = {{"schema_version", 1}, {"capture_id", record.id}, {"camera_id", device.value("id")},
        {"world_time", scene.value("world_time")}, {"scene", QJsonObject{{"id", scene.value("id")},
            {"version", scene.value("revision")}, {"view", view}, {"visible_facts", facts},
            {"constraints", scene.value("constraints")}, {"references", references}}}};
    record.task.state = "prepared";
    if (!writeRecord(record, nullptr, &record.error)) return record;
    return updateTask(record, false);
}

CameraCapture CameraCore::updateTask(CameraCapture record, bool retrying) {
    QByteArray before;
    record = readRecord(record.id, &before);
    if (!record.error.isEmpty()) return record;
    if (retrying && record.task.state != "failed") {
        record.error = "Photograph changed; reload its task state before retrying."; return record;
    }
    if (!retrying && (record.task.state == "completed" || record.task.state == "failed")) return record;
    const bool submitting = record.task.state == "prepared" || retrying;
    const QString oldId = record.task.id;
    if (submitting) {
        record.task.state = "uncertain";
        record.task.message = "Submission started; awaiting image service acknowledgement.";
        ++record.attempt;
        if (!writeRecord(record, &before, &record.error)) return record;
        record = readRecord(record.id, &before);
        if (!record.error.isEmpty()) return record;
    }
    CameraImageTask result;
    if (submitting) result = retrying && !oldId.isEmpty()
        ? m_images->retry(oldId, record.snapshot) : m_images->submit(record.snapshot);
    else if (!record.task.id.isEmpty()) result = m_images->query(record.task.id);
    else result = m_images->recover(record.id);
    if (!states.contains(result.state) || result.state == "prepared"
        || result.id.size() > 256 || result.resourceId.size() > 256 || result.message.size() > 2048
        || ((result.state == "queued" || result.state == "generating") && result.id.isEmpty())
        || (result.state == "completed" && (result.id.isEmpty() || result.resourceId.isEmpty()))
        || (!submitting && !record.task.id.isEmpty() && result.id != record.task.id)) {
        result = {record.task.id, "uncertain", {}, "Image service returned an invalid task response."};
    }
    record.task = result;
    writeRecord(record, &before, &record.error);
    return record;
}

CameraCapture CameraCore::refresh(const QString &id) {
    auto record = readCapture(id);
    if (!record.error.isEmpty() || record.task.state == "completed" || record.task.state == "failed") return record;
    // A prepared request has not been submitted; reopening history is read-only.
    if (record.task.state == "prepared") return record;
    return updateTask(record, false);
}

CameraCapture CameraCore::retry(const QString &id) {
    auto record = readCapture(id);
    if (!record.error.isEmpty()) return record;
    authorizedDevice(true, &record.error);
    if (!record.error.isEmpty()) return record;
    if (record.task.state != "failed" && record.task.state != "prepared") {
        record.error = "Only a confirmed failed or never-submitted photograph can be retried."; return record;
    }
    return updateTask(record, record.task.state == "failed");
}

QList<CameraCapture> CameraCore::captures(QString *error) const {
    QString message;
    const QString folder = path("captures", &message);
    QList<CameraCapture> result;
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) { fail(message.isEmpty() ? "Capture directory is missing." : message, error); return result; }
    const auto names = QDir(folder).entryList({"*.json"}, QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
    for (const auto &name : names) {
        auto record = readCapture(name.chopped(5));
        if (!record.error.isEmpty()) { message = record.error; continue; }
        result.append(record);
    }
    if (error) *error = message;
    return result;
}

CameraImageData CameraCore::image(const QString &id) {
    const auto record = readCapture(id);
    if (!record.error.isEmpty()) return {{}, {}, record.error};
    if (record.task.state != "completed") return {{}, {}, "No image is available for this photograph."};
    auto data = m_images->readImage(record.task.resourceId);
    if (data.error.isEmpty() && (data.bytes.isEmpty() || data.bytes.size() > 20 * 1024 * 1024
        || (data.format != "png" && data.format != "jpeg")))
        return {{}, {}, "Image service returned an unsupported or oversized image."};
    return data;
}
