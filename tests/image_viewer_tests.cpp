#include "imageviewer.h"
#include "mcfilemanager.h"
#include "terminalwidget.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QWheelEvent>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
void key(QWidget *target, int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::KeyPress, code, modifiers);
    QCoreApplication::sendEvent(target, &event);
}
QByteArray encoded(const QImage &image, const char *format = "PNG") {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format);
    return bytes;
}
QImage displayed(QGraphicsView *canvas) {
    const auto items = canvas->scene()->items();
    return items.isEmpty() ? QImage() : static_cast<QGraphicsPixmapItem *>(items.first())->pixmap().toImage();
}
bool select(QTreeWidget *panel, const QString &name) {
    for (int row = 0; row < panel->topLevelItemCount(); ++row) {
        auto *item = panel->topLevelItem(row);
        if (item->data(0, Qt::UserRole).toString() != name) continue;
        panel->setCurrentItem(item);
        return true;
    }
    return false;
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    QCoreApplication::setOrganizationName("SillyTerminalTests");
    QCoreApplication::setApplicationName("ImageViewer");
    QTemporaryDir temp;
    if (!temp.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path() + "/settings");
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
    };
    const QString world = temp.path() + "/world";
    TerminalCore core(world);
    core.createDirectory("pictures");
    core.createDirectory("empty");
    core.createDirectory("pictures/nested");
    const QString name = QString::fromUtf8("01-接线图 'draft' & copy.png");
    const QString path = "/home/guest/pictures/" + name;
    QImage diagram(1200, 800, QImage::Format_ARGB32);
    diagram.fill(QColor("#edf1f5"));
    {
        QPainter painter(&diagram);
        painter.fillRect(0, 0, 1200, 115, QColor("#243b53"));
        painter.setPen(Qt::white);
        painter.setFont(QFont("Consolas", 28, QFont::Bold));
        painter.drawText(42, 72, "SERVICE CABINET / WIRING PLAN");
        painter.setFont(QFont("Consolas", 15));
        painter.setPen(QColor("#718096"));
        for (int x = 40; x < 1200; x += 40) painter.drawLine(x, 150, x, 740);
        for (int y = 150; y < 760; y += 40) painter.drawLine(40, y, 1160, y);
        for (int i = 0; i < 3; ++i) {
            const QRect device(90 + i * 370, 260, 280, 210);
            painter.fillRect(device, QColor(i == 1 ? "#d0e9e2" : "#d5e0ed"));
            painter.setPen(QPen(QColor("#334e68"), 3));
            painter.drawRect(device);
            painter.drawText(device, Qt::AlignCenter, QStringList{"ROUTER\nLAN 01", "SWITCH\nPORT 08", "CAMERA\nFIXED VIEW"}.at(i));
        }
        painter.setPen(QPen(QColor("#168b79"), 7));
        painter.drawLine(370, 365, 460, 365);
        painter.drawLine(740, 365, 830, 365);
        painter.setPen(QColor("#486581"));
        painter.drawText(90, 640, "PNG / 1200 x 800 / IMAGE VIEWER TEST FIXTURE");
    }
    const QByteArray png = encoded(diagram);
    check(!core.saveNewBinaryFile(path, png).error, "camera-style binary export creates a real image file");
    QImage small(480, 320, QImage::Format_RGB32);
    small.fill(QColor("#bc6c25"));
    check(!core.saveNewBinaryFile("pictures/second.JPG", encoded(small, "JPEG")).error, "JPEG fixture is saved");
    core.saveNewBinaryFile("pictures/nested/ignored.png", png);
    core.saveFile("pictures/readme.txt", "ordinary text\n");
    check(core.readBinaryFile(path).bytes == png, "bounded binary API reads exported bytes without conversion");
    check(!core.readBinaryFile("/C:/Windows/image.png").error.isEmpty()
        && !core.readBinaryFile("/../../outside.png").error.isEmpty(), "binary reads stay within the world path rules");
    QFile oversized(world + "/home/guest/large.png");
    check(oversized.open(QIODevice::WriteOnly) && oversized.resize(20 * 1024 * 1024 + 1), "oversized file fixture is created");
    oversized.close();
    check(core.readBinaryFile("large.png").bytes.isEmpty() && !core.readBinaryFile("large.png").error.isEmpty(),
        "oversized data is rejected before loading it into memory");
    const auto launch = core.execute("feh pictures");
    check(launch.openImageViewer && launch.imagePaths == QStringList{"/home/guest/pictures"}, "feh command resolves arguments");
    check(core.execute("feh").imagePaths == QStringList{"/home/guest"}, "no arguments selects the current directory");
    check(core.execute("feh --recursive pictures").error && core.execute("feh https://example.invalid/a.png").error,
        "unsupported options and remote URLs fail explicitly");
    check(!core.execute("feh --help").openImageViewer && core.execute("feh --help").text.contains("PNG/JPEG"), "feh has command help");
    check(core.complete("fe").replacement == "feh ", "feh participates in command completion");
    check(core.execute("feh pictures > no-image-output").error && core.execute("echo test | feh").error
        && core.readTextFile("no-image-output").error, "interactive redirection and pipes fail before creating output");
    const auto historyBefore = core.history();
    bool closed = false;
    ImageViewer viewer(core, launch.imagePaths, QFont("Consolas", 10), [&] { closed = true; });
    viewer.resize(900, 600);
    viewer.show();
    viewer.focusImage();
    QCoreApplication::processEvents();
    auto *canvas = viewer.findChild<QGraphicsView *>("imageCanvas");
    auto *info = viewer.findChild<QLabel *>("imageInfo");
    auto *message = viewer.findChild<QLabel *>("imageMessage");
    check(displayed(canvas).size() == diagram.size() && info->text().startsWith("1 / 2") && message->isHidden(),
        "directory opens actual image bytes, excludes text and subdirectories");
    check(canvas->transform().m11() < 1 && info->text().contains("fit"), "large images initially fit the viewport");
    key(canvas, Qt::Key_Asterisk);
    QCoreApplication::processEvents();
    check(std::abs(canvas->transform().m11() - 1) < 0.001, "asterisk selects original image size");
    const int beforePan = canvas->horizontalScrollBar()->value();
    key(canvas, Qt::Key_Right, Qt::ControlModifier);
    check(canvas->horizontalScrollBar()->value() > beforePan && info->text().startsWith("1 / 2"), "Ctrl+arrows pan without changing the selected image");
    key(canvas, Qt::Key_Up);
    check(canvas->transform().m11() > 1, "Up zooms in");
    key(canvas, Qt::Key_Down);
    check(std::abs(canvas->transform().m11() - 1) < 0.001, "Down reverses one zoom step");
    const int beforeDrag = canvas->horizontalScrollBar()->value();
    const QPoint start(220, 180), end(150, 180);
    QMouseEvent press(QEvent::MouseButtonPress, start, canvas->viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, end, canvas->viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, end, canvas->viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas->viewport(), &press);
    QCoreApplication::sendEvent(canvas->viewport(), &move);
    QCoreApplication::sendEvent(canvas->viewport(), &release);
    check(canvas->horizontalScrollBar()->value() != beforeDrag, "left drag pans the image");
    QWheelEvent wheel(QPointF(200, 150), canvas->viewport()->mapToGlobal(QPoint(200, 150)), {}, QPoint(0, 120),
        Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(canvas->viewport(), &wheel);
    check(canvas->transform().m11() > 1, "Ctrl+wheel zooms the image");
    key(canvas, Qt::Key_Slash);
    QCoreApplication::processEvents();
    const qreal fitBeforeResize = canvas->transform().m11();
    viewer.resize(600, 420);
    QCoreApplication::processEvents();
    check(canvas->transform().m11() < fitBeforeResize, "fit mode follows a resized viewport");
    viewer.resize(900, 600);
    QCoreApplication::processEvents();
    viewer.grab().save(QCoreApplication::applicationDirPath() + "/image-viewer-preview.png");
    viewer.resize(560, 360);
    QCoreApplication::processEvents();
    auto *closeButton = viewer.findChild<QPushButton *>("imageClose");
    check(closeButton->geometry().right() < viewer.width(), "close control stays reachable at the minimum terminal size");
    viewer.grab().save(QCoreApplication::applicationDirPath() + "/image-viewer-narrow-preview.png");
    viewer.resize(900, 600);
    QCoreApplication::processEvents();
    key(canvas, Qt::Key_F1);
    check(viewer.findChild<QLabel *>("imageHelp")->isVisible(), "F1 shows viewer help");
    key(canvas, Qt::Key_Escape);
    check(!closed && viewer.findChild<QLabel *>("imageHelp")->isHidden(), "Escape dismisses help before quitting");
    key(canvas, Qt::Key_Right);
    check(displayed(canvas).size() == small.size() && info->text().startsWith("2 / 2"), "next reads the JPEG");
    key(canvas, Qt::Key_Right);
    check(info->text().startsWith("1 / 2"), "next wraps the file list");
    key(canvas, Qt::Key_Left);
    check(info->text().startsWith("2 / 2"), "previous wraps the file list");
    key(canvas, Qt::Key_Right);

    const QString native = world + path;
    QImage external(90, 80, QImage::Format_ARGB32);
    external.fill(Qt::green);
    check(external.save(native, "PNG"), "host editor can replace the same world file");
    check(displayed(canvas).size() == diagram.size(), "open view retains its snapshot until reload");
    key(canvas, Qt::Key_R);
    check(displayed(canvas).size() == external.size(), "reload rereads external changes from disk");
    key(canvas, Qt::Key_Asterisk);
    key(canvas, Qt::Key_Up);
    key(canvas, Qt::Key_R);
    check(std::abs(canvas->transform().m11() - 1.25) < 0.001, "reload preserves manual zoom");
    const QByteArray externalBytes = core.readBinaryFile(path).bytes;
    check(QFile::remove(native), "host can delete the open image");
    key(canvas, Qt::Key_R);
    check(displayed(canvas).isNull() && message->isVisible() && message->text().contains("no such file"),
        "deleted image clears stale pixels and displays the real error");
    check(external.save(native, "PNG"), "host can restore the image");
    key(canvas, Qt::Key_R);
    check(displayed(canvas).size() == external.size() && message->isHidden(), "reload recovers after file restoration");
    core.saveFile(path, "not a PNG anymore");
    key(canvas, Qt::Key_R);
    check(displayed(canvas).isNull() && message->isVisible(), "invalid replacement data is never shown as the previous image");
    QFile restoreFile(native);
    check(restoreFile.open(QIODevice::WriteOnly) && restoreFile.write(externalBytes) == externalBytes.size(), "fixture is restored as a real file");
    restoreFile.close();
    key(canvas, Qt::Key_R);
    core.saveNewBinaryFile("pictures/new.png", png);
    key(canvas, Qt::Key_R);
    check(info->text().startsWith("1 / 2"), "reload current image does not silently change the launch-time file list");
    ImageViewer reopened(core, {"/home/guest/pictures"}, viewer.font(), [] {});
    check(reopened.findChild<QLabel *>("imageInfo")->text().startsWith("1 / 3"), "reopening a directory discovers newly added files");
    check(core.readBinaryFile(path).bytes == externalBytes && core.history() == historyBefore, "viewing does not rewrite files or shell history");
    QImage wide(16385, 1, QImage::Format_ARGB32);
    wide.fill(Qt::red);
    core.saveNewBinaryFile("too-wide.png", encoded(wide));
    ImageViewer oversizedImage(core, {"/home/guest/too-wide.png"}, viewer.font(), [] {});
    check(oversizedImage.findChild<QLabel *>("imageMessage")->text().contains("dimensions"), "oversized decoded dimensions fail before image allocation");
    ImageViewer empty(core, {"/home/guest/empty"}, viewer.font(), [] {});
    check(empty.findChild<QLabel *>("imageInfo")->text().startsWith("0 / 0")
        && !empty.findChild<QPushButton *>("imageNext")->isEnabled(), "empty directory has an honest empty state");
    core.saveNewBinaryFile("renamed.data", png);
    ImageViewer renamed(core, {"/home/guest/renamed.data"}, viewer.font(), [] {});
    check(displayed(renamed.findChild<QGraphicsView *>("imageCanvas")).size() == diagram.size(), "explicit files are decoded by contents rather than extension");
    ImageViewer mixed(core, {"/home/guest/missing.png", "/home/guest/pictures/second.JPG"}, viewer.font(), [] {});
    auto *mixedCanvas = mixed.findChild<QGraphicsView *>("imageCanvas");
    check(displayed(mixedCanvas).isNull(), "missing explicit image reports failure");
    key(mixedCanvas, Qt::Key_Right);
    check(displayed(mixedCanvas).size() == small.size(), "failed entries do not trap navigation to a valid image");
    QImage tiny(1, 1, QImage::Format_ARGB32);
    tiny.fill(Qt::transparent);
    core.saveNewBinaryFile("tiny.png", encoded(tiny));
    ImageViewer tinyViewer(core, {"/home/guest/tiny.png"}, viewer.font(), [] {});
    tinyViewer.resize(900, 600);
    tinyViewer.show();
    QCoreApplication::processEvents();
    auto *tinyCanvas = tinyViewer.findChild<QGraphicsView *>("imageCanvas");
    const qreal tinyScale = tinyCanvas->transform().m11();
    key(tinyCanvas, Qt::Key_Up);
    check(tinyScale <= 32 && tinyCanvas->transform().m11() >= tinyScale,
        "fit respects maximum zoom and zoom-in never shrinks a tiny image");
    tinyViewer.hide();
    key(canvas, Qt::Key_Q);
    check(closed, "q closes the viewer");
    viewer.hide();

    TerminalWidget terminal(nullptr, world);
    terminal.resize(900, 600);
    terminal.show();
    terminal.runCommand("echo preserved transcript");
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [] { std::fprintf(stderr, "FAIL: image program return timed out\n"); std::abort(); });
    watchdog.start(8000);
    QTimer::singleShot(0, [&] {
        auto *image = terminal.findChild<QWidget *>("imageViewer");
        check(image && image->isVisible(), "terminal starts the standalone image program");
        if (image) key(image->findChild<QGraphicsView *>("imageCanvas"), Qt::Key_Q);
    });
    terminal.runCommand("feh pictures/second.JPG");
    check(!terminal.findChild<QWidget *>("imageViewer") && terminal.toPlainText().endsWith("guest@silly:~$ "), "viewer returns to the terminal prompt");
    QTimer::singleShot(0, [&] {
        auto *mc = terminal.findChild<QWidget *>("mcFileManager");
        if (!mc) return;
        auto *panel = mc->findChild<QTreeWidget *>("mcLeftPanel");
        select(panel, name);
        key(panel, Qt::Key_Insert);
        select(panel, name);
        for (const int action : {Qt::Key_Return, Qt::Key_F3}) {
            QTimer::singleShot(0, [&] {
                auto *image = terminal.findChild<QWidget *>("imageViewer");
                check(image && image->isVisible(), "mc opens its image association");
                if (image) key(image->findChild<QGraphicsView *>("imageCanvas"), Qt::Key_Escape);
            });
            key(panel, action);
            check(mc->isVisible() && panel->currentItem()->data(0, Qt::UserRole).toString() == name
                && panel->currentItem()->text(0).startsWith('*'), "closing image restores mc selection and marks");
        }
        key(panel, Qt::Key_F10);
    });
    terminal.runCommand("mc pictures");
    watchdog.stop();
    check(!terminal.findChild<QWidget *>("imageViewer") && !terminal.findChild<QWidget *>("mcFileManager")
        && terminal.toPlainText().contains("preserved transcript") && terminal.toPlainText().endsWith("guest@silly:~$ "),
        "mc and image viewer both return to the preserved transcript");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
