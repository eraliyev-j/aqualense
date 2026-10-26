#include "InteractiveChartView.h"
#include "Theme.h"

#include <QAbstractSeries>
#include <QChart>
#include <QDateTime>
#include <QDateTimeAxis>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPainterPath>
#include <QValueAxis>
#include <QWheelEvent>

InteractiveChartView::InteractiveChartView(QWidget *parent) : QChartView(parent)
{
    setRenderHint(QPainter::Antialiasing);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setRubberBand(QChartView::NoRubberBand);
    setDragMode(QGraphicsView::NoDrag);

    m_vline = new QGraphicsLineItem;
    m_dot = new QGraphicsEllipseItem(-4.5, -4.5, 9, 9);
    m_tipBg = new QGraphicsPathItem;
    m_tipText = new QGraphicsSimpleTextItem;
    const QList<QGraphicsItem *> items{m_vline, m_dot, m_tipBg, m_tipText};
    for (QGraphicsItem *it : items) {
        it->setZValue(1000);
        it->setVisible(false);
        scene()->addItem(it);
    }
    m_tipText->setZValue(1001);
}

void InteractiveChartView::replaceChart(QChart *c)
{
    hideHover();
    m_ref = nullptr;
    m_fn = nullptr;
    m_saved.clear();
    QChart *old = chart();
    setChart(c);
    if (old && old != c)
        delete old;
}

void InteractiveChartView::setHover(QAbstractSeries *reference, HoverFn fn, bool crosshair)
{
    m_ref = reference;
    m_fn = std::move(fn);
    m_crosshair = crosshair;
}

void InteractiveChartView::rememberView()
{
    m_saved.clear();
    if (!chart())
        return;
    for (QAbstractAxis *a : chart()->axes()) {
        if (auto *v = qobject_cast<QValueAxis *>(a))
            m_saved.push_back({a, v->min(), v->max()});
        else if (auto *d = qobject_cast<QDateTimeAxis *>(a))
            m_saved.push_back({a, d->min(), d->max()});
    }
}

void InteractiveChartView::resetView()
{
    hideHover();
    for (const SavedRange &s : std::as_const(m_saved))
        if (s.axis)
            s.axis->setRange(s.min, s.max);
}

void InteractiveChartView::focusX(double center, double halfSpan)
{
    resetView();
    if (!chart())
        return;
    for (QAbstractAxis *a : chart()->axes(Qt::Horizontal)) {
        if (auto *d = qobject_cast<QDateTimeAxis *>(a))
            d->setRange(QDateTime::fromMSecsSinceEpoch(qint64(center - halfSpan)),
                        QDateTime::fromMSecsSinceEpoch(qint64(center + halfSpan)));
        else if (auto *v = qobject_cast<QValueAxis *>(a))
            v->setRange(center - halfSpan, center + halfSpan);
    }
}

void InteractiveChartView::wheelEvent(QWheelEvent *e)
{
    if (!chart() || chart()->axes().isEmpty()) {
        e->ignore();
        return;
    }
    const QPointF pos = chart()->mapFromScene(mapToScene(e->position().toPoint()));
    const QRectF pa = chart()->plotArea();
    if (!pa.contains(pos) || e->angleDelta().y() == 0) {
        e->ignore();
        return;
    }
    const double f = e->angleDelta().y() > 0 ? 0.8 : 1.25;
    QRectF r = pa;
    if (e->modifiers() & Qt::ControlModifier) {
        r.setTop(pos.y() - (pos.y() - pa.top()) * f);
        r.setHeight(pa.height() * f);
    } else {
        r.setLeft(pos.x() - (pos.x() - pa.left()) * f);
        r.setWidth(pa.width() * f);
    }
    chart()->zoomIn(r);
    hideHover();
    e->accept();
}

void InteractiveChartView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && chart() && !chart()->axes().isEmpty()) {
        const QPointF pos = chart()->mapFromScene(mapToScene(e->pos()));
        if (chart()->plotArea().contains(pos)) {
            m_panning = true;
            m_last = e->pos();
            setCursor(Qt::ClosedHandCursor);
            hideHover();
            e->accept();
            return;
        }
    }
    QChartView::mousePressEvent(e);
}

void InteractiveChartView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_panning) {
        const QPoint d = e->pos() - m_last;
        m_last = e->pos();
        chart()->scroll(-d.x(), d.y());
        e->accept();
        return;
    }
    updateHover(e->pos());
    QChartView::mouseMoveEvent(e);
}

void InteractiveChartView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning && e->button() == Qt::LeftButton) {
        m_panning = false;
        unsetCursor();
        updateHover(e->pos());
        e->accept();
        return;
    }
    QChartView::mouseReleaseEvent(e);
}

void InteractiveChartView::mouseDoubleClickEvent(QMouseEvent *e)
{
    resetView();
    e->accept();
}

void InteractiveChartView::leaveEvent(QEvent *e)
{
    hideHover();
    QChartView::leaveEvent(e);
}

void InteractiveChartView::hideHover()
{
    m_vline->setVisible(false);
    m_dot->setVisible(false);
    m_tipBg->setVisible(false);
    m_tipText->setVisible(false);
}

void InteractiveChartView::updateHover(const QPoint &viewPos)
{
    if (!m_fn || !m_ref || !chart()) {
        hideHover();
        return;
    }
    const QPointF chartPos = chart()->mapFromScene(mapToScene(viewPos));
    const QRectF pa = chart()->plotArea();
    if (!pa.contains(chartPos)) {
        hideHover();
        return;
    }

    QPointF anchorValue;
    QString text;
    if (!m_fn(chart()->mapToValue(chartPos, m_ref), anchorValue, text)) {
        hideHover();
        return;
    }

    const ThemePalette &p = Theme::palette();
    const QRectF paScene = chart()->mapRectToScene(pa);
    const QPointF a = chart()->mapToScene(chart()->mapToPosition(anchorValue, m_ref));

    if (m_crosshair) {
        QPen lp(p.textMuted, 1, Qt::DashLine);
        m_vline->setPen(lp);
        m_vline->setLine(a.x(), paScene.top(), a.x(), paScene.bottom());
        m_vline->setVisible(true);
    }
    m_dot->setBrush(p.accent);
    m_dot->setPen(QPen(p.surface, 2));
    m_dot->setPos(a);
    m_dot->setVisible(paScene.contains(a));

    m_tipText->setFont(font());
    m_tipText->setBrush(p.text);
    m_tipText->setText(text);
    const QRectF tr = m_tipText->boundingRect();
    const double w = tr.width() + 22, h = tr.height() + 16;
    double x = a.x() + 14, y = a.y() - h - 10;
    if (x + w > paScene.right())
        x = a.x() - w - 14;
    if (x < paScene.left())
        x = paScene.left() + 4;
    if (y < paScene.top())
        y = a.y() + 14;
    if (y + h > paScene.bottom())
        y = paScene.bottom() - h - 4;

    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, w, h), 8, 8);
    QColor bg = p.surface2;
    bg.setAlpha(245);
    m_tipBg->setPath(path);
    m_tipBg->setBrush(bg);
    m_tipBg->setPen(QPen(p.border));
    m_tipBg->setPos(x, y);
    m_tipText->setPos(x + 11, y + 8);
    m_tipBg->setVisible(true);
    m_tipText->setVisible(true);
}
