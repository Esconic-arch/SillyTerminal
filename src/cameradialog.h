#pragma once

#include "cameracore.h"
#include <QDialog>
#include <QImage>
#include <QTimer>

class TerminalCore;
class QLineEdit;
class QLabel;
class QPushButton;
class QTreeWidget;

class CameraDialog : public QDialog {
public:
    CameraDialog(TerminalCore &terminal, const QString &address, QWidget *parent = nullptr,
        std::shared_ptr<CameraImageProvider> images = {});
protected:
    void resizeEvent(QResizeEvent *event) override;
private:
    TerminalCore &m_terminal;
    CameraCore m_camera;
    QLineEdit *m_address, *m_user, *m_password;
    QLabel *m_device, *m_status, *m_preview;
    QPushButton *m_connect, *m_capture, *m_retry, *m_save;
    QTreeWidget *m_history;
    QTimer m_poll;
    QImage m_image;
    QString m_active;
    void connectCamera();
    void refreshDevice();
    void reloadHistory(const QString &selected = {});
    void showCapture(const CameraCapture &capture);
    void showSelected();
    void saveImage();
    void scalePreview();
};
