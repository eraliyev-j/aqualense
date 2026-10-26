#pragma once

#include "CsvParser.h"

#include <QColor>
#include <QString>
#include <QVector>

/**
 * Korean Water Quality Index (수질등급) based on the living-environment standards
 * (생활환경기준) of the Framework Act on Environmental Policy, as published by the
 * National Institute of Environmental Research (NIER):
 *   https://water.nier.go.kr/web/contents/contentView/?pMENU_NO=37
 *
 * Seven grades: Ia (매우 좋음), Ib (좋음), II (약간 좋음), III (보통),
 *               IV (약간 나쁨), V (나쁨), VI (매우 나쁨).
 *
 * Each parameter column that can be matched to a standard (pH, BOD, COD, TOC, SS, DO,
 * T-P, T-N, Chl-a, coliforms) is graded sample by sample, and its representative value
 * (mean or median) is graded. The overall grade follows the worst-parameter rule
 * (the grade of the most limiting parameter).
 */
namespace KoreanWqi
{
enum class WaterBody { River = 0, Lake = 1 };
enum class Statistic { Mean = 0, Median = 1 };

enum class Param { None = -1, PH, BOD, COD, TOC, SS, DO, TP, TN, ChlA, TotalColiform, FecalColiform };

/** How a value compares with the grade limits. */
enum class Direction {
    Max,    // value ≤ limit meets the grade (BOD, TOC, T-P, …)
    Min,    // value ≥ limit meets the grade (DO)
    Range,  // lo ≤ value ≤ hi meets the grade (pH)
};

constexpr int kGrades = 7;            // Ia, Ib, II, III, IV, V, VI
constexpr int kWorst = kGrades - 1;   // index of VI

QString gradeCode(int g);             // "Ia" … "VI"
QString gradeNameKo(int g);           // "매우 좋음" …
QString gradeNameEn(int g);           // "Very good" …
QString gradeUse(int g);              // intended use, short English description
QColor gradeColor(int g);             // NIER grade colour (character guide, converted from CMYK)

/** Limits of one parameter for one water body. NaN = no numeric limit for that grade. */
struct Standard
{
    Param param = Param::None;
    QString label;            // "TOC"
    QString nameKo;           // "총유기탄소량"
    QString unit;             // "mg/L"
    Direction dir = Direction::Max;
    double limit[kGrades];    // Max / Min: limit per grade (VI is open-ended, NaN)
    double lo[kGrades];       // Range only
    double hi[kGrades];       // Range only
    QString note;             // footnote shown on the card
};

/** Standard for a parameter, or nullptr when the water body has no standard for it. */
const Standard *standardFor(Param p, WaterBody body);

/** Matches a column name such as "DO_mgL", "T-P", "총인", "유입DO (mg/L)" to a parameter. */
Param detect(const QString &columnName);

/** Grade of a single value (0 = Ia … 6 = VI), or -1 for NaN. */
int gradeOf(const Standard &s, double value);

struct ParamResult
{
    int column = -1;
    Param param = Param::None;
    const Standard *standard = nullptr;
    int count = 0;
    double mean = 0, median = 0, min = 0, max = 0, q1 = 0, q3 = 0, p05 = 0, p95 = 0;
    double value = 0;                 // representative value (mean or median)
    int grade = -1;                   // grade of the representative value
    double share[kGrades] = {};       // fraction of samples in each grade (sums to 1)
    double compliance = 0;            // fraction of samples meeting grade III (보통) or better
    bool included = true;             // counts towards the overall grade
    QString status;                   // why a parameter is excluded, if it is
};

struct Result
{
    WaterBody body = WaterBody::River;
    Statistic statistic = Statistic::Mean;
    QVector<ParamResult> params;      // matched columns, in file order
    QStringList unmatched;            // numeric columns with no Korean standard
    int overallGrade = -1;            // worst grade of the included parameters
    QString limiting;                 // label(s) of the parameter(s) that set the overall grade
    bool hasData() const { return !params.isEmpty(); }
};

Result evaluate(const Dataset &data, WaterBody body, Statistic statistic);

QString bodyName(WaterBody b);        // "River (하천)"
QString sourceUrl();
}
