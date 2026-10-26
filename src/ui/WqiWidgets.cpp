#include "WqiWidgets.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace KoreanWqi;

namespace {

QString num(double v)
{
    if (!std::isfinite(v))
        return QStringLiteral("—");
    const double a = std::fabs(v);
    if (a == 0)       return QStringLiteral("0");
    if (a >= 10000)   return QLocale(QLocale::English).toString(v, 'f', 0);
    if (a >= 100)     return QString::number(v, 'f', 0);
    if (a >= 10)      return QString::number(v, 'f', 1);
    if (a >= 1)       return QString::number(v, 'f', 2);
    if (a >= 0.01)    return QString::number(v, 'f', 3);
    return QString::number(v, 'g', 3);
}

/** Short limit label: drops trailing zeros ("0.10" → "0.1", "7.50" → "7.5"). */
QString lim(double v)
{
    QString s = QString::number(v, 'f', 3);
    while (s.contains(QLatin1Char('.')) && (s.endsWith(QLatin1Char('0')) || s.endsWith(QLatin1Char('.'))))
        s.chop(1);
    return s;
}

QColor onColor(const QColor &bg)
{
    const double l = (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()) / 255.0;
    return l > 0.56 ? QColor("#151a21") : QColor(Qt::white);
}

QString gradeSpan(int a, int b)
{
    return a == b ? gradeCode(a) : QStringLiteral("%1–%2").arg(gradeCode(a), gradeCode(b));
}

QVector<GradeScale::Segment> buildSegments(const ParamResult &r)
{
    QVector<GradeScale::Segment> segs;
    const Standard &s = *r.standard;

    if (s.dir == Direction::Range) {
        // pH: grades Ia–III and IV–V are nested ranges; outside both is VI
        const double inLo = s.lo[0], inHi = s.hi[0], outLo = s.lo[4], outHi = s.hi[4];
        const double lo = std::min(outLo - 1.0, std::floor((r.p05 - 0.3) * 2) / 2);
        const double hi = std::max(outHi + 1.0, std::ceil((r.p95 + 0.3) * 2) / 2);
        segs << GradeScale::Segment{lo, outLo, 6, 6};
        if (inLo > outLo)
            segs << GradeScale::Segment{outLo, inLo, 4, 5};
        segs << GradeScale::Segment{inLo, inHi, 0, 3};
        if (outHi > inHi)
            segs << GradeScale::Segment{inHi, outHi, 4, 5};
        segs << GradeScale::Segment{outHi, hi, 6, 6};
        return segs;
    }

    struct Group { double limit; int first, last; };
    QVector<Group> groups;
    for (int g = 0; g < kWorst; ++g) {
        if (!std::isfinite(s.limit[g]))
            continue;
        if (!groups.isEmpty() && s.limit[g] == groups.last().limit)
            groups.last().last = g;
        else
            groups << Group{s.limit[g], g, g};
    }
    if (groups.isEmpty())
        return segs;

    const bool isMax = s.dir == Direction::Max;
    const double outer = groups.last().limit;   // limit between the last numeric grade and VI
    const double inner = groups.first().limit;  // Ia limit
    double prev;
    double end;
    if (isMax) {
        prev = std::min(0.0, r.min);
        end = std::max(outer * 1.5, r.p95 * 1.08);
    } else {
        prev = std::max(inner * 1.5, r.p95 * 1.08);
        end = std::min(0.0, r.min);
    }
    for (const Group &gr : std::as_const(groups)) {
        segs << GradeScale::Segment{prev, gr.limit, gr.first, gr.last};
        prev = gr.limit;
    }
    segs << GradeScale::Segment{prev, end, std::min(groups.last().last + 1, kWorst), kWorst};
    return segs;
}

QString rangeText(const ParamResult &r, const QVector<GradeScale::Segment> &segs)
{
    const Standard &s = *r.standard;
    const QString u = s.unit.isEmpty() ? QString() : QStringLiteral(" ") + s.unit;
    QStringList lines;
    for (int i = 0; i < segs.size(); ++i) {
        const GradeScale::Segment &g = segs[i];
        QString t;
        if (s.dir == Direction::Range) {
            if (i == 0)                   t = QStringLiteral("< %1").arg(lim(g.v1));
            else if (i == segs.size() - 1) t = QStringLiteral("> %1").arg(lim(g.v0));
            else                          t = QStringLiteral("%1 – %2").arg(lim(g.v0), lim(g.v1));
        } else if (s.dir == Direction::Max) {
            if (i == 0)                   t = QStringLiteral("≤ %1").arg(lim(g.v1));
            else if (i == segs.size() - 1) t = QStringLiteral("> %1").arg(lim(g.v0));
            else                          t = QStringLiteral("%1 – %2").arg(lim(g.v0), lim(g.v1));
        } else {
            if (i == 0)                   t = QStringLiteral("≥ %1").arg(lim(g.v1));
            else if (i == segs.size() - 1) t = QStringLiteral("< %1").arg(lim(g.v0));
            else                          t = QStringLiteral("%1 – %2").arg(lim(g.v1), lim(g.v0));
        }
        lines << QStringLiteral("%1:  %2%3").arg(gradeSpan(g.gFirst, g.gLast), t, u);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

QString gradePillStyle(int grade, int fontPx, int padV, int padH)
{
    const QColor bg = gradeColor(grade);
    return QStringLiteral("background:%1; color:%2; border-radius:9px; padding:%3px %4px; "
                          "font-size:%5px; font-weight:700;")
        .arg(bg.name(), onColor(bg).name()).arg(padV).arg(padH).arg(fontPx);
}

// ================================================================ GradeScale
GradeScale::GradeScale(const ParamResult &r, QWidget *parent) : QWidget(parent), m_r(r)
{
    m_segments = buildSegments(r);
    setMinimumHeight(86);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(QStringLiteral("%1 grade limits\n%2\n\nBox: Q1–Q3   Whiskers: 5th–95th percentile\n"
                              "Diamond: representative value, coloured by its grade")
                   .arg(r.standard->label, rangeText(r, m_segments)));
}

double GradeScale::xFor(double value, double left, double width) const
{
    const int n = int(m_segments.size());
    if (n == 0 || !std::isfinite(value))
        return left;
    const double w = width / n;
    for (int i = 0; i < n; ++i) {
        const Segment &s = m_segments[i];
        const double a = std::min(s.v0, s.v1), b = std::max(s.v0, s.v1);
        if (value >= a && value <= b && b > a)
            return left + (i + (value - s.v0) / (s.v1 - s.v0)) * w;
    }
    const double v0 = m_segments.first().v0, v1 = m_segments.last().v1;
    return (value - v0) * (v1 - v0) < 0 ? left : left + width;
}

void GradeScale::paintEvent(QPaintEvent *)
{
    if (m_segments.isEmpty())
        return;
    const ThemePalette &pal = Theme::palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double left = 12, width = this->width() - 24;
    const double boxY = 15, bandTop = 32, bandH = 26;
    const int n = int(m_segments.size());
    const double w = width / n;

    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.82);
    QFont bold = small;
    bold.setBold(true);

    // ---- band -----------------------------------------------------------------
    QPainterPath clip;
    clip.addRoundedRect(QRectF(left, bandTop, width, bandH), 7, 7);
    p.save();
    p.setClipPath(clip);
    p.setFont(bold);
    for (int i = 0; i < n; ++i) {
        const Segment &s = m_segments[i];
        const QRectF rc(left + i * w, bandTop, w, bandH);
        const QColor c = gradeColor(s.gFirst);
        p.fillRect(rc, c);
        const QString label = gradeSpan(s.gFirst, s.gLast);
        if (QFontMetricsF(bold).horizontalAdvance(label) + 6 < w) {
            p.setPen(onColor(c));
            p.drawText(rc, Qt::AlignCenter, label);
        }
        if (i > 0) {
            p.setPen(QPen(pal.surface, 1.5));
            p.drawLine(QPointF(rc.left(), bandTop), QPointF(rc.left(), bandTop + bandH));
        }
    }
    p.restore();

    // ---- limit labels at the inner boundaries --------------------------------
    p.setFont(small);
    p.setPen(pal.textMuted);
    const QFontMetricsF fm(small);
    double lastRight = -1e9;
    for (int i = 1; i < n; ++i) {
        const double x = left + i * w;
        const QString t = lim(m_segments[i].v0);
        const double tw = fm.horizontalAdvance(t);
        if (x - tw / 2 < lastRight + 4)
            continue;
        p.drawText(QRectF(x - tw / 2 - 2, bandTop + bandH + 3, tw + 4, fm.height()), Qt::AlignCenter, t);
        lastRight = x + tw / 2;
    }
    if (m_r.count == 0)
        return;

    // ---- box plot of the data -----------------------------------------------------
    QColor ink = pal.text;
    const double x05 = xFor(m_r.p05, left, width), x95 = xFor(m_r.p95, left, width);
    const double xq1 = xFor(m_r.q1, left, width), xq3 = xFor(m_r.q3, left, width);
    QColor wc = ink;
    wc.setAlpha(150);
    p.setPen(QPen(wc, 1.3));
    p.drawLine(QPointF(x05, boxY), QPointF(x95, boxY));
    p.drawLine(QPointF(x05, boxY - 5), QPointF(x05, boxY + 5));
    p.drawLine(QPointF(x95, boxY - 5), QPointF(x95, boxY + 5));
    QColor fill = ink;
    fill.setAlpha(pal.dark ? 60 : 40);
    p.setBrush(fill);
    p.setPen(QPen(ink, 1.2));
    QRectF box(QPointF(std::min(xq1, xq3), boxY - 7), QPointF(std::max(xq1, xq3), boxY + 7));
    if (box.width() < 3)
        box.adjust(-1.5, 0, 1.5, 0);
    p.drawRoundedRect(box, 2, 2);

    // ---- representative value marker ---------------------------------------------
    const double xv = xFor(m_r.value, left, width);
    p.setPen(QPen(ink, 2.0));
    p.drawLine(QPointF(xv, boxY + 8), QPointF(xv, bandTop + bandH + 1));
    QPolygonF diamond;
    diamond << QPointF(xv, boxY - 8) << QPointF(xv + 6, boxY) << QPointF(xv, boxY + 8) << QPointF(xv - 6, boxY);
    p.setBrush(m_r.grade >= 0 ? gradeColor(m_r.grade) : pal.textMuted);
    p.setPen(QPen(ink, 1.4));
    p.drawPolygon(diamond);
}

// ================================================================ GradeShareBar
GradeShareBar::GradeShareBar(const double (&share)[kGrades], QWidget *parent) : QWidget(parent)
{
    std::copy(std::begin(share), std::end(share), std::begin(m_share));
    setFixedHeight(12);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QStringList tip;
    for (int g = 0; g < kGrades; ++g)
        tip << QStringLiteral("%1  %2:  %3%").arg(gradeCode(g), gradeNameKo(g)).arg(100.0 * m_share[g], 0, 'f', 1);
    setToolTip(QStringLiteral("Share of samples in each grade\n") + tip.join(QLatin1Char('\n')));
}

void GradeShareBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(0, 1, width(), height() - 2);
    QPainterPath clip;
    clip.addRoundedRect(r, 5, 5);
    p.setClipPath(clip);
    p.fillRect(r, Theme::palette().surface2);
    double x = 0;
    for (int g = 0; g < kGrades; ++g) {
        const double w = m_share[g] * r.width();
        if (w <= 0)
            continue;
        p.fillRect(QRectF(x, r.top(), w, r.height()), gradeColor(g));
        x += w;
    }
}

// ================================================================ WqiCard
WqiCard::WqiCard(const QString &column, const ParamResult &r, Statistic stat, QWidget *parent)
    : QFrame(parent), m_column(r.column)
{
    setObjectName(QStringLiteral("ParamCard"));
    setCursor(Qt::PointingHandCursor);
    const Standard &st = *r.standard;

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(16, 14, 16, 12);
    v->setSpacing(4);

    auto *top = new QHBoxLayout;
    auto *name = new QLabel(column);
    name->setObjectName(QStringLiteral("CardName"));
    name->setMinimumWidth(0);
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *pill = new QLabel(r.grade >= 0 ? gradeCode(r.grade) : QStringLiteral("—"));
    pill->setStyleSheet(r.grade >= 0 ? gradePillStyle(r.grade) : QString());
    if (r.grade < 0)
        pill->setObjectName(QStringLiteral("Badge"));
    top->addWidget(name, 1);
    top->addWidget(pill);
    v->addLayout(top);

    auto *stdLbl = new QLabel(QStringLiteral("%1  ·  %2%3").arg(st.label, st.nameKo,
                                                           st.unit.isEmpty() ? QString() : QStringLiteral("  ·  ") + st.unit));
    stdLbl->setObjectName(QStringLiteral("Small"));
    v->addWidget(stdLbl);

    auto *valRow = new QHBoxLayout;
    valRow->setSpacing(8);
    auto *val = new QLabel(r.count > 0 ? num(r.value) : QStringLiteral("—"));
    val->setObjectName(QStringLiteral("CardValue"));
    auto *valHint = new QLabel(r.count > 0
        ? QStringLiteral("%1  ·  %2 (%3)").arg(stat == Statistic::Mean ? QStringLiteral("mean") : QStringLiteral("median"),
                                              gradeNameKo(r.grade), gradeNameEn(r.grade))
        : QStringLiteral("no data"));
    valHint->setObjectName(QStringLiteral("Muted"));
    valRow->addWidget(val);
    valRow->addWidget(valHint, 1, Qt::AlignBottom);
    v->addLayout(valRow);

    v->addSpacing(2);
    v->addWidget(new GradeScale(r, this));

    auto *shareHead = new QHBoxLayout;
    auto *sl = new QLabel(QStringLiteral("Samples by grade  ·  n = %1").arg(QLocale(QLocale::English).toString(r.count)));
    sl->setObjectName(QStringLiteral("Small"));
    auto *cl = new QLabel(QStringLiteral("Grade III or better:  %1%").arg(100.0 * r.compliance, 0, 'f', r.compliance < 0.1 ? 1 : 0));
    cl->setObjectName(QStringLiteral("Small"));
    shareHead->addWidget(sl, 1);
    shareHead->addWidget(cl);
    v->addLayout(shareHead);
    v->addWidget(new GradeShareBar(r.share, this));

    // top three grades by share
    QVector<int> order(kGrades);
    for (int g = 0; g < kGrades; ++g)
        order[g] = g;
    std::stable_sort(order.begin(), order.end(), [&r](int a, int b) { return r.share[a] > r.share[b]; });
    QStringList parts;
    for (int k = 0; k < 3; ++k)
        if (r.share[order[k]] > 0)
            parts << QStringLiteral("<span style='color:%1'>■</span> %2 %3%")
                         .arg(gradeColor(order[k]).name(), gradeCode(order[k]))
                         .arg(100.0 * r.share[order[k]], 0, 'f', r.share[order[k]] < 0.1 ? 1 : 0);
    auto *top3 = new QLabel(parts.join(QStringLiteral("&nbsp;&nbsp;&nbsp;")));
    top3->setTextFormat(Qt::RichText);
    top3->setObjectName(QStringLiteral("Small"));
    v->addWidget(top3);

    QStringList notes;
    if (!r.status.isEmpty())
        notes << r.status + QStringLiteral(" — not used for the overall grade.");
    if (!st.note.isEmpty())
        notes << st.note;
    if (!notes.isEmpty()) {
        auto *note = new QLabel(notes.join(QLatin1Char(' ')));
        note->setObjectName(r.included ? QStringLiteral("Small") : QStringLiteral("Warn"));
        note->setWordWrap(true);
        v->addWidget(note);
    }
    v->addStretch();

    setToolTip(QStringLiteral("%1 — grade %2 %3\n%4\n\nMean %5   Median %6\nMin %7   Max %8\n\nClick to open the time series.")
                   .arg(column, gradeCode(r.grade), gradeNameKo(r.grade), gradeUse(r.grade),
                        num(r.mean), num(r.median), num(r.min), num(r.max)));
}

void WqiCard::mousePressEvent(QMouseEvent *e)
{
    QFrame::mousePressEvent(e);
    emit clicked(m_column);
}

// ================================================================ GradeLegend
GradeLegend::GradeLegend(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(46);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QStringList tip;
    for (int g = 0; g < kGrades; ++g)
        tip << QStringLiteral("%1  %2 (%3): %4").arg(gradeCode(g), gradeNameKo(g), gradeNameEn(g), gradeUse(g));
    setToolTip(tip.join(QLatin1Char('\n')));
}

void GradeLegend::paintEvent(QPaintEvent *)
{
    const ThemePalette &pal = Theme::palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double w = double(width()) / kGrades;
    QFont bold = font();
    bold.setBold(true);
    bold.setPointSizeF(bold.pointSizeF() * 0.9);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    for (int g = 0; g < kGrades; ++g) {
        const QRectF cell(g * w + 2, 0, w - 4, height());
        p.setPen(Qt::NoPen);
        p.setBrush(gradeColor(g));
        p.drawRoundedRect(QRectF(cell.left(), 0, cell.width(), 6), 3, 3);
        p.setFont(bold);
        p.setPen(pal.text);
        p.drawText(QRectF(cell.left(), 9, cell.width(), 18), Qt::AlignHCenter | Qt::AlignVCenter, gradeCode(g));
        p.setFont(small);
        p.setPen(pal.textMuted);
        p.drawText(QRectF(cell.left(), 26, cell.width(), 18), Qt::AlignHCenter | Qt::AlignVCenter, gradeNameKo(g));
    }
}
