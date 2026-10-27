#pragma once

#include <QFont>

struct TerminalAppearance {
    QFont font;
    int lineSpacing = 115;
    static QFont defaultFont();
    static TerminalAppearance load();
};
