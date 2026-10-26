#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

struct ThemePalette
{
    bool dark = true;
    QColor bg, surface, surface2, border;
    QColor text, textMuted;
    QColor accent, accentHover, onAccent, accentSoft;
    QColor danger, dangerSoft, warn, grid;
};

namespace Theme
{
enum class Mode { Dark, Light };

void setMode(Mode mode);
Mode mode();
const ThemePalette &palette();

QString styleSheet();         // application-wide QSS for the current mode
QString css(const QColor &c); // "#rrggbb" or "rgba(r,g,b,a)"

QPixmap appPixmap(int size);
QIcon appIcon();
}
