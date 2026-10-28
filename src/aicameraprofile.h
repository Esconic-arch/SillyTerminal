#pragma once
#include <QJsonObject>
#include <QString>

// Versioned task preset + ComfyUI API graph and explicit input bindings.
// No script execution or SillyTavern extension interpretation.
namespace AiCameraProfile {
QString defaultInstructions();
bool validate(const QJsonObject &profile, QString *error);
bool load(const QString &path, QJsonObject *profile, QString *error);
bool bind(const QJsonObject &profile, const QJsonObject &snapshot,
    const QJsonObject &description, quint32 seed, QJsonObject *workflow, QString *error);
}
