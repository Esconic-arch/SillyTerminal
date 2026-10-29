#include "imageviewer.h"

#include <QBuffer>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWheelEvent>

class ImageCanvas : public QGraphicsView {
public:
    explicit ImageCanvas(QWidget *parent) : QGraphicsView(parent) {
        setObjectName("imageCanvas");
        setScene(new QGraphicsScene(this));
        setFrameStyle(QFrame::NoFrame);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorViewCenter);
        setRenderHint(QPainter::SmoothPixmapTransform);
        setFocusPolicy(Qt::StrongFocus);
        QPixmap checker(24, 24);
        checker.fill(QColor("#202226"));
        QPainter painter(&checker);
        painter.fillRect(0, 0, 12, 12, QColor("#292c31"));
        painter.fillRect(12, 12, 12, 12, QColor("#292c31"));
        painter.end();
        setBackgroundBrush(checker);
    }

    void setImage(const QImage &image, bool preserveView) {
        const QPointF center = mapToScene(viewport()->rect().center());
        scene()->clear();
        m_size = image.size();
        if (image.isNull()) {
            scene()->setSceneRect({});
            resetTransform();
            m_fit = true;
        } else {
            scene()->addPixmap(QPixmap::fromImage(image));
            scene()->setSceneRect(QRectF(QPointF(), m_size));
            if (!preserveView || m_fit) fitImage();
            else centerOn(center);
        }
        if (changed) changed();
    }

    void fitImage() {
        if (m_size.isEmpty()) return;
        m_fit = true;
        // Avoid scrollbar-dependent fitInView recursion when the window resizes.
        horizontalScrollBar()->setValue(0);
        verticalScrollBar()->setValue(0);
        const qreal scale = qMin(qreal(32), qMin(qreal(qMax(1, viewport()->width() - 6)) / m_size.width(),
            qreal(qMax(1, viewport()->height() - 6)) / m_size.height()));
        resetTransform();
        QGraphicsView::scale(scale, scale);
        centerOn(QRectF(QPointF(), m_size).center());
        if (changed) changed();
    }

    void actualSize() {
        if (m_size.isEmpty()) return;
        m_fit = false;
        resetTransform();
        centerOn(QRectF(QPointF(), m_size).center());
        if (changed) changed();
    }

    void zoom(qreal factor) {
        if (m_size.isEmpty()) return;
        const qreal current = transform().m11();
        const qreal target = qBound(qreal(0.01), current * factor, qreal(32));
        m_fit = false;
        QGraphicsView::scale(target / current, target / current);
        if (changed) changed();
    }

    bool isFit() const { return m_fit; }
    std::function<void()> changed;
    std::function<void(int)> navigate;

protected:
    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        if (m_fit) fitImage();
    }
    void wheelEvent(QWheelEvent *event) override {
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            if (event->angleDelta().y()) zoom(event->angleDelta().y() > 0 ? 1.25 : 0.8);
        } else if (event->angleDelta().y() && navigate) navigate(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
    }

private:
    QSize m_size;
    bool m_fit = true;
};

bool ImageViewer::supportsFileName(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "png" || suffix == "jpg" || suffix == "jpeg";
}

ImageViewer::ImageViewer(TerminalCore &core, const QStringList &paths, const QFont &font,
    std::function<void()> close, QWidget *parent)
    : QWidget(parent), m_core(core), m_close(std::move(close)) {
    setObjectName("imageViewer");
    setAutoFillBackground(true);
    setStyleSheet(
        "#imageViewer { background: #17191c; color: #dedee5; }"
        "QLabel { color: #dedee5; padding: 5px; }"
        "QPushButton { background: #30343b; color: #dedee5; border: 1px solid #464c56; padding: 5px 8px; }"
        "QPushButton:hover { background: #444b57; }"
        "QPushButton:disabled { color: #6d727b; }"
        "#imageInfo { color: #aab2bf; }"
        "#imageMessage { color: #efb6a5; background: #28211f; }"
        "#imageHelp { background: #252a32; color: #d3dae5; padding: 14px; }"
        "QScrollBar:vertical { background: #202226; width: 10px; }"
        "QScrollBar:horizontal { background: #202226; height: 10px; }"
        "QScrollBar::handle { background: #59616d; min-height: 16px; min-width: 16px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_path = new QLabel(this);
    m_path->setObjectName("imagePath");
    m_path->setTextFormat(Qt::PlainText);
    m_path->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_path);
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(6, 2, 6, 6);
    toolbar->setSpacing(4);
    const auto button = [&](const QString &name, const QString &label, auto action) {
        auto *control = new QPushButton(label, this);
        control->setObjectName(name);
        control->setFocusPolicy(Qt::NoFocus);
        connect(control, &QPushButton::clicked, this, action);
        toolbar->addWidget(control);
        return control;
    };
    m_previous = button("imagePrevious", "Previous", [this] { changeImage(-1); });
    m_next = button("imageNext", "Next", [this] { changeImage(1); });
    button("imageZoomOut", "−", [this] { m_canvas->zoom(0.8); });
    button("imageZoomIn", "+", [this] { m_canvas->zoom(1.25); });
    button("imageFit", "Fit", [this] { m_canvas->fitImage(); });
    button("imageActualSize", "100%", [this] { m_canvas->actualSize(); });
    button("imageReload", "Reload", [this] { load(true); });
    toolbar->addStretch();
    button("imageHelpButton", "Help", [this] { m_help->setVisible(!m_help->isVisible()); });
    button("imageClose", "Close", [this] { m_close(); });
    layout->addLayout(toolbar);
    m_help = new QLabel(
        "feh — local PNG/JPEG viewer\n\n"
        "Right / Space / n: next     Left / Backspace / p: previous\n"
        "Up / Down: zoom in / out     *: 100%     /: fit\n"
        "Ctrl+arrows: pan     Left-drag: pan\n"
        "Wheel: next / previous     Ctrl+wheel: zoom\n"
        "r: reload current file     F1: help     q / Esc: close\n\n"
        "Files are read when opened, selected or reloaded. The directory\n"
        "list is captured at launch; reopen it to include new files.\n"
        "The viewer is read-only. It has no editing, remote URLs,\n"
        "animation, slideshow timer or automatic file watching.", this);
    m_help->setObjectName("imageHelp");
    m_help->setTextFormat(Qt::PlainText);
    m_help->setWordWrap(true);
    m_help->hide();
    layout->addWidget(m_help);
    m_canvas = new ImageCanvas(this);
    m_canvas->installEventFilter(this);
    m_canvas->changed = [this] { updateInfo(); };
    m_canvas->navigate = [this](int direction) { changeImage(direction); };
    layout->addWidget(m_canvas, 1);
    m_message = new QLabel(this);
    m_message->setObjectName("imageMessage");
    m_message->setTextFormat(Qt::PlainText);
    m_message->setWordWrap(true);
    m_message->hide();
    layout->addWidget(m_message);
    m_info = new QLabel(this);
    m_info->setObjectName("imageInfo");
    m_info->setTextFormat(Qt::PlainText);
    m_info->setWordWrap(true);
    layout->addWidget(m_info);
    setAppearance(font);

    for (const auto &path : paths.isEmpty() ? QStringList{core.currentPath()} : paths) {
        const auto directory = core.listDirectory(path);
        if (directory.error.isEmpty()) {
            for (const auto &entry : directory.entries)
                if (!entry.directory && supportsFileName(entry.name))
                    m_files.append((directory.path == "/" ? QString("/") : directory.path + '/') + entry.name);
        } else {
            // Explicit files are checked by content, so a PNG with a different
            // extension can still be opened. Errors remain navigable entries.
            m_files.append(path);
        }
    }
    m_files.removeDuplicates();
    load();
}

void ImageViewer::setAppearance(const QFont &font) { setFont(font); }
void ImageViewer::focusImage() { m_canvas->setFocus(); }

void ImageViewer::updateInfo() {
    if (!m_info) return;
    const QString count = m_files.isEmpty() ? "0 / 0" : QString("%1 / %2").arg(m_index + 1).arg(m_files.size());
    m_info->setText(m_image.isNull() ? count + "    No image loaded    F1 Help    q Close"
        : QString("%1    %2 × %3    %4    %5% %6    Read-only")
            .arg(count).arg(m_image.width()).arg(m_image.height()).arg(m_format.toUpper())
            .arg(qRound(m_canvas->transform().m11() * 100)).arg(m_canvas->isFit() ? "(fit)" : ""));
}

void ImageViewer::load(bool preserveView) {
    const QString path = m_files.value(m_index);
    m_path->setText("feh  —  " + (path.isEmpty() ? QString("No images") : path));
    m_path->setToolTip(path);
    m_previous->setEnabled(m_files.size() > 1);
    m_next->setEnabled(m_files.size() > 1);
    QString error;
    QImage decoded;
    QString format;
    if (m_files.isEmpty()) error = "No PNG/JPEG images found. Open a local file or directory with feh.";
    else {
        auto file = m_core.readBinaryFile(path);
        error = file.error;
        if (error.isEmpty()) {
            m_files[m_index] = file.path;
            QBuffer buffer(&file.bytes);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer);
            reader.setDecideFormatFromContent(true);
            reader.setAutoTransform(true);
            format = QString::fromLatin1(reader.format()).toLower();
            if (format != "png" && format != "jpeg" && format != "jpg")
                error = "Unsupported image data. This viewer supports PNG and JPEG only.";
            else {
                const QSize size = reader.size();
                if (!size.isValid() || size.isEmpty())
                    error = "Cannot read image dimensions: " + reader.errorString();
                else if (size.width() > 16384 || size.height() > 16384
                    || qint64(size.width()) * size.height() > 32000000)
                    error = "Image dimensions exceed the supported limit (16384 per side, 32 megapixels).";
                else {
                    decoded = reader.read();
                    if (decoded.isNull()) error = "Cannot decode image: " + reader.errorString();
                }
            }
        }
    }
    // Never leave an old image visible beneath the name of a failed reload.
    m_image = decoded;
    m_format = format;
    m_canvas->setImage(decoded, preserveView && error.isEmpty());
    m_message->setText(error);
    m_message->setVisible(!error.isEmpty());
    updateInfo();
}

void ImageViewer::changeImage(int direction) {
    if (m_files.size() < 2) return;
    m_index = (m_index + direction + m_files.size()) % m_files.size();
    load();
}

bool ImageViewer::eventFilter(QObject *object, QEvent *event) {
    if (object != m_canvas || event->type() != QEvent::KeyPress) return QWidget::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    const bool control = key->modifiers().testFlag(Qt::ControlModifier);
    const int code = key->key();
    if (code == Qt::Key_Escape && m_help->isVisible()) { m_help->hide(); return true; }
    if (code == Qt::Key_Escape || code == Qt::Key_Q) { m_close(); return true; }
    if (code == Qt::Key_F1) { m_help->setVisible(!m_help->isVisible()); return true; }
    if (control && (code == Qt::Key_Left || code == Qt::Key_Right || code == Qt::Key_Up || code == Qt::Key_Down)) {
        auto *bar = code == Qt::Key_Left || code == Qt::Key_Right ? m_canvas->horizontalScrollBar() : m_canvas->verticalScrollBar();
        bar->setValue(bar->value() + (code == Qt::Key_Left || code == Qt::Key_Up ? -40 : 40));
        return true;
    }
    if (code == Qt::Key_Right || code == Qt::Key_Space || code == Qt::Key_N) { changeImage(1); return true; }
    if (code == Qt::Key_Left || code == Qt::Key_Backspace || code == Qt::Key_P) { changeImage(-1); return true; }
    if (code == Qt::Key_Up) { m_canvas->zoom(1.25); return true; }
    if (code == Qt::Key_Down) { m_canvas->zoom(0.8); return true; }
    if (code == Qt::Key_Asterisk) { m_canvas->actualSize(); return true; }
    if (code == Qt::Key_Slash) { m_canvas->fitImage(); return true; }
    if (code == Qt::Key_R) { load(true); return true; }
    return QWidget::eventFilter(object, event);
}
