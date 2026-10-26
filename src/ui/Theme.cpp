#include "Theme.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace {

Theme::Mode g_mode = Theme::Mode::Dark;

ThemePalette makeDark()
{
    ThemePalette p;
    p.dark = true;
    p.bg = QColor("#0e1116");
    p.surface = QColor("#151a21");
    p.surface2 = QColor("#1c222b");
    p.border = QColor("#29313c");
    p.text = QColor("#e7eaef");
    p.textMuted = QColor("#8a94a4");
    p.accent = QColor("#38b2c4");
    p.accentHover = QColor("#4cc4d6");
    p.onAccent = QColor("#062229");
    p.accentSoft = QColor(56, 178, 196, 40);
    p.danger = QColor("#f06363");
    p.dangerSoft = QColor(240, 99, 99, 38);
    p.warn = QColor("#e9b54a");
    p.grid = QColor("#212833");
    return p;
}

ThemePalette makeLight()
{
    ThemePalette p;
    p.dark = false;
    p.bg = QColor("#f5f6f8");
    p.surface = QColor("#ffffff");
    p.surface2 = QColor("#f0f2f5");
    p.border = QColor("#e0e4ea");
    p.text = QColor("#151a21");
    p.textMuted = QColor("#647084");
    p.accent = QColor("#0e8ea2");
    p.accentHover = QColor("#0b7a8b");
    p.onAccent = QColor("#ffffff");
    p.accentSoft = QColor(14, 142, 162, 30);
    p.danger = QColor("#d94848");
    p.dangerSoft = QColor(217, 72, 72, 26);
    p.warn = QColor("#b7791f");
    p.grid = QColor("#edf0f4");
    return p;
}

const char *kTemplate = R"QSS(
QWidget { color: @text; }
QMainWindow, #Central, #Main, #Page { background: @bg; }
#Sidebar { background: @surf; border-right: 1px solid @border; }
#Brand { font-size: 16px; font-weight: 600; }
#Hero { font-size: 30px; font-weight: 700; }
#Title { font-size: 22px; font-weight: 600; }
#Subtitle, #Muted { color: @muted; }
#Small { color: @muted; font-size: 11px; }
#Warn { color: @warn; }
#SectionLabel { color: @muted; font-size: 11px; font-weight: 600; }

#NewBtn { background: @accent; color: @onAccent; border: none; border-radius: 10px;
          padding: 9px 12px; font-weight: 600; text-align: left; }
#NewBtn:hover { background: @accentHover; }
#FlatBtn { background: transparent; border: none; border-radius: 8px; padding: 7px 10px;
           color: @muted; text-align: left; }
#FlatBtn:hover { background: @surf2; color: @text; }

QLineEdit { background: @surf2; border: 1px solid @border; border-radius: 8px; padding: 7px 10px;
            selection-background-color: @accent; }
QLineEdit:focus { border-color: @accent; }

#SessionList { background: transparent; border: none; outline: 0; }

#KpiCard, #Card, #ChartCard { background: @surf; border: 1px solid @border; border-radius: 12px; }
#ParamCard { background: @surf; border: 1px solid @border; border-radius: 12px; }
#ParamCard:hover { border-color: @accent; }
#KpiCaption { color: @muted; font-size: 11px; font-weight: 600; }
#KpiValue { font-size: 22px; font-weight: 600; }
#KpiValue[danger="true"] { color: @danger; }
#KpiHint { color: @muted; font-size: 12px; }
#CardName { font-weight: 600; }
#CardValue { font-size: 20px; font-weight: 600; }
#Badge { background: @surf2; color: @muted; border-radius: 9px; padding: 2px 9px; font-size: 11px; }
#BadgeDanger { background: @dangerSoft; color: @danger; border-radius: 9px; padding: 2px 9px;
               font-size: 11px; font-weight: 600; }

QPushButton { background: @surf2; border: 1px solid @border; border-radius: 8px; padding: 7px 14px; }
QPushButton:hover { border-color: @accent; }
QPushButton:disabled { color: @muted; }
QPushButton#Primary { background: @accent; color: @onAccent; border: none; font-weight: 600;
                      padding: 10px 22px; }
QPushButton#Primary:hover { background: @accentHover; }
QPushButton::menu-indicator { image: none; width: 0; }

QComboBox, QSpinBox, QDoubleSpinBox { background: @surf2; border: 1px solid @border;
    border-radius: 8px; padding: 5px 10px; min-height: 20px; }
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: @accent; }
QComboBox { padding-right: 26px; }
QComboBox::drop-down { border: none; width: 24px; subcontrol-origin: padding; subcontrol-position: center right; }
QComboBox::down-arrow { image: url(:/icons/chevron_down.png); width: 10px; height: 8px; }
QAbstractSpinBox { padding-right: 22px; }
QAbstractSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 20px;
    border: none; background: transparent; margin-top: 3px; }
QAbstractSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 20px;
    border: none; background: transparent; margin-bottom: 3px; }
QAbstractSpinBox::up-arrow { image: url(:/icons/chevron_up.png); width: 9px; height: 7px; }
QAbstractSpinBox::down-arrow { image: url(:/icons/chevron_down.png); width: 9px; height: 7px; }
QAbstractSpinBox::up-arrow:disabled, QAbstractSpinBox::down-arrow:disabled { image: none; }
QComboBox QAbstractItemView { background: @surf; border: 1px solid @border; outline: 0;
    selection-background-color: @accentSoft; selection-color: @text; padding: 4px; }

QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: @muted; padding: 8px 16px; margin-right: 2px;
               border: none; border-bottom: 2px solid transparent; font-weight: 600; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }
QTabBar::tab:hover { color: @text; }

QTableWidget { background: @surf; border: 1px solid @border; border-radius: 12px;
               gridline-color: @border; alternate-background-color: @surf2;
               selection-background-color: @accentSoft; selection-color: @text; outline: 0; }
QHeaderView { background: transparent; }
QHeaderView::section { background: @surf; color: @muted; border: none;
                       border-bottom: 1px solid @border; padding: 8px; font-weight: 600; }
QTableCornerButton::section { background: @surf; border: none; }

QScrollArea, #ScrollBody { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @border; border-radius: 3px; min-height: 30px; }
QScrollBar::handle:horizontal { background: @border; border-radius: 3px; min-width: 30px; }
QScrollBar::handle:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QMenu { background: @surf; border: 1px solid @border; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 7px 22px 7px 14px; border-radius: 6px; }
QMenu::item:selected { background: @surf2; }
QMenu::separator { height: 1px; background: @border; margin: 4px 6px; }
QToolTip { background: @surf; color: @text; border: 1px solid @border; padding: 6px; }

#DropZone { background: @surf; border: 2px dashed @border; border-radius: 18px; }
#DropZone[active="true"] { border-color: @accent; background: @accentSoft; }
#DropTitle { font-size: 18px; font-weight: 600; }

QProgressBar#Busy { background: transparent; border: none; }
QProgressBar#Busy::chunk { background: @accent; }
#Toast { background: @text; color: @bg; border-radius: 10px; padding: 10px 18px; font-weight: 600; }
QGraphicsView { background: transparent; border: none; }
)QSS";

} // namespace

void Theme::setMode(Mode mode) { g_mode = mode; }
Theme::Mode Theme::mode() { return g_mode; }

const ThemePalette &Theme::palette()
{
    static const ThemePalette dark = makeDark();
    static const ThemePalette light = makeLight();
    return g_mode == Mode::Dark ? dark : light;
}

QString Theme::css(const QColor &c)
{
    if (c.alpha() == 255)
        return c.name();
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QString Theme::styleSheet()
{
    const ThemePalette &p = palette();
    QString s = QString::fromUtf8(kTemplate);
    // longer tokens first so "@accent" does not clobber "@accentHover"
    const std::pair<const char *, QColor> tokens[] = {
        {"@accentHover", p.accentHover}, {"@accentSoft", p.accentSoft}, {"@onAccent", p.onAccent},
        {"@accent", p.accent},           {"@dangerSoft", p.dangerSoft}, {"@danger", p.danger},
        {"@surf2", p.surface2},          {"@surf", p.surface},          {"@border", p.border},
        {"@muted", p.textMuted},         {"@text", p.text},             {"@warn", p.warn},
        {"@bg", p.bg},
    };
    for (const auto &t : tokens)
        s.replace(QLatin1String(t.first), css(t.second));
    return s;
}

QPixmap Theme::appPixmap(int size)
{
    QPixmap pm(256, 256);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient g(0, 0, 256, 256);
    g.setColorAt(0, QColor("#3fc1d2"));
    g.setColorAt(1, QColor("#16648f"));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(8, 8, 240, 240), 58, 58);

    QPainterPath drop;
    drop.moveTo(128, 44);
    drop.cubicTo(128, 44, 64, 118, 64, 154);
    drop.cubicTo(64, 190, 93, 214, 128, 214);
    drop.cubicTo(163, 214, 192, 190, 192, 154);
    drop.cubicTo(192, 118, 128, 44, 128, 44);
    p.setBrush(Qt::white);
    p.drawPath(drop);

    QPen pen(QColor("#16648f"), 11, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const QPointF line[] = {{88, 168}, {108, 146}, {126, 170}, {146, 128}, {168, 152}};
    p.drawPolyline(line, 5);
    p.end();

    return pm.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QIcon Theme::appIcon()
{
    QIcon icon;
    for (int s : {16, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(appPixmap(s));
    return icon;
}
