#include "cameradialog.h"
#include "terminalcore.h"

#include <QBuffer>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QDir>
#include <QFormLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

CameraDialog::CameraDialog(TerminalCore &terminal, const QString &address, QWidget *parent,
    std::shared_ptr<CameraImageProvider> images)
    : QDialog(parent), m_terminal(terminal),
      m_camera(QFileInfo(terminal.worldRoot()).dir().filePath("network"), std::move(images)) {
    setObjectName("cameraDialog");
    setWindowTitle("Camera — Snapshot client");
    resize(940, 700);
    setMinimumSize(680, 540);
    setStyleSheet(
        "QDialog { background: #25262a; color: #dedee5; }"
        "QLabel { color: #dedee5; }"
        "QLineEdit, QTreeWidget { background: #1c1c1f; color: #dedee5; border: 1px solid #484852; padding: 5px; }"
        "QHeaderView::section { background: #36363f; color: #dedee5; padding: 5px; border: 0; }"
        "QTreeWidget::item:selected { background: #45465b; color: white; }"
        "QPushButton { background: #36363f; color: #dedee5; border: 1px solid #4b4b57; border-radius: 4px; padding: 7px 12px; }"
        "QPushButton:hover { background: #454550; }"
        "QPushButton:disabled { color: #777782; background: #2b2b31; }");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    auto *form = new QFormLayout;
    m_address = new QLineEdit(address, this); m_address->setObjectName("cameraAddress");
    m_address->setPlaceholderText("http://camera-address");
    m_user = new QLineEdit(this); m_user->setObjectName("cameraUser");
    m_password = new QLineEdit(this); m_password->setObjectName("cameraPassword");
    m_password->setEchoMode(QLineEdit::Password);
    form->addRow("Address", m_address); form->addRow("User", m_user); form->addRow("Password", m_password);
    layout->addLayout(form);
    auto *connection = new QHBoxLayout;
    m_connect = new QPushButton("Connect", this); m_connect->setObjectName("cameraConnect");
    auto *refresh = new QPushButton("Refresh status", this); refresh->setObjectName("cameraRefresh");
    m_device = new QLabel("Not connected", this); m_device->setObjectName("cameraDevice");
    m_device->setTextFormat(Qt::PlainText); m_device->setWordWrap(true);
    connection->addWidget(m_connect); connection->addWidget(refresh); connection->addWidget(m_device, 1);
    layout->addLayout(connection);
    auto *splitter = new QSplitter(Qt::Vertical, this);
    m_preview = new QLabel("No photograph selected.", splitter); m_preview->setObjectName("cameraPreview");
    m_preview->setAlignment(Qt::AlignCenter); m_preview->setMinimumHeight(160);
    m_preview->setWordWrap(true);
    m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_preview->setStyleSheet("background: #141416; color: #b4b4c0; border: 1px solid #484852;");
    m_history = new QTreeWidget(splitter); m_history->setObjectName("cameraHistory");
    m_history->setHeaderLabels({"Photograph", "Camera time", "Status", "Attempt"});
    m_history->setRootIsDecorated(false); m_history->setMinimumHeight(100);
    m_history->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_history->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    splitter->setStretchFactor(0, 3); splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    auto *actions = new QHBoxLayout;
    m_capture = new QPushButton("Take snapshot", this); m_capture->setObjectName("cameraCapture");
    m_capture->setAutoDefault(false);
    m_retry = new QPushButton("Retry photograph", this); m_retry->setObjectName("cameraRetry");
    m_retry->setAutoDefault(false);
    m_save = new QPushButton("Save image...", this); m_save->setObjectName("cameraSave");
    m_save->setAutoDefault(false);
    auto *close = new QPushButton("Close", this);
    for (auto *button : {m_capture, m_retry, m_save}) actions->addWidget(button);
    actions->addStretch(); actions->addWidget(close); layout->addLayout(actions);
    m_status = new QLabel(this); m_status->setObjectName("cameraStatus");
    m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); m_status->setMinimumHeight(36);
    layout->addWidget(m_status);
    m_capture->setEnabled(false); m_retry->setEnabled(false); m_save->setEnabled(false);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_connect, &QPushButton::clicked, this, [this] { connectCamera(); });
    connect(m_password, &QLineEdit::returnPressed, this, [this] { connectCamera(); });
    connect(refresh, &QPushButton::clicked, this, [this] {
        refreshDevice(); reloadHistory(m_active);
        if (!m_active.isEmpty()) showCapture(m_camera.refresh(m_active));
    });
    connect(m_capture, &QPushButton::clicked, this, [this] {
        m_poll.stop();
        const auto record = m_camera.capture();
        refreshDevice();
        if (!record.id.isEmpty()) reloadHistory(record.id);
        showCapture(record);
    });
    connect(m_retry, &QPushButton::clicked, this, [this] {
        const auto record = m_camera.retry(m_active);
        refreshDevice(); reloadHistory(m_active); showCapture(record);
    });
    connect(m_save, &QPushButton::clicked, this, [this] { saveImage(); });
    connect(m_history, &QTreeWidget::itemSelectionChanged, this, [this] { showSelected(); });
    m_poll.setInterval(750);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        const auto record = m_camera.refresh(m_active);
        reloadHistory(m_active); showCapture(record);
    });
    reloadHistory();
    if (!m_camera.initializationError().isEmpty()) m_status->setText(m_camera.initializationError());
    else m_status->setText("Sign in to request a photograph. Opening this client does not take a picture.");
}

void CameraDialog::connectCamera() {
    if (m_camera.connected()) {
        m_camera.disconnectDevice();
        m_connect->setText("Connect"); m_address->setEnabled(true); m_user->setEnabled(true); m_password->setEnabled(true);
        m_capture->setEnabled(false); m_device->setText("Not connected");
        m_status->setText("Disconnected. Saved photographs remain available.");
        return;
    }
    QString error;
    if (!m_camera.connectDevice(m_address->text(), m_user->text(), m_password->text(), &error)) {
        m_status->setText(error); return;
    }
    m_password->clear(); m_password->setEnabled(false); m_user->setEnabled(false); m_address->setEnabled(false);
    m_connect->setText("Disconnect"); refreshDevice();
    m_status->setText("Connected. No photograph has been requested.");
}

void CameraDialog::refreshDevice() {
    QString error;
    const auto info = m_camera.deviceStatus(&error);
    if (!error.isEmpty()) { m_device->setText(error); m_capture->setEnabled(false); return; }
    m_device->setText(info.value("name").toString() + " | " + info.value("manufacturer").toString()
        + ' ' + info.value("model").toString() + " | Firmware " + info.value("firmware").toString()
        + " | " + info.value("signed_in_as").toString());
    m_capture->setEnabled(info.value("can_capture").toBool() && !info.value("privacy_mode").toBool());
    if (info.value("privacy_mode").toBool()) m_device->setText(m_device->text() + " | Privacy mode active");
}

void CameraDialog::reloadHistory(const QString &selected) {
    const QSignalBlocker blocked(m_history);
    m_history->clear();
    QString error;
    const auto records = m_camera.captures(&error);
    for (const auto &record : records) {
        auto *item = new QTreeWidgetItem(m_history, {record.id, record.snapshot.value("world_time").toString(),
            record.task.state, QString::number(record.attempt)});
        item->setData(0, Qt::UserRole, record.id);
        if (record.id == selected) m_history->setCurrentItem(item);
    }
    if (!error.isEmpty()) m_status->setText(error);
}

void CameraDialog::showSelected() {
    const auto *item = m_history->currentItem();
    if (item) showCapture(m_camera.readCapture(item->data(0, Qt::UserRole).toString()));
}

void CameraDialog::showCapture(const CameraCapture &record) {
    m_poll.stop();
    m_image = {}; m_preview->clear(); m_save->setEnabled(false); m_retry->setEnabled(false);
    if (!record.error.isEmpty()) { m_preview->setText("Photograph unavailable."); m_status->setText(record.error); return; }
    m_active = record.id;
    m_status->setText(record.task.message.isEmpty() ? record.task.state : record.task.message);
    m_preview->setText("No image available. Photograph: " + record.task.state);
    m_retry->setText(record.task.state == "prepared" ? "Submit photograph" : "Retry photograph");
    m_retry->setEnabled(record.task.state == "failed" || record.task.state == "prepared");
    if (record.task.state == "queued" || record.task.state == "generating") m_poll.start();
    if (record.task.state != "completed") return;
    const auto data = m_camera.image(record.id);
    if (!data.error.isEmpty()) { m_status->setText(data.error); return; }
    QBuffer buffer; buffer.setData(data.bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, data.format.toLatin1());
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 32 * 1024 * 1024) {
        m_status->setText("Image dimensions are invalid or too large."); return;
    }
    m_image = reader.read();
    if (m_image.isNull()) { m_status->setText("Photograph could not be decoded."); return; }
    m_save->setEnabled(true); scalePreview();
}

void CameraDialog::scalePreview() {
    if (!m_image.isNull()) m_preview->setPixmap(QPixmap::fromImage(m_image).scaled(
        m_preview->size() - QSize(12, 12), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void CameraDialog::resizeEvent(QResizeEvent *event) { QDialog::resizeEvent(event); scalePreview(); }

void CameraDialog::saveImage() {
    const auto data = m_camera.image(m_active);
    if (!data.error.isEmpty()) { m_status->setText(data.error); return; }
    bool accepted = false;
    const auto filename = QInputDialog::getText(this, "Save photograph", "File in this computer:", QLineEdit::Normal,
        m_terminal.currentPath() + '/' + m_active + '.' + data.format, &accepted);
    if (!accepted || filename.isEmpty()) return;
    const auto result = m_terminal.saveNewBinaryFile(filename, data.bytes);
    m_status->setText(result.error ? result.text : "Photograph saved: " + filename);
}
