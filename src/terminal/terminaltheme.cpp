#include "terminaltheme.h"

QColor TerminalTheme::ansi256Color(int index) const
{
    if (index < 16) {
        return ansiColors[index];
    }

    if (index < 232) {
        int n = index - 16;
        int r = (n / 36) ? (n / 36) * 40 + 55 : 0;
        int g = ((n % 36) / 6) ? ((n % 36) / 6) * 40 + 55 : 0;
        int b = (n % 6) ? (n % 6) * 40 + 55 : 0;
        return QColor(r, g, b);
    }

    int gray = (index - 232) * 10 + 8;
    return QColor(gray, gray, gray);
}
