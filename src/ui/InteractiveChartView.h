#pragma once

#include <QChartView>
#include <QPointer>
#include <QVariant>
#include <QVector>

#include <functional>

class QGraphicsLineItem;
class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QGraphicsSimpleTextItem;

/**
 * QChartView with:
 *   - mouse-wheel zoom around the cursor (Ctrl + wheel zooms the Y axis)
 *   - left-drag panning
 *   - double-click to reset the view
 *   - hover crosshair + tooltip driven by a callback
 */
class InteractiveChartView : public QChartView
{
    Q_OBJECT
public:
    /** Given the value under the cursor, fill `anchor` (value coords) and `text`; return false to hide. */
    using HoverFn = std::function<bool(const QPointF &value, QPointF &anchor, QString &text)>;

    explicit InteractiveChartView(QWidget *parent = nullptr);

    void replaceChart(QChart *chart);                     // deletes the previous chart
    void setHover(QAbstractSeries *reference, HoverFn fn, bool crosshair = true);
    void rememberView();                                  // snapshot axis ranges for reset
    void resetView();
    void focusX(double center, double halfSpan);

protected:
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    void updateHover(const QPoint &viewPos);
    void hideHover();

    struct SavedRange { QPointer<QAbstractAxis> axis; QVariant min, max; };

    QAbstractSeries *m_ref = nullptr;
    HoverFn m_fn;
    bool m_crosshair = true;
    bool m_panning = false;
    QPoint m_last;
    QVector<SavedRange> m_saved;

    QGraphicsLineItem *m_vline = nullptr;
    QGraphicsEllipseItem *m_dot = nullptr;
    QGraphicsPathItem *m_tipBg = nullptr;
    QGraphicsSimpleTextItem *m_tipText = nullptr;
};
