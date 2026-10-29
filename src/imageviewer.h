#pragma once

#include "terminalcore.h"
#include <QImage>
#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class ImageCanvas;

class ImageViewer : public QWidget {
public:
    ImageViewer(TerminalCore &core, const QStringList &paths, const QFont &font,
        std::function<void()> close, QWidget *parent = nullptr);
    static bool supportsFileName(const QString &path);
    void focusImage();
    void setAppearance(const QFont &font);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    TerminalCore &m_core;
    QStringList m_files;
    int m_index = 0;
    ImageCanvas *m_canvas;
    QLabel *m_path;
    QLabel *m_info = nullptr;
    QLabel *m_message;
    QLabel *m_help;
    QPushButton *m_previous;
    QPushButton *m_next;
    QImage m_image;
    QString m_format;
    std::function<void()> m_close;

    void load(bool preserveView = false);
    void changeImage(int direction);
    void updateInfo();
};
