#include "terminalappearance.h"
#include <QFontDatabase>
#include <QSettings>

QFont TerminalAppearance::defaultFont() {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const auto families = QFontDatabase::families();
    for (const QString &family : {QStringLiteral("SF Mono"), QStringLiteral("Menlo"),
             QStringLiteral("Cascadia Mono"), QStringLiteral("Cascadia Code"),
             QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono")}) {
        if (families.contains(family)) { font.setFamily(family); break; }
    }
    font.setPointSize(12);
    return font;
}

TerminalAppearance TerminalAppearance::load() {
    TerminalAppearance result{defaultFont(), 115};
    QSettings settings;
    const QString family = settings.value("appearance/fontFamily").toString();
    if (QFontDatabase::families().contains(family)) result.font.setFamily(family);
    result.font.setPointSize(qBound(8, settings.value("appearance/fontSize", 12).toInt(), 32));
    result.lineSpacing = qBound(100, settings.value("appearance/lineSpacing", 115).toInt(), 160);
    return result;
}
