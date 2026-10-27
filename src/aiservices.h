#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>

// Real host-service settings. These never belong to the simulated computer.
struct AiServiceSettings {
    QString deepSeekUrl = QStringLiteral("https://api.deepseek.com");
    QString deepSeekModel = QStringLiteral("deepseek-flash");
    QString deepSeekKeyRef;
    QString comfyUiUrl = QStringLiteral("http://127.0.0.1:8188");
    QString comfyUiKeyRef;
    int timeoutSeconds = 15;
};

class AiSecretStore {
public:
    virtual ~AiSecretStore() = default;
    virtual bool read(const QString &id, QString *secret, QString *error) = 0;
    virtual bool write(const QString &id, const QString &secret, QString *error) = 0;
    virtual bool remove(const QString &id, QString *error) = 0;
};

std::shared_ptr<AiSecretStore> nativeAiSecretStore();

class AiSettingsStore {
public:
    explicit AiSettingsStore(const QString &filePath = {},
        std::shared_ptr<AiSecretStore> secrets = {});
    QString filePath() const { return m_filePath; }
    bool load(AiServiceSettings *settings, QString *error) const;
    bool readKey(const QString &reference, QString *secret, QString *error) const;
    // Commit new credentials before atomically replacing the public config.
    // A failed save leaves the previous config and its credentials usable.
    bool save(AiServiceSettings *settings, const QString &deepSeekKey,
        const QString &comfyUiKey, QString *error);
private:
    QString m_filePath;
    std::shared_ptr<AiSecretStore> m_secrets;
};

struct AiConnectionResult {
    bool success = false;
    QString message;
    QStringList models;
};

class QNetworkReply;

// A single asynchronous, read-only connection check. No generation is submitted.
class AiConnectionClient : public QObject {
public:
    explicit AiConnectionClient(QObject *parent = nullptr);
    ~AiConnectionClient() override { cancel(); }
    void checkDeepSeek(const QString &baseUrl, const QString &key, int timeoutMs = 15000);
    void checkComfyUi(const QString &baseUrl, const QString &bearerToken = {}, int timeoutMs = 15000);
    void cancel();
    bool busy() const { return m_reply != nullptr; }
    static bool normalizeBaseUrl(const QString &text, QUrl *url, QString *error);
    // Follow the project's callback-based widgets so this module also builds
    // with the Windows Qt kit when the checkout path contains non-ASCII text.
    std::function<void(const AiConnectionResult &)> onFinished;
private:
    QNetworkAccessManager m_network;
    QTimer m_timeout;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_body;
    bool m_deepSeek = false;
    bool m_timedOut = false;
    bool m_tooLarge = false;
    void start(const QString &baseUrl, const QString &path, const QString &key, int timeoutMs);
    void complete();
};
