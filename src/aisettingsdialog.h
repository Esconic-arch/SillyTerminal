#pragma once

#include "aiservices.h"
#include <QDialog>

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class AiSettingsDialog : public QDialog {
public:
    explicit AiSettingsDialog(QWidget *parent = nullptr, const AiSettingsStore &store = AiSettingsStore{});
private:
    AiSettingsStore m_store;
    AiServiceSettings m_settings;
    AiConnectionClient m_deepSeekClient;
    AiConnectionClient m_comfyUiClient;
    QLineEdit *m_deepSeekUrl;
    QLineEdit *m_deepSeekKey;
    QComboBox *m_model;
    QLineEdit *m_comfyUiUrl;
    QLineEdit *m_comfyUiKey;
    QLineEdit *m_cameraProfile;
    QLineEdit *m_cameraTextPreset;
    QSpinBox *m_timeout;
    QGroupBox *m_deepSeekGroup;
    QGroupBox *m_comfyUiGroup;
    QLabel *m_deepSeekStatus;
    QLabel *m_comfyUiStatus;
    QLabel *m_status;
    QPushButton *m_save;
    bool m_deepSeekBusy = false;
    bool m_comfyUiBusy = false;
    bool m_deepSeekKeyUnavailable = false;
    bool m_comfyUiKeyUnavailable = false;
    void updateBusy();
    void save();
};
