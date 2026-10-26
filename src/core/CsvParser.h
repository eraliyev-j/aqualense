#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * A parsed, analysis-ready table.
 * Numeric columns are stored column-major; missing / unparseable cells are NaN.
 * When a date/time column is detected, rows are sorted chronologically.
 */
struct Dataset
{
    QString sourceName;
    QString timeColumn;                 // empty when the file has no time axis
    QVector<QDateTime> time;            // one entry per row (empty if no time axis)
    QStringList columns;                // numeric parameter names
    QVector<QVector<double>> values;    // values[column][row]
    QStringList skippedColumns;         // text columns that were ignored
    QStringList warnings;               // non-fatal notes shown to the user
    int rowCount = 0;

    bool hasTime() const { return !time.isEmpty(); }
};

namespace CsvParser
{
/**
 * Parses CSV/TSV bytes into a Dataset.
 *  - delimiter auto-detected: comma, semicolon, tab or pipe
 *  - quoted fields with "" escapes
 *  - UTF-8 (with or without BOM), falling back to the system code page (e.g. CP949)
 *  - first date/time column detected automatically (25+ formats, ISO 8601,
 *    separate Date + Time columns)
 *  - decimal comma supported when the delimiter is not a comma
 */
bool parse(const QByteArray &raw, const QString &sourceName, Dataset &out, QString *error);
}
