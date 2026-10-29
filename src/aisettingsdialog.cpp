#include "aisettingsdialog.h"
#include "aicameraprofile.h"
#include "aipromptpreset.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFile>
#include <QFileDialog>
#include <QSaveFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QLabel *statusLabel(const QString &name, QWidget *parent) {
    auto *label = new QLabel(parent);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setMinimumHeight(label->fontMetrics().height());
    return label;
}
void showStatus(QLabel *label, const QString &text, bool success = false) {
    label->setText(text);
    label->setStyleSheet(success ? "color: #a5d6ae;" : "color: #f0a6a9;");
}
QWidget *keyRow(QLineEdit *input, bool *unavailable, QWidget *parent) {
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    input->setEchoMode(QLineEdit::Password);
    input->setMaxLength(2048);
    auto *show = new QPushButton("Show", row);
    show->setCheckable(true);
    show->setFixedWidth(62);
    show->setAutoDefault(false);
    auto *clear = new QPushButton("Clear", row);
    clear->setObjectName(input->objectName() + "Clear");
    clear->setFixedWidth(62);
    clear->setAutoDefault(false);
    QObject::connect(show, &QPushButton::toggled, input, [input, show](bool checked) {
        input->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
        show->setText(checked ? "Hide" : "Show");
    });
    QObject::connect(clear, &QPushButton::clicked, input, [input, unavailable] {
        input->clear(); *unavailable = false;
    });
    QObject::connect(input, &QLineEdit::textEdited, input, [unavailable] { *unavailable = false; });
    layout->addWidget(input, 1);
    layout->addWidget(show);
    layout->addWidget(clear);
    return row;
}
}

AiSettingsDialog::AiSettingsDialog(QWidget *parent, const AiSettingsStore &store)
    : QDialog(parent), m_store(store) {
    setObjectName("aiSettingsDialog");
    setWindowTitle("AI services");
    auto colors = palette();
    colors.setColor(QPalette::PlaceholderText, QColor("#92939f"));
    setPalette(colors);
    setMinimumWidth(640);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    auto *intro = new QLabel("Configure the external services used by SillyTerminal.", this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *tabs = new QTabWidget(this);
    auto *connections = new QWidget(tabs);
    auto *connectionLayout = new QVBoxLayout(connections);
    tabs->addTab(connections, "Connections");
    layout->addWidget(tabs);

    m_deepSeekGroup = new QGroupBox("DeepSeek", this);
    auto *deepSeekForm = new QFormLayout(m_deepSeekGroup);
    deepSeekForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_deepSeekUrl = new QLineEdit(m_deepSeekGroup);
    m_deepSeekUrl->setObjectName("deepSeekUrl");
    m_deepSeekUrl->setPlaceholderText("https://api.deepseek.com");
    m_deepSeekUrl->setMaxLength(2048);
    m_deepSeekUrl->setToolTip("API base URL, optionally ending in /v1. Do not append /chat/completions.");
    deepSeekForm->addRow("API base URL", m_deepSeekUrl);
    m_deepSeekKey = new QLineEdit(m_deepSeekGroup);
    m_deepSeekKey->setObjectName("deepSeekKey");
    deepSeekForm->addRow("API key", keyRow(m_deepSeekKey, &m_deepSeekKeyUnavailable, m_deepSeekGroup));
    m_model = new QComboBox(m_deepSeekGroup);
    m_model->setObjectName("deepSeekModel");
    m_model->setEditable(true);
    m_model->lineEdit()->setMaxLength(256);
    m_model->setToolTip("Check the connection to load available models, or enter a model name.");
    deepSeekForm->addRow("Model", m_model);
    auto *deepSeekTest = new QPushButton("Test connection / load models", m_deepSeekGroup);
    deepSeekTest->setObjectName("testDeepSeek");
    deepSeekTest->setAutoDefault(false);
    deepSeekForm->addRow(QString(), deepSeekTest);
    m_deepSeekStatus = statusLabel("deepSeekStatus", m_deepSeekGroup);
    deepSeekForm->addRow(m_deepSeekStatus);
    connectionLayout->addWidget(m_deepSeekGroup);

    m_comfyUiGroup = new QGroupBox("ComfyUI (self-hosted server)", this);
    auto *comfyUiForm = new QFormLayout(m_comfyUiGroup);
    comfyUiForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_comfyUiUrl = new QLineEdit(m_comfyUiGroup);
    m_comfyUiUrl->setObjectName("comfyUiUrl");
    m_comfyUiUrl->setPlaceholderText("http://127.0.0.1:8188");
    m_comfyUiUrl->setMaxLength(2048);
    comfyUiForm->addRow("Server URL", m_comfyUiUrl);
    m_comfyUiKey = new QLineEdit(m_comfyUiGroup);
    m_comfyUiKey->setObjectName("comfyUiKey");
    m_comfyUiKey->setPlaceholderText("Optional; leave blank for a local server");
    m_comfyUiKey->setToolTip("Optional Bearer token for a server behind an authenticated proxy.");
    comfyUiForm->addRow("Bearer token", keyRow(m_comfyUiKey, &m_comfyUiKeyUnavailable, m_comfyUiGroup));
    auto *comfyUiTest = new QPushButton("Test connection", m_comfyUiGroup);
    comfyUiTest->setObjectName("testComfyUi");
    comfyUiTest->setAutoDefault(false);
    comfyUiForm->addRow(QString(), comfyUiTest);
    m_comfyUiStatus = statusLabel("comfyUiStatus", m_comfyUiGroup);
    comfyUiForm->addRow(m_comfyUiStatus);
    connectionLayout->addWidget(m_comfyUiGroup);

    auto *timeoutRow = new QFormLayout;
    m_timeout = new QSpinBox(this);
    m_timeout->setObjectName("connectionTimeout");
    m_timeout->setRange(1, 120);
    m_timeout->setSuffix(" seconds");
    timeoutRow->addRow("Connection timeout", m_timeout);
    connectionLayout->addLayout(timeoutRow);
    auto *generation = new QWidget(tabs);
    auto *generationLayout = new QVBoxLayout(generation);
    tabs->addTab(generation, "Camera generation");
    auto *explanation = new QLabel("Use a camera profile to bind a ComfyUI API workflow to the snapshot's prompt, size and seed. "
        "It can also include visual style guidance for DeepSeek.\n\n"
        "Export the example and adapt it to a workflow tested in your ComfyUI, including your checkpoint name. "
        "Snapshot reference assets are not supported yet.", generation);
    explanation->setWordWrap(true);
    generationLayout->addWidget(explanation);
    auto *profileRow = new QHBoxLayout;
    m_cameraProfile = new QLineEdit(generation);
    m_cameraProfile->setObjectName("cameraGenerationProfile");
    m_cameraProfile->setPlaceholderText("Camera profile JSON file");
    auto *browse = new QPushButton("Browse...", generation);
    browse->setAutoDefault(false);
    profileRow->addWidget(m_cameraProfile, 1); profileRow->addWidget(browse);
    generationLayout->addLayout(profileRow);
    auto *profileButtons = new QHBoxLayout;
    auto *exportExample = new QPushButton("Export example...", generation);
    auto *verify = new QPushButton("Check profile", generation);
    exportExample->setAutoDefault(false); verify->setAutoDefault(false);
    profileButtons->addWidget(exportExample); profileButtons->addWidget(verify); profileButtons->addStretch();
    generationLayout->addLayout(profileButtons);
    auto *profileStatus = statusLabel("cameraProfileStatus", generation);
    generationLayout->addWidget(profileStatus);
    generationLayout->addStretch();
    auto *generationNote = new QLabel("Taking a snapshot sends its visible scene to DeepSeek and submits a ComfyUI image job. "
        "The task freezes the selected profile; editing the file affects new captures. "
        "Checking this profile validates its local format only.", generation);
    generationNote->setWordWrap(true);
    generationLayout->addWidget(generationNote);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Choose camera profile", m_cameraProfile->text(), "JSON files (*.json)");
        if (!path.isEmpty()) m_cameraProfile->setText(path);
    });
    connect(verify, &QPushButton::clicked, this, [this, profileStatus] {
        QJsonObject profile; QString error;
        if (AiCameraProfile::load(m_cameraProfile->text(), &profile, &error))
            showStatus(profileStatus, "Profile format is valid. Verify the workflow in your ComfyUI before taking a snapshot.", true);
        else showStatus(profileStatus, error);
    });
    connect(exportExample, &QPushButton::clicked, this, [this, profileStatus] {
        const auto path = QFileDialog::getSaveFileName(this, "Export camera profile example", "camera-profile.json", "JSON files (*.json)");
        if (path.isEmpty()) return;
        QFile input(":/ai/camera-profile.example.json");
        QSaveFile output(path); output.setDirectWriteFallback(false);
        if (!input.open(QIODevice::ReadOnly)) { showStatus(profileStatus, "Cannot read the bundled example."); return; }
        const auto bytes = input.readAll();
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            showStatus(profileStatus, "Cannot export the example file."); return;
        }
        m_cameraProfile->setText(path);
        showStatus(profileStatus, "Example exported. Replace its checkpoint and adapt the workflow and bindings before saving.", true);
    });
    connect(m_cameraProfile, &QLineEdit::textChanged, profileStatus, &QLabel::clear);
    auto *presetPage = new QWidget(tabs);
    auto *presetLayout = new QVBoxLayout(presetPage);
    tabs->addTab(presetPage, "Text preset");
    auto *presetExplanation = new QLabel("Optional prompt modules for the camera's DeepSeek description step. "
        "Leave this empty to use the built-in camera instructions.\n\n"
        "Export the empty template, fill the modules you need and enable them. "
        "Native preset v1 supports system guidance and user/assistant examples around the snapshot. "
        "SillyTavern presets, macros and extension scripts require a separate importer.", presetPage);
    presetExplanation->setWordWrap(true);
    presetLayout->addWidget(presetExplanation);
    auto *presetRow = new QHBoxLayout;
    m_cameraTextPreset = new QLineEdit(presetPage);
    m_cameraTextPreset->setObjectName("cameraTextPreset");
    m_cameraTextPreset->setPlaceholderText("Built-in camera instructions (no extra preset)");
    auto *browsePreset = new QPushButton("Browse...", presetPage); browsePreset->setAutoDefault(false);
    presetRow->addWidget(m_cameraTextPreset, 1); presetRow->addWidget(browsePreset); presetLayout->addLayout(presetRow);
    auto *presetButtons = new QHBoxLayout;
    auto *exportPreset = new QPushButton("Export template...", presetPage);
    auto *previewPreset = new QPushButton("Check / preview", presetPage);
    previewPreset->setObjectName("previewCameraTextPreset");
    auto *clearPreset = new QPushButton("Use built-in", presetPage); clearPreset->setObjectName("clearCameraTextPreset");
    for (auto *button : {exportPreset, previewPreset, clearPreset}) { button->setAutoDefault(false); presetButtons->addWidget(button); }
    presetButtons->addStretch(); presetLayout->addLayout(presetButtons);
    auto *presetStatus = statusLabel("cameraTextPresetStatus", presetPage); presetLayout->addWidget(presetStatus);
    presetLayout->addStretch();
    auto *presetNote = new QLabel("Preview checks local files only and uses example input. "
        "Save applies your selection to new captures. Each capture keeps its preset and assembled messages for retries. "
        "Deleting or changing the source file affects new captures; an invalid selected preset stops generation before an API request.", presetPage);
    presetNote->setWordWrap(true); presetLayout->addWidget(presetNote);
    connect(browsePreset, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Choose camera text preset", m_cameraTextPreset->text(), "JSON files (*.json)");
        if (!path.isEmpty()) m_cameraTextPreset->setText(path);
    });
    connect(clearPreset, &QPushButton::clicked, m_cameraTextPreset, &QLineEdit::clear);
    connect(m_cameraTextPreset, &QLineEdit::textChanged, presetStatus, &QLabel::clear);
    connect(exportPreset, &QPushButton::clicked, this, [this, presetStatus] {
        const auto path = QFileDialog::getSaveFileName(this, "Export empty text preset", "camera-text-preset.json", "JSON files (*.json)");
        if (path.isEmpty()) return;
        QFile input(":/ai/prompt-preset.example.json"); QSaveFile output(path); output.setDirectWriteFallback(false);
        if (!input.open(QIODevice::ReadOnly)) { showStatus(presetStatus, "Cannot read the bundled preset template."); return; }
        const auto bytes = input.readAll();
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            showStatus(presetStatus, "Cannot export the preset template."); return;
        }
        m_cameraTextPreset->setText(path);
        showStatus(presetStatus, "Empty template exported. Fill and enable modules when ready, then Save.", true);
    });
    connect(previewPreset, &QPushButton::clicked, this, [this, presetStatus] {
        QJsonObject preset, profile; QJsonArray messages; QString error;
        if (!AiPromptPreset::load(m_cameraTextPreset->text().trimmed(), &preset, &error)
            || (!m_cameraProfile->text().trimmed().isEmpty() && !AiCameraProfile::load(m_cameraProfile->text().trimmed(), &profile, &error))
            || !AiCameraProfile::messages(profile, preset, QJsonObject{{"preview", "Example only. The complete frozen snapshot is supplied at capture time."}}, &messages, &error)) {
            showStatus(presetStatus, error); return;
        }
        showStatus(presetStatus, "Preset is valid. Preview uses example input; no API request was sent.", true);
        auto *preview = new QDialog(this); preview->setObjectName("cameraPresetPreview");
        preview->setAttribute(Qt::WA_DeleteOnClose); preview->setWindowTitle("Camera text messages - example input");
        preview->resize(760, 480); preview->setModal(true);
        auto *previewLayout = new QVBoxLayout(preview);
        auto *text = new QPlainTextEdit(preview); text->setObjectName("cameraPresetMessages"); text->setReadOnly(true);
        text->setPlainText(QString::fromUtf8(QJsonDocument(messages).toJson())); previewLayout->addWidget(text);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, preview);
        connect(buttons, &QDialogButtonBox::rejected, preview, &QDialog::reject); previewLayout->addWidget(buttons);
        preview->show();
    });
    auto *note = new QLabel("Tests check API access only; they do not generate text or images.\n"
        "Keys are saved in Windows Credential Manager or macOS Keychain.\n"
        "These settings belong to the application, outside the simulated computer.", this);
    note->setWordWrap(true);
    note->setStyleSheet("color: #92939f;");
    layout->addWidget(note);
    m_status = statusLabel("aiSettingsStatus", this);
    layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_save = buttons->button(QDialogButtonBox::Save);
    m_save->setObjectName("saveAiSettings");
    connect(buttons, &QDialogButtonBox::accepted, this, &AiSettingsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    QString error;
    const bool loaded = m_store.load(&m_settings, &error);
    m_deepSeekUrl->setText(m_settings.deepSeekUrl);
    m_comfyUiUrl->setText(m_settings.comfyUiUrl);
    m_model->setEditText(m_settings.deepSeekModel);
    m_timeout->setValue(m_settings.timeoutSeconds);
    m_cameraProfile->setText(m_settings.cameraProfilePath);
    m_cameraTextPreset->setText(m_settings.cameraTextPresetPath);
    QString key;
    if (!loaded) {
        showStatus(m_status, error);
        m_save->setEnabled(false);
        m_deepSeekGroup->setEnabled(false);
        m_comfyUiGroup->setEnabled(false);
        m_timeout->setEnabled(false);
        generation->setEnabled(false);
        presetPage->setEnabled(false);
    } else {
        if (m_store.readKey(m_settings.deepSeekKeyRef, &key, &error)) m_deepSeekKey->setText(key);
        else {
            m_deepSeekKeyUnavailable = true;
            showStatus(m_deepSeekStatus, "Stored key could not be read. Re-enter it or choose Clear.");
        }
        if (m_store.readKey(m_settings.comfyUiKeyRef, &key, &error)) m_comfyUiKey->setText(key);
        else {
            m_comfyUiKeyUnavailable = true;
            showStatus(m_comfyUiStatus, "Stored token could not be read. Re-enter it or choose Clear.");
        }
    }

    const auto deepSeekChanged = [this] { m_deepSeekStatus->clear(); };
    connect(m_deepSeekUrl, &QLineEdit::textChanged, this, deepSeekChanged);
    connect(m_deepSeekKey, &QLineEdit::textChanged, this, deepSeekChanged);
    connect(m_model, &QComboBox::currentTextChanged, this, deepSeekChanged);
    const auto comfyUiChanged = [this] { m_comfyUiStatus->clear(); };
    connect(m_comfyUiUrl, &QLineEdit::textChanged, this, comfyUiChanged);
    connect(m_comfyUiKey, &QLineEdit::textChanged, this, comfyUiChanged);
    connect(m_timeout, &QSpinBox::valueChanged, this, [this] {
        m_deepSeekStatus->clear(); m_comfyUiStatus->clear();
    });
    connect(deepSeekTest, &QPushButton::clicked, this, [this] {
        m_deepSeekBusy = true; updateBusy();
        showStatus(m_deepSeekStatus, "Checking connection...", true);
        m_deepSeekClient.checkDeepSeek(m_deepSeekUrl->text(), m_deepSeekKey->text(), m_timeout->value() * 1000);
    });
    connect(comfyUiTest, &QPushButton::clicked, this, [this] {
        m_comfyUiBusy = true; updateBusy();
        showStatus(m_comfyUiStatus, "Checking connection...", true);
        m_comfyUiClient.checkComfyUi(m_comfyUiUrl->text(), m_comfyUiKey->text(), m_timeout->value() * 1000);
    });
    m_deepSeekClient.onFinished = [this](const AiConnectionResult &result) {
        m_deepSeekBusy = false; updateBusy();
        if (result.success) {
            const QString selected = m_model->currentText();
            const QSignalBlocker block(m_model);
            m_model->clear(); m_model->addItems(result.models); m_model->setEditText(selected);
            if (!result.models.contains(selected)) {
                showStatus(m_deepSeekStatus, "Connected. Select a model from the list; the current name was not returned.");
                return;
            }
        }
        showStatus(m_deepSeekStatus, result.message, result.success);
    };
    m_comfyUiClient.onFinished = [this](const AiConnectionResult &result) {
        m_comfyUiBusy = false; updateBusy();
        showStatus(m_comfyUiStatus, result.message, result.success);
    };
}

void AiSettingsDialog::updateBusy() {
    m_deepSeekGroup->setEnabled(!m_deepSeekBusy);
    m_comfyUiGroup->setEnabled(!m_comfyUiBusy);
    m_timeout->setEnabled(!m_deepSeekBusy && !m_comfyUiBusy);
    m_save->setEnabled(!m_deepSeekBusy && !m_comfyUiBusy);
}

void AiSettingsDialog::save() {
    if (m_deepSeekKeyUnavailable || m_comfyUiKeyUnavailable) {
        showStatus(m_status, "Re-enter the unavailable saved key, or choose Clear to remove it.");
        return;
    }
    auto settings = m_settings;
    settings.deepSeekUrl = m_deepSeekUrl->text();
    settings.deepSeekModel = m_model->currentText();
    settings.comfyUiUrl = m_comfyUiUrl->text();
    settings.timeoutSeconds = m_timeout->value();
    settings.cameraProfilePath = m_cameraProfile->text().trimmed();
    settings.cameraTextPresetPath = m_cameraTextPreset->text().trimmed();
    QString error;
    if (!m_store.save(&settings, m_deepSeekKey->text(), m_comfyUiKey->text(), &error)) {
        showStatus(m_status, error); return;
    }
    accept();
}
