#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Camera-side integration seam v1. Calls return local state promptly; a real
// adapter schedules external I/O asynchronously and persists its own jobs/resources.
// submit: same capture_id + identical snapshot => existing job; different => conflict.
// retry: only a confirmed failed job, same capture_id and snapshot, new attempt.
// query: read/recover an existing job; never implicitly resubmit an uncertain job.
// No service credentials, host filenames, or ComfyUI workflow details cross this seam.
struct CameraImageTask {
    QString id;
    QString state; // queued, generating, completed, failed, uncertain
    QString resourceId;
    QString message;
};

struct CameraImageData {
    QByteArray bytes;
    QString format; // png or jpeg
    QString error;
};

class CameraImageProvider {
public:
    virtual ~CameraImageProvider() = default;
    virtual CameraImageTask submit(const QJsonObject &snapshot) = 0;
    virtual CameraImageTask query(const QString &taskId) = 0;
    // Read-only lookup after a crash before the local record received a task ID.
    virtual CameraImageTask recover(const QString &captureId) = 0;
    virtual CameraImageTask retry(const QString &taskId, const QJsonObject &snapshot) = 0;
    virtual CameraImageData readImage(const QString &resourceId) = 0;
};

// Fallback for clients constructed without an injected provider. The application
// supplies its AI adapter; this fallback never fabricates a successful capture.
class UnavailableCameraImageProvider final : public CameraImageProvider {
public:
    CameraImageTask submit(const QJsonObject &) override {
        return {{}, "failed", {}, "Image generation is not available yet."};
    }
    CameraImageTask query(const QString &id) override {
        return {id, "uncertain", {}, "Image service unavailable; task status is unknown."};
    }
    CameraImageTask recover(const QString &) override { return query({}); }
    CameraImageTask retry(const QString &, const QJsonObject &snapshot) override { return submit(snapshot); }
    CameraImageData readImage(const QString &) override { return {{}, {}, "Image service unavailable."}; }
};
