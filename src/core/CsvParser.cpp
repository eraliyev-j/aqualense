#include "CsvParser.h"

#include <QHash>
#include <QSet>
#include <QStringDecoder>
#include <QTime>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr int kSampleSize = 200;

const QStringList &dateFormats()
{
    static const QStringList f = {
        QStringLiteral("yyyy-MM-dd HH:mm:ss"), QStringLiteral("yyyy-MM-dd HH:mm"),
        QStringLiteral("yyyy-MM-dd H:mm:ss"),  QStringLiteral("yyyy-MM-dd H:mm"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss"), QStringLiteral("yyyy-MM-ddTHH:mm"),
        QStringLiteral("yyyy/MM/dd HH:mm:ss"), QStringLiteral("yyyy/MM/dd HH:mm"),
        QStringLiteral("yyyy/M/d H:mm:ss"),    QStringLiteral("yyyy/M/d H:mm"),
        QStringLiteral("yyyy-M-d H:mm:ss"),    QStringLiteral("yyyy-M-d H:mm"),
        QStringLiteral("yyyy.MM.dd HH:mm:ss"), QStringLiteral("yyyy.MM.dd HH:mm"),
        QStringLiteral("dd.MM.yyyy HH:mm:ss"), QStringLiteral("dd.MM.yyyy HH:mm"),
        QStringLiteral("dd/MM/yyyy HH:mm:ss"), QStringLiteral("dd/MM/yyyy HH:mm"),
        QStringLiteral("MM/dd/yyyy HH:mm:ss"), QStringLiteral("MM/dd/yyyy HH:mm"),
        QStringLiteral("M/d/yyyy H:mm:ss"),    QStringLiteral("M/d/yyyy H:mm"),
        QStringLiteral("yyyy-MM-dd"), QStringLiteral("yyyy/MM/dd"), QStringLiteral("yyyy.MM.dd"),
        QStringLiteral("yyyyMMdd"),   QStringLiteral("dd.MM.yyyy"), QStringLiteral("dd/MM/yyyy"),
        QStringLiteral("MM/dd/yyyy"),
        QStringLiteral("ISO"), // last: Qt's ISO parser also accepts date-only strings
    };
    return f;
}

bool isDateOnly(const QString &fmt)
{
    return fmt != QLatin1String("ISO") && !fmt.contains(QLatin1Char('H'));
}

QDateTime parseDate(const QString &value, const QString &fmt)
{
    const QString v = value.trimmed();
    if (fmt == QLatin1String("ISO")) {
        QDateTime d = QDateTime::fromString(v, Qt::ISODateWithMs);
        if (!d.isValid())
            d = QDateTime::fromString(v, Qt::ISODate);
        return d;
    }
    return QDateTime::fromString(v, fmt);
}

QTime parseTimeOfDay(const QString &value)
{
    const QString v = value.trimmed();
    for (const char *f : {"H:mm:ss", "H:mm", "HH:mm:ss.zzz"}) {
        const QTime t = QTime::fromString(v, QLatin1String(f));
        if (t.isValid())
            return t;
    }
    return {};
}

bool isMissing(const QString &v)
{
    static const QSet<QString> tokens = {
        QString(), QStringLiteral("na"), QStringLiteral("n/a"), QStringLiteral("nan"),
        QStringLiteral("null"), QStringLiteral("none"), QStringLiteral("-"),
        QStringLiteral("--"), QStringLiteral("#n/a"), QStringLiteral("#value!"),
    };
    return tokens.contains(v.trimmed().toLower());
}

bool parseNumber(const QString &field, QChar delimiter, double &out)
{
    QString s = field.trimmed();
    if (s.isEmpty())
        return false;
    // Decimal comma ("7,25") is only unambiguous when the delimiter is not a comma.
    if (delimiter != QLatin1Char(',') && s.contains(QLatin1Char(',')) && !s.contains(QLatin1Char('.')))
        s.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    out = s.toDouble(&ok);
    return ok && std::isfinite(out);
}

QChar detectDelimiter(const QString &headerLine)
{
    const QChar candidates[] = {QLatin1Char(','), QLatin1Char(';'), QLatin1Char('\t'), QLatin1Char('|')};
    QChar best = QLatin1Char(',');
    int bestCount = -1;
    for (QChar d : candidates) {
        int count = 0;
        bool quoted = false;
        for (QChar ch : headerLine) {
            if (ch == QLatin1Char('"'))
                quoted = !quoted;
            else if (!quoted && ch == d)
                ++count;
        }
        if (count > bestCount) {
            bestCount = count;
            best = d;
        }
    }
    return best;
}

QStringList splitLine(const QString &line, QChar delimiter)
{
    QStringList out;
    QString cur;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    cur += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cur += ch;
            }
        } else if (ch == QLatin1Char('"')) {
            quoted = true;
        } else if (ch == delimiter) {
            out << cur.trimmed();
            cur.clear();
        } else {
            cur += ch;
        }
    }
    out << cur.trimmed();
    return out;
}

double bestDateFormat(const QStringList &sample, QString &format)
{
    double best = 0.0;
    for (const QString &f : dateFormats()) {
        int ok = 0;
        for (const QString &v : sample)
            if (parseDate(v, f).isValid())
                ++ok;
        const double ratio = double(ok) / sample.size();
        if (ratio > best + 1e-9) {
            best = ratio;
            format = f;
        }
        if (best > 0.999)
            break;
    }
    return best;
}

QString decode(const QByteArray &raw, bool *usedFallback)
{
    QStringDecoder utf8(QStringConverter::Utf8);
    QString text = utf8.decode(raw);
    *usedFallback = false;
    if (utf8.hasError()) {
        // Excel on Korean/Russian/etc. Windows saves CSV in the ANSI code page.
        QStringDecoder system(QStringConverter::System);
        text = system.decode(raw);
        *usedFallback = true;
    }
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    return text;
}

int findHeader(const QStringList &header, std::initializer_list<const char *> names)
{
    for (int i = 0; i < header.size(); ++i) {
        const QString h = header[i].trimmed().toLower();
        for (const char *n : names)
            if (h == QString::fromUtf8(n))
                return i;
    }
    return -1;
}

/**
 * Long ("tidy") format — one row per measurement, e.g.
 *   Time,Section,Metric,Value,Unit,Tag
 *   2026-03-25T00:00:54,1,TOC,16.5,mg/L,ST1.TOC
 * is reshaped into one column per metric ("TOC (mg/L)"), keyed by the time column.
 * When several sections/stations are present, the station is appended to the name.
 * Returns false (and leaves the table untouched) when the file is not in that shape.
 */
bool pivotLongFormat(QStringList &header, QVector<QStringList> &rows, QChar delim, QString *note)
{
    const int timeIdx = findHeader(header, {"time", "datetime", "date_time", "timestamp", "date", "일시",
                                            "측정일시", "시간", "측정시간"});
    const int metricIdx = findHeader(header, {"metric", "parameter", "param", "variable", "item", "항목",
                                              "측정항목", "항목명"});
    const int valueIdx = findHeader(header, {"value", "val", "measurement", "result", "측정값", "값"});
    if (timeIdx < 0 || metricIdx < 0 || valueIdx < 0 || rows.isEmpty())
        return false;
    const int unitIdx = findHeader(header, {"unit", "units", "단위"});
    const int siteIdx = findHeader(header, {"section", "station", "site", "location", "지점", "측정소", "구간"});

    // the value column must be numeric and the metric column must be text
    int numeric = 0, sampled = 0;
    for (int r = 0; r < rows.size() && sampled < kSampleSize; ++r) {
        if (isMissing(rows[r][valueIdx]))
            continue;
        ++sampled;
        double d;
        if (parseNumber(rows[r][valueIdx], delim, d))
            ++numeric;
    }
    if (sampled == 0 || double(numeric) / sampled < 0.6)
        return false;

    QSet<QString> sites;
    if (siteIdx >= 0)
        for (const QStringList &f : std::as_const(rows))
            sites.insert(f[siteIdx]);
    const bool multiSite = sites.size() > 1;

    QStringList names;
    QHash<QString, int> colOf;
    QHash<QString, int> rowOf;
    QVector<QStringList> out;
    int duplicates = 0;
    for (const QStringList &f : std::as_const(rows)) {
        const QString metric = f[metricIdx].trimmed();
        if (metric.isEmpty() || isMissing(f[timeIdx]))
            continue;
        const QString key = multiSite ? QStringLiteral("%1 [%2]").arg(metric, f[siteIdx]) : metric;
        int col = colOf.value(key, -1);
        if (col < 0) {
            if (names.size() >= 200)
                return false; // too many distinct "metrics": probably not a long-format file
            col = int(names.size());
            colOf.insert(key, col);
            const QString unit = unitIdx >= 0 ? f[unitIdx].trimmed() : QString();
            names << (unit.isEmpty() || isMissing(unit) ? key : QStringLiteral("%1 (%2)").arg(key, unit));
            for (QStringList &o : out)
                o << QString();
        }
        int row = rowOf.value(f[timeIdx], -1);
        if (row < 0) {
            row = int(out.size());
            rowOf.insert(f[timeIdx], row);
            QStringList o;
            o.reserve(1 + names.size());
            o << f[timeIdx];
            for (int i = 0; i < names.size(); ++i)
                o << QString();
            out << o;
        }
        QString &cell = out[row][1 + col];
        if (!cell.isEmpty())
            ++duplicates;
        cell = f[valueIdx];
    }
    if (names.isEmpty() || out.isEmpty())
        return false;

    const int before = int(rows.size());
    header = QStringList{header[timeIdx]} + names;
    rows = std::move(out);
    if (note) {
        *note = QStringLiteral("Long-format file (time / metric / value) reshaped: %1 rows into %2 timestamps "
                               "× %3 parameter(s): %4.")
                    .arg(before).arg(rows.size()).arg(names.size()).arg(names.join(QStringLiteral(", ")));
        if (duplicates > 0)
            *note += QStringLiteral(" %1 duplicate timestamp(s) kept the last value.").arg(duplicates);
    }
    return true;
}

} // namespace

bool CsvParser::parse(const QByteArray &raw, const QString &sourceName, Dataset &out, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };

    out = Dataset{};
    out.sourceName = sourceName;

    bool fallbackEncoding = false;
    const QString text = decode(raw, &fallbackEncoding);

    // ---- lines -------------------------------------------------------------
    QStringList lines;
    {
        const QStringList parts = text.split(QLatin1Char('\n'));
        lines.reserve(parts.size());
        for (QString l : parts) {
            if (l.endsWith(QLatin1Char('\r')))
                l.chop(1);
            if (l.trimmed().isEmpty())
                continue;
            if (lines.isEmpty() && l.startsWith(QLatin1Char('#')))
                continue; // leading comment lines
            lines << l;
        }
    }
    if (lines.size() < 2)
        return fail(QStringLiteral("The file needs a header row and at least one data row."));

    const QChar delim = detectDelimiter(lines.first());
    QStringList header = splitLine(lines.first(), delim);
    int nCols = int(header.size());

    QSet<QString> seen;
    for (int i = 0; i < nCols; ++i) {
        QString h = header[i].trimmed();
        if (h.isEmpty())
            h = QStringLiteral("Column %1").arg(i + 1);
        const QString base = h;
        int k = 2;
        while (seen.contains(h))
            h = QStringLiteral("%1 (%2)").arg(base).arg(k++);
        seen.insert(h);
        header[i] = h;
    }

    QVector<QStringList> rows;
    rows.reserve(lines.size() - 1);
    int ragged = 0;
    for (int i = 1; i < lines.size(); ++i) {
        QStringList f = splitLine(lines[i], delim);
        if (f.size() != nCols) {
            ++ragged;
            while (f.size() < nCols)
                f << QString();
            while (f.size() > nCols)
                f.removeLast();
        }
        rows << f;
    }

    // ---- long format (time / metric / value rows) → one column per metric ----
    QString pivotNote;
    if (pivotLongFormat(header, rows, delim, &pivotNote))
        nCols = int(header.size());

    // ---- classify columns ---------------------------------------------------
    int timeIdx = -1;
    QString timeFmt;
    QVector<int> numericIdx;
    QVector<bool> isText(nCols, false);

    for (int c = 0; c < nCols; ++c) {
        QStringList sample;
        for (int r = 0; r < rows.size() && sample.size() < kSampleSize; ++r)
            if (!isMissing(rows[r][c]))
                sample << rows[r][c];
        if (sample.isEmpty())
            continue;

        int numeric = 0;
        for (const QString &s : sample) {
            double d;
            if (parseNumber(s, delim, d))
                ++numeric;
        }
        const double numRatio = double(numeric) / sample.size();

        if (timeIdx < 0 && numRatio < 0.5) {
            QString fmt;
            if (bestDateFormat(sample, fmt) >= 0.8) {
                timeIdx = c;
                timeFmt = fmt;
                continue;
            }
        }
        if (numRatio >= 0.6)
            numericIdx << c;
        else
            isText[c] = true;
    }

    // Separate "Date" + "Time" columns
    int timeOfDayIdx = -1;
    if (timeIdx >= 0 && isDateOnly(timeFmt) && timeIdx + 1 < nCols && isText[timeIdx + 1]) {
        int ok = 0, total = 0;
        for (int r = 0; r < rows.size() && total < kSampleSize; ++r) {
            if (isMissing(rows[r][timeIdx + 1]))
                continue;
            ++total;
            if (parseTimeOfDay(rows[r][timeIdx + 1]).isValid())
                ++ok;
        }
        if (total > 0 && double(ok) / total >= 0.8) {
            timeOfDayIdx = timeIdx + 1;
            isText[timeOfDayIdx] = false;
        }
    }

    if (numericIdx.isEmpty())
        return fail(QStringLiteral("No numeric columns were found. Check that the first row holds "
                                   "column names and the remaining rows hold measurements."));

    // ---- row order ----------------------------------------------------------
    QVector<int> order;
    order.reserve(rows.size());
    QVector<QDateTime> times;
    int badTime = 0;

    if (timeIdx >= 0) {
        QVector<QDateTime> parsed;
        QVector<int> src;
        for (int r = 0; r < rows.size(); ++r) {
            QDateTime t = parseDate(rows[r][timeIdx], timeFmt);
            if (t.isValid() && timeOfDayIdx >= 0) {
                const QTime tod = parseTimeOfDay(rows[r][timeOfDayIdx]);
                if (tod.isValid())
                    t.setTime(tod);
            }
            if (!t.isValid()) {
                ++badTime;
                continue;
            }
            parsed << t;
            src << r;
        }
        QVector<int> perm(parsed.size());
        std::iota(perm.begin(), perm.end(), 0);
        std::stable_sort(perm.begin(), perm.end(), [&](int a, int b) { return parsed[a] < parsed[b]; });
        for (int p : perm) {
            order << src[p];
            times << parsed[p];
        }
        out.timeColumn = timeOfDayIdx >= 0 ? header[timeIdx] + QStringLiteral(" + ") + header[timeOfDayIdx]
                                           : header[timeIdx];
    } else {
        order.resize(rows.size());
        std::iota(order.begin(), order.end(), 0);
    }

    if (order.isEmpty())
        return fail(QStringLiteral("None of the rows has a valid date/time value."));

    // ---- values -------------------------------------------------------------
    for (int c : numericIdx) {
        QVector<double> col(order.size(), kNaN);
        int valid = 0;
        for (int i = 0; i < order.size(); ++i) {
            const QString &field = rows[order[i]][c];
            double d;
            if (!isMissing(field) && parseNumber(field, delim, d)) {
                col[i] = d;
                ++valid;
            }
        }
        if (valid == 0) {
            out.skippedColumns << header[c];
            continue;
        }
        out.columns << header[c];
        out.values << col;
    }
    for (int c = 0; c < nCols; ++c)
        if (isText[c])
            out.skippedColumns << header[c];

    if (out.columns.isEmpty())
        return fail(QStringLiteral("All numeric columns are empty."));

    out.time = times;
    out.rowCount = int(order.size());

    // ---- notes --------------------------------------------------------------
    if (!pivotNote.isEmpty())
        out.warnings << pivotNote;
    if (fallbackEncoding)
        out.warnings << QStringLiteral("File is not UTF-8; decoded with the system code page.");
    if (ragged > 0)
        out.warnings << QStringLiteral("%1 row(s) had a different number of fields and were padded or trimmed.").arg(ragged);
    if (badTime > 0)
        out.warnings << QStringLiteral("%1 row(s) without a valid timestamp were skipped.").arg(badTime);
    if (!out.skippedColumns.isEmpty())
        out.warnings << QStringLiteral("Non-numeric column(s) ignored: %1.").arg(out.skippedColumns.join(QStringLiteral(", ")));
    if (timeIdx < 0)
        out.warnings << QStringLiteral("No date/time column detected; rows are plotted by index.");

    return true;
}
