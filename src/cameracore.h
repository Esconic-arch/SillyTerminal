#pragma once

#include "cameraimageprovider.h"
#include <QJsonArray>
#include <QList>
#include <memory>

struct CameraCapture {
    QString id;
    QJsonObject snapshot;
    CameraImageTask task;
    int attempt = 0;
    QString error;
    QJsonObject extraFields; // Preserve unknown record metadata across status updates.
};

// Real files for a simulated target; never connects to a host camera or network.
class CameraCore {
public:
    explicit CameraCore(const QString &networkDirectory,
        std::shared_ptr<CameraImageProvider> images = {});
    QString directory() const { return m_directory; }
    QString initializationError() const { return m_initializationError; }
    bool connectDevice(const QString &address, const QString &user, const QString &password, QString *error);
    void disconnectDevice();
    bool connected() const { return !m_user.isEmpty(); }
    QJsonObject deviceStatus(QString *error) const;
    CameraCapture capture();
    CameraCapture refresh(const QString &captureId);
    CameraCapture retry(const QString &captureId);
    CameraCapture readCapture(const QString &captureId) const;
    QList<CameraCapture> captures(QString *error) const;
    CameraImageData image(const QString &captureId);

private:
    QString m_directory;
    QString m_initializationError;
    QString m_address, m_user, m_passwordHash;
    std::shared_ptr<CameraImageProvider> m_images;
    void initialize();
    QString path(const QString &relative, QString *error) const;
    QJsonObject readJson(const QString &relative, QString *error, QByteArray *bytes = nullptr) const;
    QJsonObject loadDevice(QString *error) const;
    QJsonObject authorizedDevice(bool capture, QString *error) const;
    QJsonObject loadScene(QString *error) const;
    CameraCapture readRecord(const QString &id, QByteArray *bytes) const;
    bool writeRecord(const CameraCapture &record, const QByteArray *expected, QString *error);
    CameraCapture updateTask(CameraCapture record, bool retry);
};
