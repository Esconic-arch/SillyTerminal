#include "aipromptpreset.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>

namespace {
QJsonObject module(const QString &id, const QString &role, const QString &position, const QString &content, bool enabled = true) {
    return {{"id", id}, {"role", role}, {"position", position}, {"content", content}, {"enabled", enabled}};
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); QTemporaryDir temp; if (!temp.isValid()) return 2;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *message) { ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); } };
    QString error; QJsonObject preset; QJsonArray messages;
    check(AiPromptPreset::load({}, &preset, &error) && preset == AiPromptPreset::empty(), "no preset selects the built-in task contract");
    check(AiPromptPreset::compose(preset, "task rules", "literal {{input data}}", "Return JSON", &messages, &error)
        && messages.size() == 2 && messages.first().toObject().value("content") == "task rules"
        && messages.last().toObject().value("content").toString().contains("literal {{input data}}")
        && messages.last().toObject().value("content").toString().endsWith("Return JSON"),
        "input is literal data and output instructions remain present without an optional preset");
    preset.insert("modules", QJsonArray{module("system", "system", "before_input", "style rules"),
        module("example-user", "user", "before_input", "example question"),
        module("example-assistant", "assistant", "before_input", "example answer"),
        module("disabled", "assistant", "after_input", "hidden text", false),
        module("after", "user", "after_input", "extra guidance")});
    check(AiPromptPreset::compose(preset, "task rules", "scene facts", "Return JSON", &messages, &error)
        && messages.size() == 4 && messages.first().toObject().value("content") == "task rules\n\nstyle rules"
        && messages.at(1).toObject().value("content") == "example question"
        && messages.at(2).toObject().value("content") == "example answer"
        && messages.last().toObject().value("content").toString().contains("extra guidance")
        && !QJsonDocument(messages).toJson().contains("hidden text"), "module roles, order, disabled entries and same-role merging are applied");
    auto modified = preset; auto modules = preset.value("modules").toArray();
    modules.append(module("ending", "assistant", "after_input", "assistant example")); modified.insert("modules", modules);
    check(AiPromptPreset::compose(modified, "task", "facts", "Return JSON", &messages, &error)
        && messages.last().toObject().value("role") == "user", "assistant examples never turn into an unsupported trailing prefix");
    modified = preset; modules = preset.value("modules").toArray(); modules.append(modules.first()); modified.insert("modules", modules);
    check(!AiPromptPreset::validate(modified, &error), "duplicate module identifiers are rejected");
    modified = preset; modified.insert("version", 1.5);
    check(!AiPromptPreset::validate(modified, &error), "unsupported schema versions are rejected exactly");
    modified = preset; modified.insert("extensions", QJsonObject{});
    check(!AiPromptPreset::validate(modified, &error), "unsupported extension fields cannot be silently discarded");
    check(!AiPromptPreset::validate(QJsonObject{{"prompts", QJsonArray{}}, {"prompt_order", QJsonArray{}}}, &error)
        && error.contains("SillyTavern"), "SillyTavern presets produce an explicit importer requirement");
    const auto badModule = [&](const QJsonObject &entry) { auto value = AiPromptPreset::empty(); value.insert("modules", QJsonArray{entry}); return !AiPromptPreset::validate(value, &error); };
    check(badModule(module("macro", "user", "before_input", "{{char}}")), "unexpanded macros are rejected before generation");
    check(badModule(module("wrong-role", "tool", "before_input", "text")), "unsupported roles are rejected");
    check(badModule(module("wrong-position", "system", "after_input", "text")), "system messages cannot be inserted after the input");
    check(badModule(module("empty", "user", "before_input", "")), "enabled empty modules are rejected");
    check(!badModule(module("empty", "user", "before_input", "", false)), "disabled empty slots are ready to fill later");
    check(badModule(module("large", "user", "before_input", QString(65537, 'a'))), "module content is bounded");
    modified = AiPromptPreset::empty(); modified.insert("modules", QJsonArray{module("first", "assistant", "before_input", "answer")});
    check(!AiPromptPreset::compose(modified, "rules", "facts", "JSON", &messages, &error), "examples require an initial user message for role compatibility");
    check(!AiPromptPreset::compose(preset, "rules", QString(2 * 1024 * 1024, 'x'), "JSON", &messages, &error), "oversized composed messages are rejected locally");
    const QString path = temp.path() + "/preset.json";
    check(write(path, QJsonDocument(preset).toJson()) && AiPromptPreset::load(path, &modified, &error) && modified == preset,
        "a preset is loaded from a real file");
    auto changed = preset; changed.insert("name", "External edit"); write(path, QJsonDocument(changed).toJson());
    check(AiPromptPreset::load(path, &modified, &error) && modified.value("name") == "External edit", "a later load observes external changes");
    write(path, "invalid json");
    check(!AiPromptPreset::load(path, &modified, &error), "invalid files are reported without replacement");
    QFile::remove(path);
    check(!AiPromptPreset::load(path, &modified, &error) && !QFile::exists(path), "missing selected files do not silently fall back or regenerate");
    check(!AiPromptPreset::load("relative.json", &modified, &error), "preset paths must be absolute");
    write(path, QByteArray(1024 * 1024 + 1, 'x'));
    check(!AiPromptPreset::load(path, &modified, &error), "file reads are bounded");
    // The resource is pulled in with AiSettingsStore in the application; unit
    // tests exercise the tracked template directly to avoid linker dead stripping.
    QFile example(QStringLiteral(PRESET_SOURCE_DIR) + "/assets/ai/prompt-preset.example.json");
    check(example.open(QIODevice::ReadOnly) && AiPromptPreset::validate(QJsonDocument::fromJson(example.readAll()).object(), &error),
        "the supplied empty template validates before users fill it");
    std::printf("AI prompt preset checks: %d, failures: %d\n", checks, failures); return failures ? 1 : 0;
}
