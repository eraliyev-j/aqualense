#pragma once

#include "CsvParser.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

enum class AnomalyMethod { Hampel = 0, ZScore = 1, IQR = 2 };

struct AnalysisSettings
{
    AnomalyMethod method = AnomalyMethod::Hampel;
    double threshold = 3.5;   // Hampel / Z-score: sigmas, IQR: fence multiplier k
    int window = 25;          // rolling window (points) for Hampel and the moving average

    QJsonObject toJson() const;
    static AnalysisSettings fromJson(const QJsonObject &obj);
    static double defaultThreshold(AnomalyMethod m);
    static QString methodName(AnomalyMethod m);
};

struct ColumnStats
{
    QString name;
    int count = 0;
    int missing = 0;
    int anomalies = 0;
    double mean = 0, median = 0, stddev = 0, min = 0, max = 0, q1 = 0, q3 = 0, skewness = 0;
    double slope = 0;            // linear trend per day (or per row when there is no time axis)
    bool hasBounds = false;      // static threshold band (Z-score, IQR)
    double lower = 0, upper = 0;
};

struct Anomaly
{
    int column = 0;
    int row = 0;
    double value = 0;
    double expected = 0;         // mean / median / rolling median, depending on method
    double score = 0;            // distance in robust or classical sigmas (or IQRs)
};

struct AnalysisResult
{
    AnalysisSettings settings;
    QVector<ColumnStats> stats;
    QVector<Anomaly> anomalies;            // sorted by row, then column
    QVector<QVector<int>> anomalyRows;     // per column, ascending row indices
    QVector<QVector<double>> smooth;       // centred moving average per column
    QString slopeUnit;                     // "/day" or "/row"
    int totalMissing = 0;
    int totalCells = 0;
};

namespace Analyzer
{
AnalysisResult run(const Dataset &data, const AnalysisSettings &settings);
}
