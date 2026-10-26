#pragma once

#include "KoreanWqi.h"

#include <QFrame>
#include <QWidget>

/**
 * Horizontal grade scale for one parameter: one coloured band per grade range
 * (Ia … VI, equal widths, limits written under the band) with the data overlaid as a
 * box plot (P5–P95 whiskers, Q1–Q3 box) and a marker for the representative value.
 * Bands run from the best grade (left) to the worst (right); for pH they follow the value.
 */
class GradeScale : public QWidget
{
public:
    explicit GradeScale(const KoreanWqi::ParamResult &r, QWidget *parent = nullptr);
    QSize sizeHint() const override { return {280, 86}; }
    QSize minimumSizeHint() const override { return {200, 86}; }

    struct Segment { double v0, v1; int gFirst, gLast; };

protected:
    void paintEvent(QPaintEvent *) override;

private:
    double xFor(double value, double left, double width) const;

    KoreanWqi::ParamResult m_r;
    QVector<Segment> m_segments;
};

/** 100 % stacked bar: share of samples in each grade. */
class GradeShareBar : public QWidget
{
public:
    explicit GradeShareBar(const double (&share)[KoreanWqi::kGrades], QWidget *parent = nullptr);
    QSize sizeHint() const override { return {280, 12}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    double m_share[KoreanWqi::kGrades];
};

/** Overview card for one parameter on the Korean WQI page. */
class WqiCard : public QFrame
{
    Q_OBJECT
public:
    WqiCard(const QString &column, const KoreanWqi::ParamResult &r, KoreanWqi::Statistic stat,
            QWidget *parent = nullptr);

signals:
    void clicked(int column);

protected:
    void mousePressEvent(QMouseEvent *e) override;

private:
    int m_column;
};

/** Seven-grade colour legend (Ia … VI). */
class GradeLegend : public QWidget
{
public:
    explicit GradeLegend(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {640, 46}; }
    QSize minimumSizeHint() const override { return {420, 46}; }

protected:
    void paintEvent(QPaintEvent *) override;
};

/** Pill with a grade code on the grade colour. */
QString gradePillStyle(int grade, int fontPx = 11, int padV = 2, int padH = 9);
