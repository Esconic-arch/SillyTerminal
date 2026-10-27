#include "aiservices.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {
bool fail(const QString &message, QString *error) {
    if (error) *error = message;
    return false;
}
bool validReference(const QString &id) {
    return id.isEmpty() || (!QUuid(id).isNull()
        && QUuid(id).toString(QUuid::WithoutBraces) == id);
}
}

AiSettingsStore::AiSettingsStore(const QString &filePath, std::shared_ptr<AiSecretStore> secrets)
    : m_filePath(filePath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/ai-services.json"
        : filePath), m_secrets(secrets ? std::move(secrets) : nativeAiSecretStore()) {}

bool AiSettingsStore::load(AiServiceSettings *settings, QString *error) const {
    *settings = AiServiceSettings{};
    QFile file(m_filePath);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly)) return fail("Cannot read AI service settings: " + file.errorString(), error);
    if (file.size() > 65536) return fail("AI service settings are too large. The file was preserved.", error);
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto object = document.object();
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || object.value("version").toInt(-1) != 1)
        return fail("AI service settings are invalid or use an unsupported version. The file was preserved.", error);
    for (const auto &key : {"deepseek_url", "deepseek_model", "deepseek_key_ref", "comfyui_url", "comfyui_key_ref"}) {
        if (!object.value(key).isString()) return fail("AI service settings contain an invalid field.", error);
    }
    const int timeout = object.value("timeout_seconds").toInt(-1);
    if (timeout < 1 || timeout > 120 || object.value("timeout_seconds").toDouble() != timeout
        || !validReference(object.value("deepseek_key_ref").toString())
        || !validReference(object.value("comfyui_key_ref").toString()))
        return fail("AI service settings contain an invalid timeout or credential reference.", error);
    settings->deepSeekUrl = object.value("deepseek_url").toString();
    settings->deepSeekModel = object.value("deepseek_model").toString();
    settings->deepSeekKeyRef = object.value("deepseek_key_ref").toString();
    settings->comfyUiUrl = object.value("comfyui_url").toString();
    settings->comfyUiKeyRef = object.value("comfyui_key_ref").toString();
    settings->timeoutSeconds = timeout;
    return true;
}

bool AiSettingsStore::readKey(const QString &reference, QString *secret, QString *error) const {
    secret->clear();
    if (reference.isEmpty()) return true;
    if (!validReference(reference)) return fail("Invalid credential reference.", error);
    return m_secrets->read(reference, secret, error);
}

bool AiSettingsStore::save(AiServiceSettings *settings, const QString &deepSeekKey,
    const QString &comfyUiKey, QString *error) {
    QUrl deepSeekUrl, comfyUiUrl;
    if (!AiConnectionClient::normalizeBaseUrl(settings->deepSeekUrl, &deepSeekUrl, error)
        || !AiConnectionClient::normalizeBaseUrl(settings->comfyUiUrl, &comfyUiUrl, error)) return false;
    if (settings->deepSeekModel.trimmed().isEmpty() || settings->deepSeekModel.size() > 256)
        return fail("Enter a DeepSeek model name (up to 256 characters).", error);
    if (settings->timeoutSeconds < 1 || settings->timeoutSeconds > 120)
        return fail("Connection timeout must be between 1 and 120 seconds.", error);
    if (deepSeekKey.contains('\r') || deepSeekKey.contains('\n')
        || comfyUiKey.contains('\r') || comfyUiKey.contains('\n')
        || deepSeekKey.toUtf8().size() > 2048 || comfyUiKey.toUtf8().size() > 2048)
        return fail("API keys must be a single line of at most 2048 bytes.", error);
    if (!QDir().mkpath(QFileInfo(m_filePath).absolutePath()))
        return fail("Cannot create the AI settings directory.", error);
    QLockFile lock(m_filePath + ".lock");
    if (!lock.tryLock(1000)) return fail("AI settings are being saved by another instance. Try again.", error);
    AiServiceSettings previous;
    if (!load(&previous, error)) return false;
    AiServiceSettings next = *settings;
    next.deepSeekUrl = deepSeekUrl.toString();
    next.comfyUiUrl = comfyUiUrl.toString();
    next.deepSeekModel = next.deepSeekModel.trimmed();
    QStringList created;
    const auto rollback = [&] { for (const auto &id : created) m_secrets->remove(id, nullptr); };
    const auto store = [&](const QString &key, QString *ref) {
        ref->clear();
        if (key.isEmpty()) return true;
        *ref = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!m_secrets->write(*ref, key, error)) return false;
        created.append(*ref);
        return true;
    };
    if (!store(deepSeekKey, &next.deepSeekKeyRef) || !store(comfyUiKey, &next.comfyUiKeyRef)) {
        rollback(); return false;
    }
    const QJsonObject object{{"version", 1}, {"deepseek_url", next.deepSeekUrl},
        {"deepseek_model", next.deepSeekModel}, {"deepseek_key_ref", next.deepSeekKeyRef},
        {"comfyui_url", next.comfyUiUrl}, {"comfyui_key_ref", next.comfyUiKeyRef},
        {"timeout_seconds", next.timeoutSeconds}};
    const auto bytes = QJsonDocument(object).toJson();
    QSaveFile file(m_filePath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        rollback(); return fail("Cannot save AI service settings: " + file.errorString(), error);
    }
    *settings = next;
    // Obsolete credentials no longer referenced by the committed config.
    if (!previous.deepSeekKeyRef.isEmpty()) m_secrets->remove(previous.deepSeekKeyRef, nullptr);
    if (!previous.comfyUiKeyRef.isEmpty()) m_secrets->remove(previous.comfyUiKeyRef, nullptr);
    return true;
}

AiConnectionClient::AiConnectionClient(QObject *parent) : QObject(parent) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_reply) { m_timedOut = true; m_reply->abort(); }
    });
}

bool AiConnectionClient::normalizeBaseUrl(const QString &text, QUrl *url, QString *error) {
    QUrl result(text.trimmed(), QUrl::StrictMode);
    if (!result.isValid() || (result.scheme() != "https" && result.scheme() != "http")
        || result.host().isEmpty() || !result.userInfo().isEmpty()
        || result.hasQuery() || result.hasFragment())
        return fail("Enter an HTTP or HTTPS API base URL without credentials, query or fragment.", error);
    QString path = result.path();
    while (path.endsWith('/')) path.chop(1);
    result.setPath(path);
    *url = result;
    return true;
}

void AiConnectionClient::checkDeepSeek(const QString &baseUrl, const QString &key, int timeoutMs) {
    cancel(); m_deepSeek = true;
    start(baseUrl, "/models", key, timeoutMs);
}

void AiConnectionClient::checkComfyUi(const QString &baseUrl, const QString &token, int timeoutMs) {
    cancel(); m_deepSeek = false;
    start(baseUrl, "/system_stats", token, timeoutMs);
}

void AiConnectionClient::start(const QString &baseUrl, const QString &path,
    const QString &key, int timeoutMs) {
    QUrl url;
    QString error;
    if (!normalizeBaseUrl(baseUrl, &url, &error)) { emit finished({false, error, {}}); return; }
    if ((m_deepSeek && key.trimmed().isEmpty()) || key.contains('\r') || key.contains('\n')
        || key.toUtf8().size() > 2048) {
        emit finished({false, "Enter a valid single-line API key.", {}}); return;
    }
    url.setPath(url.path() + path);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "SillyTerminal/0.1");
    // Never forward a configured credential to a redirect target.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    if (!key.isEmpty()) request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    m_body.clear(); m_timedOut = false; m_tooLarge = false;
    m_reply = m_network.get(request);
    connect(m_reply, &QIODevice::readyRead, this, [this] {
        if (!m_reply) return;
        m_body += m_reply->read(1024 * 1024 + 1 - m_body.size());
        if (m_body.size() > 1024 * 1024) { m_tooLarge = true; m_reply->abort(); }
    });
    connect(m_reply, &QNetworkReply::finished, this, &AiConnectionClient::complete);
    m_timeout.start(qBound(1, timeoutMs, 120000));
}

void AiConnectionClient::cancel() {
    m_timeout.stop();
    if (!m_reply) return;
    auto *reply = m_reply;
    m_reply = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    m_body.clear();
}

void AiConnectionClient::complete() {
    if (!m_reply) return;
    m_timeout.stop();
    auto *reply = m_reply;
    m_reply = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply->error();
    if (reply->isOpen()) m_body += reply->read(1024 * 1024 + 1 - m_body.size());
    reply->deleteLater();
    AiConnectionResult result;
    if (m_timedOut) result.message = "Connection timed out. Check the address and service status.";
    else if (m_tooLarge || m_body.size() > 1024 * 1024) result.message = "The service response exceeded the 1 MiB limit.";
    else if (status >= 300 && status < 400) result.message = "The API redirected the request. Enter its final base URL.";
    else if (status == 401 || status == 403) result.message = "Authentication was rejected. Check the API key or access policy.";
    else if (status == 404) result.message = "API endpoint not found. Check the base URL and service type.";
    else if (status == 429) result.message = "The service is rate limited. Try again later.";
    else if (status >= 400) result.message = QStringLiteral("The service returned HTTP %1.").arg(status);
    else if (networkError != QNetworkReply::NoError) {
        if (networkError == QNetworkReply::SslHandshakeFailedError)
            result.message = "TLS verification failed. Check the server certificate and system time.";
        else result.message = "Cannot connect to the service. Check its address, port and network connection.";
    } else if (status != 200) result.message = "The service returned an unexpected HTTP response.";
    else {
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(m_body, &parse);
        const auto object = document.object();
        if (parse.error != QJsonParseError::NoError || !document.isObject())
            result.message = "The API did not return a JSON object.";
        else if (m_deepSeek) {
            bool valid = object.value("data").isArray();
            for (const auto &value : object.value("data").toArray()) {
                const auto id = value.toObject().value("id");
                if (!id.isString() || id.toString().trimmed().isEmpty() || id.toString().size() > 256) { valid = false; break; }
                result.models.append(id.toString());
            }
            result.models.removeDuplicates();
            if (valid && !result.models.isEmpty()) {
                result.success = true;
                result.message = QStringLiteral("Connected. %1 model(s) available.").arg(result.models.size());
            } else { result.models.clear(); result.message = "The API returned no usable model list."; }
        } else if (object.value("system").isObject() && object.value("devices").isArray()) {
            result.success = true;
            result.message = "Connected to ComfyUI. No image generation was submitted.";
        } else result.message = "The server did not return ComfyUI system information.";
    }
    m_body.clear();
    emit finished(result);
}
