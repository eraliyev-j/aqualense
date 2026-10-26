#include "Widgets.h"
#include "Theme.h"

#include <QChart>
#include <QChartView>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QGraphicsLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QMimeData>
#include <QPainter>
#include <QScatterSeries>
#include <QStyle>
#include <QUrl>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>

// ---------------------------------------------------------------- DropZone
DropZone::DropZone(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("DropZone"));
    setAcceptDrops(true);
    setProperty("active", false);
}

QString DropZone::csvPathFromMime(const QMimeData *mime)
{
    if (!mime || !mime->hasUrls())
        return {};
    for (const QUrl &u : mime->urls()) {
        if (!u.isLocalFile())
            continue;
        const QString path = u.toLocalFile();
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == QLatin1String("csv") || ext == QLatin1String("txt") || ext == QLatin1String("tsv"))
            return path;
    }
    return {};
}

void DropZone::setActive(bool active)
{
    setProperty("active", active);
    style()->unpolish(this);
    style()->polish(this);
}

void DropZone::dragEnterEvent(QDragEnterEvent *e)
{
    if (!csvPathFromMime(e->mimeData()).isEmpty()) {
        e->acceptProposedAction();
        setActive(true);
    }
}

void DropZone::dragLeaveEvent(QDragLeaveEvent *e)
{
    setActive(false);
    QFrame::dragLeaveEvent(e);
}

void DropZone::dropEvent(QDropEvent *e)
{
    setActive(false);
    const QString path = csvPathFromMime(e->mimeData());
    if (!path.isEmpty()) {
        e->acceptProposedAction();
        emit fileDropped(path);
    }
}

// ---------------------------------------------------------------- CardGrid
CardGrid::CardGrid(QWidget *parent) : QWidget(parent), m_grid(new QGridLayout(this))
{
    setObjectName(QStringLiteral("ScrollBody"));
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(14);
}

void CardGrid::setCards(const QList<QWidget *> &cards)
{
    for (QWidget *w : std::as_const(m_cards))
        w->deleteLater();
    m_cards = cards;
    for (QWidget *w : std::as_const(m_cards))
        w->setParent(this);
    relayout(true);
}

void CardGrid::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    relayout(false);
}

void CardGrid::relayout(bool force)
{
    const int cols = std::max(1, width() / 300);
    if (!force && cols == m_cols)
        return;
    m_cols = cols;
    while (QLayoutItem *it = m_grid->takeAt(0))
        delete it;
    for (int c = 0; c < 8; ++c)
        m_grid->setColumnStretch(c, c < cols ? 1 : 0);
    for (int i = 0; i < m_cards.size(); ++i) {
        m_grid->addWidget(m_cards[i], i / cols, i % cols);
        m_cards[i]->show();
    }
    const int rows = int((m_cards.size() + cols - 1) / cols);
    for (int r = 0; r <= rows + 1; ++r)
        m_grid->setRowStretch(r, 0);
    m_grid->setRowStretch(rows, 1);
}

// ---------------------------------------------------------------- ParamCard
ParamCard::ParamCard(int index, const QString &name, const QString &value, const QString &range,
                     int anomalies, const QVector<QPointF> &line, const QVector<QPointF> &marks,
                     bool /*timeAxis*/, QWidget *parent)
    : QFrame(parent), m_index(index)
{
    setObjectName(QStringLiteral("ParamCard"));
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(176);
    const ThemePalette &p = Theme::palette();

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(16, 14, 16, 12);
    v->setSpacing(4);

    auto *top = new QHBoxLayout;
    auto *nameLbl = new QLabel(name);
    nameLbl->setObjectName(QStringLiteral("CardName"));
    auto *badge = new QLabel(anomalies > 0 ? QStringLiteral("%1 anomal%2").arg(anomalies).arg(anomalies == 1 ? "y" : "ies")
                                           : QStringLiteral("clean"));
    badge->setObjectName(anomalies > 0 ? QStringLiteral("BadgeDanger") : QStringLiteral("Badge"));
    top->addWidget(nameLbl, 1);
    top->addWidget(badge);
    v->addLayout(top);

    auto *valueLbl = new QLabel(value);
    valueLbl->setObjectName(QStringLiteral("CardValue"));
    v->addWidget(valueLbl);
    auto *rangeLbl = new QLabel(range);
    rangeLbl->setObjectName(QStringLiteral("Small"));
    v->addWidget(rangeLbl);

    // sparkline
    auto *chart = new QChart;
    chart->setBackgroundBrush(p.surface);
    chart->setBackgroundPen(Qt::NoPen);
    chart->setBackgroundRoundness(0);
    chart->setMargins(QMargins(0, 0, 0, 0));
    chart->layout()->setContentsMargins(0, 0, 0, 0);
    chart->legend()->hide();
    chart->setAnimationOptions(QChart::NoAnimation);

    auto *ls = new QLineSeries;
    QPen pen(p.accent);
    pen.setWidthF(1.4);
    ls->setPen(pen);
    ls->replace(QList<QPointF>(line.begin(), line.end()));
    chart->addSeries(ls);

    auto *sc = new QScatterSeries;
    sc->setMarkerSize(5);
    sc->setColor(p.danger);
    sc->setBorderColor(p.danger);
    sc->replace(QList<QPointF>(marks.begin(), marks.end()));
    chart->addSeries(sc);

    auto *ax = new QValueAxis;
    auto *ay = new QValueAxis;
    if (!line.isEmpty()) {
        double x0 = line.first().x(), x1 = line.last().x();
        double y0 = line.first().y(), y1 = y0;
        for (const QPointF &pt : line) {
            y0 = std::min(y0, pt.y());
            y1 = std::max(y1, pt.y());
        }
        const double pad = (y1 - y0) * 0.08 + 1e-9;
        ax->setRange(x0, x1 > x0 ? x1 : x0 + 1);
        ay->setRange(y0 - pad, y1 + pad);
    }
    chart->addAxis(ax, Qt::AlignBottom);
    chart->addAxis(ay, Qt::AlignLeft);
    for (QAbstractSeries *s : chart->series()) {
        s->attachAxis(ax);
        s->attachAxis(ay);
    }
    ax->setVisible(false);
    ay->setVisible(false);

    auto *view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setFrameShape(QFrame::NoFrame);
    view->setFixedHeight(72);
    view->setAttribute(Qt::WA_TransparentForMouseEvents);
    v->addSpacing(4);
    v->addWidget(view);
}

void ParamCard::mousePressEvent(QMouseEvent *e)
{
    QFrame::mousePressEvent(e);
    emit clicked(m_index);
}

// ---------------------------------------------------------------- SessionDelegate
QSize SessionDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const
{
    Q_UNUSED(opt);
    return QSize(200, idx.data(SessionRoles::Header).toBool() ? 32 : 54);
}

void SessionDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const
{
    const ThemePalette &pal = Theme::palette();
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRect r = opt.rect.adjusted(2, 2, -2, -2);

    if (idx.data(SessionRoles::Header).toBool()) {
        QFont f = opt.font;
        f.setPointSizeF(f.pointSizeF() * 0.82);
        f.setBold(true);
        p->setFont(f);
        p->setPen(pal.textMuted);
        p->drawText(r.adjusted(10, 0, 0, -4), Qt::AlignLeft | Qt::AlignBottom,
                    idx.data(Qt::DisplayRole).toString().toUpper());
        p->restore();
        return;
    }

    const bool selected = opt.state & QStyle::State_Selected;
    const bool hover = opt.state & QStyle::State_MouseOver;
    if (selected || hover) {
        QColor bg = pal.surface2;
        if (!selected)
            bg.setAlpha(150);
        p->setPen(Qt::NoPen);
        p->setBrush(bg);
        p->drawRoundedRect(r, 9, 9);
    }
    if (selected) {
        p->setBrush(pal.accent);
        p->drawRoundedRect(QRectF(r.left() + 1, r.top() + 12, 3, r.height() - 24), 1.5, 1.5);
    }

    const QRect textRect = r.adjusted(14, 8, -12, -8);
    QFont tf = opt.font;
    tf.setWeight(selected ? QFont::DemiBold : QFont::Medium);
    p->setFont(tf);
    p->setPen(pal.text);
    const bool pinned = idx.data(SessionRoles::Pinned).toBool();
    const int pinW = pinned ? 14 : 0;
    const QString title = QFontMetrics(tf).elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                                      textRect.width() - pinW);
    p->drawText(textRect, Qt::AlignLeft | Qt::AlignTop, title);
    if (pinned) {
        p->setPen(Qt::NoPen);
        p->setBrush(pal.accent);
        p->drawEllipse(QPointF(textRect.right() - 4, textRect.top() + 8), 3, 3);
    }

    QFont sf = opt.font;
    sf.setPointSizeF(sf.pointSizeF() * 0.86);
    p->setFont(sf);
    p->setPen(pal.textMuted);
    const QString sub = QFontMetrics(sf).elidedText(idx.data(SessionRoles::Subtitle).toString(), Qt::ElideRight,
                                                    textRect.width());
    p->drawText(textRect, Qt::AlignLeft | Qt::AlignBottom, sub);
    p->restore();
}
