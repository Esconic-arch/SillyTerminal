#include "aipromptpreset.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace {
constexpr int fileLimit = 1024 * 1024, messageLimit = 2 * 1024 * 1024;
bool fail(const QString &text, QString *error) { if (error) *error = text; return false; }
bool fields(const QJsonObject &object, const QSet<QString> &allowed) {
    for (auto it = object.begin(); it != object.end(); ++it) if (!allowed.contains(it.key())) return false;
    return true;
}
}

QJsonObject AiPromptPreset::empty() {
    return {{"version", 1}, {"name", "Built-in task instructions"}, {"modules", QJsonArray{}}};
}

bool AiPromptPreset::validate(const QJsonObject &preset, QString *error) {
    if (preset.contains("prompts") || preset.contains("prompt_order"))
        return fail("This is a SillyTavern preset. Native preset v1 is required; its macros and extensions need an importer.", error);
    if (!fields(preset, {"version", "name", "modules"}) || preset.value("version").toDouble() != 1
        || !preset.value("name").isString() || preset.value("name").toString().trimmed().isEmpty()
        || preset.value("name").toString().size() > 200 || !preset.value("modules").isArray()
        || preset.value("modules").toArray().size() > 256
        || QJsonDocument(preset).toJson(QJsonDocument::Compact).size() > fileLimit)
        return fail("Use native preset version 1 with a name and up to 256 modules (maximum 1 MiB).", error);
    QSet<QString> ids;
    const QRegularExpression identifier("^[A-Za-z0-9_.-]{1,64}$");
    for (const auto &value : preset.value("modules").toArray()) {
        const auto module = value.toObject();
        const QString id = module.value("id").toString(), role = module.value("role").toString();
        const QString position = module.value("position").toString(), content = module.value("content").toString();
        if (!value.isObject() || !fields(module, {"id", "enabled", "role", "position", "content"})
            || !identifier.match(id).hasMatch() || ids.contains(id) || !module.value("enabled").isBool()
            || (role != "system" && role != "user" && role != "assistant")
            || (position != "before_input" && position != "after_input")
            || (role == "system" && position != "before_input") || !module.value("content").isString()
            || content.size() > 65536 || (module.value("enabled").toBool() && content.trimmed().isEmpty()))
            return fail("Invalid preset module. Check its unique id, enabled flag, role, position and content (up to 65536 characters). System modules belong before the input.", error);
        if (module.value("enabled").toBool() && (content.contains("{{") || content.contains("}}")))
            return fail("Native preset v1 does not expand template macros. Expand or disable the affected module before use.", error);
        ids.insert(id);
    }
    return true;
}

bool AiPromptPreset::load(const QString &path, QJsonObject *preset, QString *error) {
    if (path.isEmpty()) { *preset = empty(); return true; }
    const QFileInfo info(path);
    if (!info.isAbsolute() || info.isSymLink() || info.isJunction() || !info.isFile())
        return fail("Choose a regular native preset JSON file using an absolute path, or leave the preset empty.", error);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > fileLimit)
        return fail("Cannot read the preset, or it exceeds 1 MiB. The file was preserved.", error);
    const auto bytes = file.read(fileLimit + 1);
    if (bytes.size() > fileLimit || file.error() != QFileDevice::NoError)
        return fail("Cannot read the preset, or it exceeds 1 MiB. The file was preserved.", error);
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return fail("The preset is not a JSON object.", error);
    if (!validate(document.object(), error)) return false;
    *preset = document.object(); return true;
}

bool AiPromptPreset::validateMessages(const QJsonArray &messages, QString *error) {
    if (messages.size() < 2 || messages.size() > 515
        || messages.first().toObject().value("role") != "system" || messages.last().toObject().value("role") != "user"
        || QJsonDocument(messages).toJson(QJsonDocument::Compact).size() > messageLimit)
        return fail("The assembled prompt is invalid or exceeds 2 MiB.", error);
    QString previous;
    for (const auto &value : messages) {
        const auto message = value.toObject(); const QString role = message.value("role").toString();
        if (!value.isObject() || !fields(message, {"role", "content"})
            || (role != "system" && role != "user" && role != "assistant") || role == previous
            || (role == "system" && !previous.isEmpty()) || !message.value("content").isString()
            || message.value("content").toString().trimmed().isEmpty())
            return fail("The assembled prompt has invalid roles or message content.", error);
        previous = role;
    }
    if (messages.at(1).toObject().value("role") != "user")
        return fail("Start preset examples with a user message before an assistant example.", error);
    return true;
}

bool AiPromptPreset::compose(const QJsonObject &preset, const QString &instructions,
    const QString &input, const QString &finalInstructions, QJsonArray *messages, QString *error) {
    if (!validate(preset, error)) return false;
    if (instructions.trimmed().isEmpty() || input.trimmed().isEmpty() || finalInstructions.trimmed().isEmpty())
        return fail("Prompt composition requires task instructions, input and final output instructions.", error);
    QJsonArray result;
    const auto append = [&result](const QString &role, const QString &content) {
        if (!result.isEmpty() && result.last().toObject().value("role") == role) {
            auto last = result.takeAt(result.size() - 1).toObject();
            last.insert("content", last.value("content").toString() + "\n\n" + content); result.append(last);
        } else result.append(QJsonObject{{"role", role}, {"content", content}});
    };
    append("system", instructions);
    const auto modules = preset.value("modules").toArray();
    for (const auto &value : modules) {
        const auto module = value.toObject();
        if (module.value("enabled").toBool() && module.value("role") == "system") append("system", module.value("content").toString());
    }
    for (const QString &position : {QString("before_input"), QString("after_input")}) {
        if (position == "after_input") append("user", "<task_input>\n" + input + "\n</task_input>");
        for (const auto &value : modules) {
            const auto module = value.toObject();
            if (module.value("enabled").toBool() && module.value("role") != "system" && module.value("position") == position)
                append(module.value("role").toString(), module.value("content").toString());
        }
    }
    append("user", "Task output requirements:\n" + finalInstructions);
    if (!validateMessages(result, error)) return false;
    *messages = result; return true;
}
