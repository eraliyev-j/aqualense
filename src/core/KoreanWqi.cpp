#include "KoreanWqi.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace KoreanWqi {
namespace {

constexpr double N = std::numeric_limits<double>::quiet_NaN();

Standard maxStd(Param p, const char *label, const char *ko, const char *unit,
                std::initializer_list<double> lim, const QString &note = QString())
{
    Standard s;
    s.param = p;
    s.label = QString::fromUtf8(label);
    s.nameKo = QString::fromUtf8(ko);
    s.unit = QString::fromUtf8(unit);
    s.dir = Direction::Max;
    int i = 0;
    for (double v : lim)
        s.limit[i++] = v;
    for (; i < kGrades; ++i)
        s.limit[i] = N;
    for (int g = 0; g < kGrades; ++g)
        s.lo[g] = s.hi[g] = N;
    s.note = note;
    return s;
}

Standard minStd(Param p, const char *label, const char *ko, const char *unit, std::initializer_list<double> lim)
{
    Standard s = maxStd(p, label, ko, unit, lim);
    s.dir = Direction::Min;
    return s;
}

Standard phStd()
{
    Standard s = maxStd(Param::PH, "pH", "수소이온농도", "", {});
    s.dir = Direction::Range;
    for (int g = 0; g < kGrades; ++g) {
        s.lo[g] = g <= 3 ? 6.5 : (g <= 5 ? 6.0 : N);
        s.hi[g] = g <= 5 ? 8.5 : N;
    }
    s.note = QStringLiteral("Grades Ia–III share 6.5–8.5, IV–V share 6.0–8.5.");
    return s;
}

const QString kColiNote = QStringLiteral("No limit for grades IV–V; values above the grade III limit are shown as VI.");

// 생활환경기준 — 하천 (rivers)
const QVector<Standard> &riverStandards()
{
    static const QVector<Standard> s = {
        phStd(),
        maxStd(Param::BOD, "BOD", "생물화학적 산소요구량", "mg/L", {1, 2, 3, 5, 8, 10}),
        maxStd(Param::COD, "COD", "화학적 산소요구량", "mg/L", {2, 4, 5, 7, 9, 11},
               QStringLiteral("COD standard applied until 2015-12-31 (replaced by TOC); shown for reference.")),
        maxStd(Param::TOC, "TOC", "총유기탄소량", "mg/L", {2, 3, 4, 5, 6, 8}),
        maxStd(Param::SS, "SS", "부유물질량", "mg/L", {25, 25, 25, 25, 100},
               QStringLiteral("Grade V is qualitative (no floating debris); values above 100 mg/L are shown as VI.")),
        minStd(Param::DO, "DO", "용존산소량", "mg/L", {7.5, 5, 5, 5, 2, 2}),
        maxStd(Param::TP, "T-P", "총인", "mg/L", {0.02, 0.04, 0.1, 0.2, 0.3, 0.5}),
        maxStd(Param::TotalColiform, "Total coliform", "총대장균군", "CFU/100mL", {50, 500, 1000, 5000}, kColiNote),
        maxStd(Param::FecalColiform, "Fecal coliform", "분원성대장균군", "CFU/100mL", {10, 100, 200, 1000}, kColiNote),
    };
    return s;
}

// 생활환경기준 — 호소 (lakes)
const QVector<Standard> &lakeStandards()
{
    static const QVector<Standard> s = {
        phStd(),
        maxStd(Param::COD, "COD", "화학적 산소요구량", "mg/L", {2, 3, 4, 5, 8, 10},
               QStringLiteral("COD standard applied until 2015-12-31 (replaced by TOC); shown for reference.")),
        maxStd(Param::TOC, "TOC", "총유기탄소량", "mg/L", {2, 3, 4, 5, 6, 8}),
        maxStd(Param::SS, "SS", "부유물질량", "mg/L", {1, 5, 5, 15, 15},
               QStringLiteral("Grade V is qualitative (no floating debris); values above 15 mg/L are shown as VI.")),
        minStd(Param::DO, "DO", "용존산소량", "mg/L", {7.5, 5, 5, 5, 2, 2}),
        maxStd(Param::TP, "T-P", "총인", "mg/L", {0.01, 0.02, 0.03, 0.05, 0.10, 0.15}),
        maxStd(Param::TN, "T-N", "총질소", "mg/L", {0.2, 0.3, 0.4, 0.6, 1.0, 1.5}),
        maxStd(Param::ChlA, "Chl-a", "클로로필-a", "mg/m³", {5, 9, 14, 20, 35, 70}),
        maxStd(Param::TotalColiform, "Total coliform", "총대장균군", "CFU/100mL", {50, 500, 1000, 5000}, kColiNote),
        maxStd(Param::FecalColiform, "Fecal coliform", "분원성대장균군", "CFU/100mL", {10, 100, 200, 1000}, kColiNote),
    };
    return s;
}

double quantileSorted(const QVector<double> &v, double q)
{
    if (v.isEmpty())
        return N;
    const double pos = q * (v.size() - 1);
    const int i = int(std::floor(pos));
    const double f = pos - i;
    return i + 1 < v.size() ? v[i] + f * (v[i + 1] - v[i]) : v[i];
}

bool rx(const QString &s, const char *pattern)
{
    return QRegularExpression(QLatin1String(pattern)).match(s).hasMatch();
}

} // namespace

// ---------------------------------------------------------------------------
QString gradeCode(int g)
{
    static const char *c[] = {"Ia", "Ib", "II", "III", "IV", "V", "VI"};
    return g >= 0 && g < kGrades ? QLatin1String(c[g]) : QStringLiteral("—");
}

QString gradeNameKo(int g)
{
    static const char *c[] = {"매우 좋음", "좋음", "약간 좋음", "보통", "약간 나쁨", "나쁨", "매우 나쁨"};
    return g >= 0 && g < kGrades ? QString::fromUtf8(c[g]) : QString();
}

QString gradeNameEn(int g)
{
    static const char *c[] = {"Very good", "Good", "Fairly good", "Moderate", "Fairly poor", "Poor", "Very poor"};
    return g >= 0 && g < kGrades ? QLatin1String(c[g]) : QStringLiteral("Not assessed");
}

QString gradeUse(int g)
{
    static const char *c[] = {
        "Drinking water after simple filtration and disinfection",
        "Drinking water after conventional treatment",
        "Drinking or swimming water after conventional treatment",
        "Drinking water after advanced treatment; industrial water after conventional treatment",
        "Agricultural water; industrial water after advanced treatment",
        "Industrial water after special treatment; should not hinder daily activities such as walking",
        "Almost no dissolved oxygen; fish can hardly survive",
    };
    return g >= 0 && g < kGrades ? QLatin1String(c[g]) : QString();
}

QColor gradeColor(int g)
{
    // NIER character design guide (CMYK), converted to sRGB and slightly deepened for legibility.
    static const QColor c[] = {QColor("#169fd8"), QColor("#1fb98a"), QColor("#76bf2a"), QColor("#8a9fa0"),
                               QColor("#e3a008"), QColor("#e2650f"), QColor("#d9261c")};
    return g >= 0 && g < kGrades ? c[g] : QColor("#808080");
}

const Standard *standardFor(Param p, WaterBody body)
{
    const QVector<Standard> &all = body == WaterBody::River ? riverStandards() : lakeStandards();
    for (const Standard &s : all)
        if (s.param == p)
            return &s;
    return nullptr;
}

Param detect(const QString &columnName)
{
    const QString s = columnName.toLower().trimmed();
    const QString k = QString(s).remove(QLatin1Char(' '));
    if (s.isEmpty())
        return Param::None;

    if (k.contains(QStringLiteral("분원성")) || rx(s, "fa?ecal") || rx(s, "(^|[^a-z])fc([^a-z]|$)"))
        return Param::FecalColiform;
    if (k.contains(QStringLiteral("총대장균")) || rx(s, "total ?coli") || rx(s, "(^|[^a-z])tc([^a-z]|$)"))
        return Param::TotalColiform;
    if (k.contains(QStringLiteral("클로로필")) || rx(s, "chl"))
        return Param::ChlA;
    if (k.contains(QStringLiteral("총유기탄소")) || rx(s, "(^|[^a-z])toc([^a-z]|$)"))
        return Param::TOC;
    if (k.contains(QStringLiteral("생물화학적")) || rx(s, "(^|[^a-z])bod"))
        return Param::BOD;
    if (k.contains(QStringLiteral("화학적산소")) || rx(s, "(^|[^a-z])cod"))
        return Param::COD;
    if (k.contains(QStringLiteral("용존산소")) || rx(s, "(^|[^a-z])do([^a-z]|$)")) {
        // DO saturation (%) is not a concentration
        if (s.contains(QLatin1Char('%')) || s.contains(QStringLiteral("sat")) || k.contains(QStringLiteral("포화")))
            return Param::None;
        return Param::DO;
    }
    if (k.contains(QStringLiteral("부유물질")) || rx(s, "(^|[^a-z])t?ss([^a-z]|$)"))
        return Param::SS;
    if (k.contains(QStringLiteral("총인")) || rx(s, "total ?phosph") || rx(s, "(^|[^a-z])t[-_. ]?p([^a-z]|$)"))
        return Param::TP;
    if (k.contains(QStringLiteral("총질소")) || rx(s, "total ?nitrogen") || rx(s, "(^|[^a-z])t[-_. ]?n([^a-z]|$)"))
        return Param::TN;
    if (k.contains(QStringLiteral("수소이온")) || rx(s, "(^|[^a-z])ph([^a-z]|$)"))
        return Param::PH;
    return Param::None;
}

int gradeOf(const Standard &s, double v)
{
    if (!std::isfinite(v))
        return -1;
    for (int g = 0; g < kWorst; ++g) {
        switch (s.dir) {
        case Direction::Max:
            if (std::isfinite(s.limit[g]) && v <= s.limit[g])
                return g;
            break;
        case Direction::Min:
            if (std::isfinite(s.limit[g]) && v >= s.limit[g])
                return g;
            break;
        case Direction::Range:
            if (std::isfinite(s.lo[g]) && v >= s.lo[g] && v <= s.hi[g])
                return g;
            break;
        }
    }
    return kWorst;
}

Result evaluate(const Dataset &data, WaterBody body, Statistic statistic)
{
    Result r;
    r.body = body;
    r.statistic = statistic;

    for (int c = 0; c < data.columns.size(); ++c) {
        const Param p = detect(data.columns[c]);
        const Standard *st = p == Param::None ? nullptr : standardFor(p, body);
        if (!st) {
            r.unmatched << data.columns[c];
            continue;
        }
        ParamResult pr;
        pr.column = c;
        pr.param = p;
        pr.standard = st;

        QVector<double> v;
        v.reserve(data.values[c].size());
        int counts[kGrades] = {};
        for (double x : data.values[c]) {
            if (!std::isfinite(x))
                continue;
            v << x;
            counts[gradeOf(*st, x)]++;
        }
        pr.count = int(v.size());
        if (pr.count == 0) {
            pr.included = false;
            pr.status = QStringLiteral("No data");
            r.params << pr;
            continue;
        }
        std::sort(v.begin(), v.end());
        double sum = 0;
        for (double x : v)
            sum += x;
        pr.mean = sum / pr.count;
        pr.median = quantileSorted(v, 0.5);
        pr.min = v.first();
        pr.max = v.last();
        pr.q1 = quantileSorted(v, 0.25);
        pr.q3 = quantileSorted(v, 0.75);
        pr.p05 = quantileSorted(v, 0.05);
        pr.p95 = quantileSorted(v, 0.95);
        pr.value = statistic == Statistic::Mean ? pr.mean : pr.median;
        pr.grade = gradeOf(*st, pr.value);
        int meets = 0;
        for (int g = 0; g < kGrades; ++g) {
            pr.share[g] = double(counts[g]) / pr.count;
            if (g <= 3)
                meets += counts[g];
        }
        pr.compliance = double(meets) / pr.count;
        if (p == Param::COD) {
            pr.included = false;
            pr.status = QStringLiteral("Reference only");
        }
        r.params << pr;
    }

    // Lakes: T-P is not applied when TN/TP < 7, T-N is not applied when TN/TP ≥ 16.
    if (body == WaterBody::Lake) {
        ParamResult *tp = nullptr, *tn = nullptr;
        for (ParamResult &pr : r.params) {
            if (pr.param == Param::TP && pr.count > 0 && !tp) tp = &pr;
            if (pr.param == Param::TN && pr.count > 0 && !tn) tn = &pr;
        }
        if (tp && tn && tp->value > 0) {
            const double ratio = tn->value / tp->value;
            const QString why = QStringLiteral("Not applied: TN/TP = %1").arg(ratio, 0, 'f', 1);
            if (ratio < 7) {
                tp->included = false;
                tp->status = why;
            } else if (ratio >= 16) {
                tn->included = false;
                tn->status = why;
            }
        }
    }

    // Overall grade: the most limiting included parameter.
    QStringList limiting;
    for (const ParamResult &pr : std::as_const(r.params)) {
        if (!pr.included || pr.grade < 0)
            continue;
        if (pr.grade > r.overallGrade) {
            r.overallGrade = pr.grade;
            limiting.clear();
        }
        if (pr.grade == r.overallGrade && !limiting.contains(pr.standard->label))
            limiting << pr.standard->label;
    }
    r.limiting = limiting.join(QStringLiteral(", "));
    return r;
}

QString bodyName(WaterBody b)
{
    return b == WaterBody::River ? QStringLiteral("River (하천)") : QStringLiteral("Lake (호소)");
}

QString sourceUrl()
{
    return QStringLiteral("https://water.nier.go.kr/web/contents/contentView/?pMENU_NO=37");
}

} // namespace KoreanWqi
