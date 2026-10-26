#pragma once

#include <QFrame>
#include <QList>
#include <QPointF>
#include <QStyledItemDelegate>
#include <QVector>

class QGridLayout;
class QMimeData;

namespace SessionRoles {
enum { Id = Qt::UserRole + 1, Header, Subtitle, Pinned };
}

/** Dashed drag-and-drop target on the welcome page. */
class DropZone : public QFrame
{
    Q_OBJECT
public:
    explicit DropZone(QWidget *parent = nullptr);
    static QString csvPathFromMime(const QMimeData *mime);

signals:
    void fileDropped(const QString &path);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    void setActive(bool active);
};

/** Responsive grid that reflows its cards on resize. */
class CardGrid : public QWidget
{
    Q_OBJECT
public:
    explicit CardGrid(QWidget *parent = nullptr);
    void setCards(const QList<QWidget *> &cards);

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    void relayout(bool force);
    QGridLayout *m_grid;
    QList<QWidget *> m_cards;
    int m_cols = 0;
};

/** Overview card for one parameter, with a sparkline. */
class ParamCard : public QFrame
{
    Q_OBJECT
public:
    ParamCard(int index, const QString &name, const QString &value, const QString &range,
              int anomalies, const QVector<QPointF> &line, const QVector<QPointF> &marks,
              bool timeAxis, QWidget *parent = nullptr);

signals:
    void clicked(int index);

protected:
    void mousePressEvent(QMouseEvent *e) override;

private:
    int m_index;
};

/** Two-line ChatGPT-style rows with date-group headers. */
class SessionDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
};
