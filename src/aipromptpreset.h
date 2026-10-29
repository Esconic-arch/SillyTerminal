#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// Native, provider-independent prompt modules. Importers for other formats can
// target this contract without changing callers or executing frontend scripts.
namespace AiPromptPreset {
QJsonObject empty();
bool validate(const QJsonObject &preset, QString *error);
// An empty path selects the built-in task instructions with no extra modules.
bool load(const QString &path, QJsonObject *preset, QString *error);
bool compose(const QJsonObject &preset, const QString &instructions,
    const QString &input, const QString &finalInstructions, QJsonArray *messages, QString *error);
bool validateMessages(const QJsonArray &messages, QString *error);
}
