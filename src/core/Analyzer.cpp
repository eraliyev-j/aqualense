#include "Analyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double quantileSorted(const QVector<double> &s, double q)
{
    if (s.isEmpty())
        return kNaN;
    const double pos = q * (s.size() - 1);
    const int lo = int(std::floor(pos));
    const int hi = std::min(lo + 1, int(s.size()) - 1);
    return s[lo] + (s[hi] - s[lo]) * (pos - lo);
}

double medianInPlace(QVector<double> &v)
{
    const int n = int(v.size());
    auto mid = v.begin() + n / 2;
    std::nth_element(v.begin(), mid, v.end());
    double m = *mid;
    if (n % 2 == 0) {
        const double lower = *std::max_element(v.begin(), mid);
        m = 0.5 * (m + lower);
    }
    return m;
}

QVector<double> movingAverage(const QVector<double> &v, int window)
{
    const int n = int(v.size());
    const int h = window / 2;
    QVector<double> sum(n + 1, 0.0);
    QVector<int> cnt(n + 1, 0);
    for (int i = 0; i < n; ++i) {
        const bool ok = std::isfinite(v[i]);
        sum[i + 1] = sum[i] + (ok ? v[i] : 0.0);
        cnt[i + 1] = cnt[i] + (ok ? 1 : 0);
    }
    QVector<double> out(n, kNaN);
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(v[i]))
            continue;
        const int a = std::max(0, i - h), b = std::min(n - 1, i + h);
        const int c = cnt[b + 1] - cnt[a];
        if (c > 0)
            out[i] = (sum[b + 1] - sum[a]) / c;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
QJsonObject AnalysisSettings::toJson() const
{
    return {{QStringLiteral("method"), int(method)},
            {QStringLiteral("threshold"), threshold},
            {QStringLiteral("window"), window}};
}

AnalysisSettings AnalysisSettings::fromJson(const QJsonObject &obj)
{
    AnalysisSettings s;
    const int m = obj.value(QStringLiteral("method")).toInt(int(s.method));
    if (m >= 0 && m <= 2)
        s.method = AnomalyMethod(m);
    s.threshold = obj.value(QStringLiteral("threshold")).toDouble(defaultThreshold(s.method));
    s.window = obj.value(QStringLiteral("window")).toInt(s.window);
    return s;
}

double AnalysisSettings::defaultThreshold(AnomalyMethod m)
{
    switch (m) {
    case AnomalyMethod::Hampel: return 3.5;
    case AnomalyMethod::ZScore: return 3.0;
    case AnomalyMethod::IQR:    return 1.5;
    }
    return 3.0;
}

QString AnalysisSettings::methodName(AnomalyMethod m)
{
    switch (m) {
    case AnomalyMethod::Hampel: return QStringLiteral("Hampel filter");
    case AnomalyMethod::ZScore: return QStringLiteral("Z-score");
    case AnomalyMethod::IQR:    return QStringLiteral("IQR fences");
    }
    return {};
}

// ---------------------------------------------------------------------------
AnalysisResult Analyzer::run(const Dataset &data, const AnalysisSettings &settings)
{
    AnalysisResult res;
    res.settings = settings;
    res.slopeUnit = data.hasTime() ? QStringLiteral("/day") : QStringLiteral("/row");

    const int n = data.rowCount;
    const int window = std::max(3, settings.window | 1); // odd, >= 3
    const double t = settings.threshold;

    // x positions used for the trend regression
    QVector<double> xs(n);
    if (data.hasTime()) {
        const qint64 t0 = data.time.first().toMSecsSinceEpoch();
        for (int i = 0; i < n; ++i)
            xs[i] = double(data.time[i].toMSecsSinceEpoch() - t0) / 86400000.0;
    } else {
        for (int i = 0; i < n; ++i)
            xs[i] = i;
    }

    for (int c = 0; c < data.columns.size(); ++c) {
        const QVector<double> &v = data.values[c];
        ColumnStats s;
        s.name = data.columns[c];
        QVector<int> rowsFlagged;

        QVector<double> clean;
        clean.reserve(n);
        for (double x : v)
            if (std::isfinite(x))
                clean << x;
        s.count = int(clean.size());
        s.missing = n - s.count;
        res.totalMissing += s.missing;

        if (clean.isEmpty()) {
            res.stats << s;
            res.anomalyRows << rowsFlagged;
            res.smooth << QVector<double>(n, kNaN);
            continue;
        }

        // ---- descriptive statistics ----------------------------------------
        double sum = 0;
        for (double x : clean)
            sum += x;
        s.mean = sum / s.count;
        double m2 = 0, m3 = 0;
        for (double x : clean) {
            const double d = x - s.mean;
            m2 += d * d;
            m3 += d * d * d;
        }
        s.stddev = s.count > 1 ? std::sqrt(m2 / (s.count - 1)) : 0.0;
        const double popSd = std::sqrt(m2 / s.count);
        s.skewness = (s.count > 2 && popSd > 0) ? (m3 / s.count) / std::pow(popSd, 3) : 0.0;

        QVector<double> sorted = clean;
        std::sort(sorted.begin(), sorted.end());
        s.min = sorted.first();
        s.max = sorted.last();
        s.q1 = quantileSorted(sorted, 0.25);
        s.median = quantileSorted(sorted, 0.50);
        s.q3 = quantileSorted(sorted, 0.75);

        // ---- linear trend (least squares) ----------------------------------
        {
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            int k = 0;
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(v[i]))
                    continue;
                sx += xs[i];
                sy += v[i];
                sxx += xs[i] * xs[i];
                sxy += xs[i] * v[i];
                ++k;
            }
            const double den = k * sxx - sx * sx;
            s.slope = (k > 1 && std::fabs(den) > 1e-12) ? (k * sxy - sx * sy) / den : 0.0;
        }

        res.smooth << movingAverage(v, window);

        // ---- anomaly detection ---------------------------------------------
        auto flag = [&](int row, double expected, double score) {
            res.anomalies.push_back({c, row, v[row], expected, score});
            rowsFlagged << row;
        };

        switch (settings.method) {
        case AnomalyMethod::ZScore:
            if (s.stddev > 0) {
                s.hasBounds = true;
                s.lower = s.mean - t * s.stddev;
                s.upper = s.mean + t * s.stddev;
                for (int i = 0; i < n; ++i) {
                    if (!std::isfinite(v[i]))
                        continue;
                    const double z = std::fabs(v[i] - s.mean) / s.stddev;
                    if (z > t)
                        flag(i, s.mean, z);
                }
            }
            break;

        case AnomalyMethod::IQR: {
            const double iqr = s.q3 - s.q1;
            if (iqr > 0) {
                s.hasBounds = true;
                s.lower = s.q1 - t * iqr;
                s.upper = s.q3 + t * iqr;
                for (int i = 0; i < n; ++i) {
                    if (!std::isfinite(v[i]))
                        continue;
                    if (v[i] < s.lower || v[i] > s.upper) {
                        const double dist = v[i] < s.q1 ? s.q1 - v[i] : v[i] - s.q3;
                        flag(i, s.median, dist / iqr);
                    }
                }
            }
            break;
        }

        case AnomalyMethod::Hampel: {
            // Rolling median + MAD: robust to trends and daily cycles, catches sensor spikes.
            const int h = window / 2;
            const double floorScale = s.stddev * 0.05; // guards against quantised / flat signals
            QVector<double> buf;
            buf.reserve(window);
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(v[i]))
                    continue;
                buf.clear();
                for (int j = std::max(0, i - h); j <= std::min(n - 1, i + h); ++j)
                    if (std::isfinite(v[j]))
                        buf << v[j];
                if (buf.size() < 3)
                    continue;
                const double med = medianInPlace(buf);
                for (double &b : buf)
                    b = std::fabs(b - med);
                const double scale = std::max(medianInPlace(buf) * 1.4826, floorScale);
                if (scale <= 0)
                    continue;
                const double score = std::fabs(v[i] - med) / scale;
                if (score > t)
                    flag(i, med, score);
            }
            break;
        }
        }

        s.anomalies = int(rowsFlagged.size());
        res.stats << s;
        res.anomalyRows << rowsFlagged;
    }

    std::sort(res.anomalies.begin(), res.anomalies.end(), [](const Anomaly &a, const Anomaly &b) {
        return a.row != b.row ? a.row < b.row : a.column < b.column;
    });
    res.totalCells = n * int(data.columns.size());
    return res;
}
