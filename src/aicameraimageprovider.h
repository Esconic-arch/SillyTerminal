#pragma once
#include "aiservices.h"
#include "cameraimageprovider.h"
#include <memory>

struct AiCameraOptions {
    int pollIntervalMs = 1000;
    int textTimeoutMs = 120000;
    int maximumWaitMs = 600000;
};

// Own this object for the application/session lifetime, not the camera window.
// Construction does not read credentials or initiate external I/O.
class AiCameraImageProvider final : public CameraImageProvider {
public:
    explicit AiCameraImageProvider(const QString &directory,
        const AiSettingsStore &settings = AiSettingsStore{}, const AiCameraOptions &options = {});
    ~AiCameraImageProvider() override;
    CameraImageTask submit(const QJsonObject &snapshot) override;
    CameraImageTask query(const QString &taskId) override;
    CameraImageTask recover(const QString &captureId) override;
    CameraImageTask retry(const QString &taskId, const QJsonObject &snapshot) override;
    CameraImageData readImage(const QString &resourceId) override;
private:
    class Impl;
    std::unique_ptr<Impl> m;
};
