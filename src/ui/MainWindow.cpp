#include "MainWindow.h"
#include "InteractiveChartView.h"
#include "Theme.h"
#include "Widgets.h"
#include "WqiWidgets.h"

#include <QApplication>
#include <QAreaSeries>
#include <QChart>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTimeAxis>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLegendMarker>
#include <QLineEdit>
#include <QLineSeries>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QScatterSeries>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTabBar>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QValueAxis>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxPlotPoints = 4000;
constexpr int kMaxTableRows = 5000;

// ---- formatting -----------------------------------------------------------
QString fmtNum(double v)
{
    if (!std::isfinite(v))
        return QStringLiteral("—");
    const double a = std::fabs(v);
    if (a == 0)
        return QStringLiteral("0");
    if (a >= 10000) return QString::number(v, 'f', 0);
    if (a >= 100)   return QString::number(v, 'f', 1);
    if (a >= 1)     return QString::number(v, 'f', 2);
    if (a >= 0.01)  return QString::number(v, 'f', 3);
    return QString::number(v, 'g', 3);
}

QString fmtInt(qint64 v) { return QLocale(QLocale::English).toString(v); }

QString fmtDuration(qint64 ms)
{
    const double h = ms / 3600000.0;
    if (h < 1)  return QStringLiteral("%1 min").arg(qRound(ms / 60000.0));
    if (h < 48) return QStringLiteral("%1 h").arg(QString::number(h, 'f', h < 10 ? 1 : 0));
    return QStringLiteral("%1 days").arg(QString::number(h / 24.0, 'f', h / 24.0 < 10 ? 1 : 0));
}

QString fmtInterval(qint64 ms)
{
    if (ms <= 0)         return QStringLiteral("irregular");
    if (ms < 60000)      return QStringLiteral("%1 s").arg(QString::number(ms / 1000.0, 'g', 3));
    if (ms < 3600000)    return QStringLiteral("%1 min").arg(QString::number(ms / 60000.0, 'g', 3));
    if (ms < 86400000)   return QStringLiteral("%1 h").arg(QString::number(ms / 3600000.0, 'g', 3));
    return QStringLiteral("%1 d").arg(QString::number(ms / 86400000.0, 'g', 3));
}

QString dtAxisFormat(qint64 spanMs)
{
    const qint64 day = 86400000;
    if (spanMs > 150 * day) return QStringLiteral("yyyy-MM");
    if (spanMs > 4 * day)   return QStringLiteral("MM-dd");
    if (spanMs > day)       return QStringLiteral("MM-dd HH:mm");
    return QStringLiteral("HH:mm");
}

QString csvField(QString s)
{
    if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\n'))) {
        s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + s + QLatin1Char('"');
    }
    return s;
}

// ---- charts -----------------------------------------------------------------
QChart *makeChart()
{
    const ThemePalette &p = Theme::palette();
    auto *c = new QChart;
    c->setAnimationOptions(QChart::NoAnimation);
    c->setBackgroundBrush(p.surface);
    c->setBackgroundPen(Qt::NoPen);
    c->setBackgroundRoundness(0);
    c->setMargins(QMargins(4, 4, 8, 4));
    c->legend()->setAlignment(Qt::AlignTop);
    c->legend()->setLabelColor(p.textMuted);
    c->legend()->setMarkerShape(QLegend::MarkerShapeFromSeries);
    return c;
}

void styleAxis(QAbstractAxis *a)
{
    const ThemePalette &p = Theme::palette();
    QFont f = qApp->font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    a->setLabelsFont(f);
    a->setTitleFont(f);
    a->setLabelsColor(p.textMuted);
    a->setTitleBrush(p.textMuted);
    a->setLinePenColor(p.border);
    a->setGridLineColor(p.grid);
    a->setMinorGridLineVisible(false);
}

/** Min/max bucket decimation: keeps spikes visible while limiting point count. */
QVector<int> decimate(const QVector<double> &v, int maxPoints)
{
    QVector<int> finite;
    finite.reserve(v.size());
    for (int i = 0; i < v.size(); ++i)
        if (std::isfinite(v[i]))
            finite << i;
    if (finite.size() <= maxPoints)
        return finite;

    QVector<int> out;
    const int buckets = maxPoints / 2;
    const double step = double(finite.size()) / buckets;
    for (int b = 0; b < buckets; ++b) {
        const int s = int(b * step), e = std::min(int(finite.size()), int((b + 1) * step));
        if (s >= e)
            continue;
        int lo = finite[s], hi = finite[s];
        for (int k = s; k < e; ++k) {
            const int i = finite[k];
            if (v[i] < v[lo]) lo = i;
            if (v[i] > v[hi]) hi = i;
        }
        out << std::min(lo, hi);
        if (lo != hi)
            out << std::max(lo, hi);
    }
    return out;
}

QLabel *mutedLabel(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("Muted"));
    return l;
}

/** Table item that sorts numerically but shows formatted text. */
class NumItem : public QTableWidgetItem
{
public:
    NumItem(double v, const QString &text) : QTableWidgetItem(text)
    {
        setData(Qt::UserRole + 7, v);
        setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    bool operator<(const QTableWidgetItem &o) const override
    {
        return data(Qt::UserRole + 7).toDouble() < o.data(Qt::UserRole + 7).toDouble();
    }
};

QTableWidget *makeTable(const QStringList &headers)
{
    auto *t = new QTableWidget(0, int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->setAlternatingRowColors(true);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setShowGrid(false);
    t->setWordWrap(false);
    t->horizontalHeader()->setHighlightSections(false);
    t->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    t->verticalHeader()->setDefaultSectionSize(34);
    t->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    return t;
}

QFrame *wrapCard(QWidget *inner, const char *name = "ChartCard", int margin = 10)
{
    auto *card = new QFrame;
    card->setObjectName(QLatin1String(name));
    auto *l = new QVBoxLayout(card);
    l->setContentsMargins(margin, margin, margin, margin);
    l->addWidget(inner);
    return card;
}

} // namespace

// ============================================================================
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("AquaLens — Water Quality Analyzer"));
    setMinimumSize(1120, 720);
    setAcceptDrops(true);

    QSettings settings;
    Theme::setMode(settings.value(QStringLiteral("theme")).toString() == QLatin1String("light")
                       ? Theme::Mode::Light : Theme::Mode::Dark);
    qApp->setStyleSheet(Theme::styleSheet());

    auto *central = new QWidget;
    central->setObjectName(QStringLiteral("Central"));
    auto *h = new QHBoxLayout(central);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(buildSidebar());

    auto *main = new QWidget;
    main->setObjectName(QStringLiteral("Main"));
    auto *v = new QVBoxLayout(main);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_busy = new QProgressBar;
    m_busy->setObjectName(QStringLiteral("Busy"));
    m_busy->setRange(0, 0);
    m_busy->setTextVisible(false);
    m_busy->setFixedHeight(3);
    QSizePolicy sp = m_busy->sizePolicy();
    sp.setRetainSizeWhenHidden(true);
    m_busy->setSizePolicy(sp);
    m_busy->hide();
    v->addWidget(m_busy);

    m_stack = new QStackedWidget;
    m_stack->addWidget(buildWelcome());
    m_stack->addWidget(buildWorkspace());
    v->addWidget(m_stack, 1);
    h->addWidget(main, 1);
    setCentralWidget(central);

    m_toast = new QLabel(central);
    m_toast->setObjectName(QStringLiteral("Toast"));
    m_toast->hide();
    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(350);
    connect(m_debounce, &QTimer::timeout, this, &MainWindow::runReanalysis);
    connect(&m_watcher, &QFutureWatcher<JobResult>::finished, this, &MainWindow::onJobFinished);

    QString err;
    m_dbOk = m_db.open(&err);
    if (!m_dbOk)
        QTimer::singleShot(0, this, [this, err] {
            QMessageBox::warning(this, QStringLiteral("History unavailable"),
                                 QStringLiteral("The history database could not be opened. Analyses will "
                                                "work but will not be saved.\n\n%1").arg(err));
        });
    refreshSessions();
    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
}

MainWindow::~MainWindow()
{
    m_watcher.waitForFinished();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    QSettings().setValue(QStringLiteral("geometry"), saveGeometry());
    QMainWindow::closeEvent(e);
}

// ============================================================================
// Layout
// ============================================================================
QWidget *MainWindow::buildSidebar()
{
    auto *side = new QWidget;
    side->setObjectName(QStringLiteral("Sidebar"));
    side->setAttribute(Qt::WA_StyledBackground);
    side->setFixedWidth(286);
    auto *v = new QVBoxLayout(side);
    v->setContentsMargins(14, 16, 14, 12);
    v->setSpacing(10);

    auto *brand = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(Theme::appPixmap(56).scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto *name = new QLabel(QStringLiteral("AquaLens"));
    name->setObjectName(QStringLiteral("Brand"));
    brand->addWidget(icon);
    brand->addSpacing(4);
    brand->addWidget(name);
    brand->addStretch();
    v->addLayout(brand);
    v->addSpacing(4);

    auto *newBtn = new QPushButton(QStringLiteral("+   New analysis"));
    newBtn->setObjectName(QStringLiteral("NewBtn"));
    newBtn->setCursor(Qt::PointingHandCursor);
    newBtn->setShortcut(QKeySequence::New);
    newBtn->setToolTip(QStringLiteral("Start a new analysis (Ctrl+N)"));
    connect(newBtn, &QPushButton::clicked, this, &MainWindow::newAnalysis);
    v->addWidget(newBtn);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Search history"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, [this] { refreshSessions(); });
    v->addWidget(m_search);

    m_sessionList = new QListWidget;
    m_sessionList->setObjectName(QStringLiteral("SessionList"));
    m_sessionList->setItemDelegate(new SessionDelegate(m_sessionList));
    m_sessionList->setMouseTracking(true);
    m_sessionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_sessionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sessionList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sessionList, &QListWidget::itemClicked, this, &MainWindow::onSessionClicked);
    connect(m_sessionList, &QListWidget::itemActivated, this, &MainWindow::onSessionClicked);
    connect(m_sessionList, &QListWidget::customContextMenuRequested, this, &MainWindow::onSessionContextMenu);
    v->addWidget(m_sessionList, 1);

    m_emptyHistory = mutedLabel(QStringLiteral("No analyses yet.\nUpload a CSV to get started."));
    m_emptyHistory->setAlignment(Qt::AlignCenter);
    v->addWidget(m_emptyHistory);
    v->addStretch(0);

    auto *footer = new QHBoxLayout;
    m_themeBtn = new QPushButton;
    m_themeBtn->setObjectName(QStringLiteral("FlatBtn"));
    m_themeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_themeBtn, &QPushButton::clicked, this, &MainWindow::toggleTheme);
    auto *about = new QPushButton(QStringLiteral("About"));
    about->setObjectName(QStringLiteral("FlatBtn"));
    about->setCursor(Qt::PointingHandCursor);
    connect(about, &QPushButton::clicked, this, &MainWindow::showAbout);
    footer->addWidget(m_themeBtn);
    footer->addStretch();
    footer->addWidget(about);
    v->addLayout(footer);
    updateThemeButton();
    return side;
}

QWidget *MainWindow::buildWelcome()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("Page"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(48, 40, 48, 40);
    outer->addStretch(1);

    auto *col = new QWidget;
    col->setMaximumWidth(760);
    auto *v = new QVBoxLayout(col);
    v->setSpacing(12);

    auto *icon = new QLabel;
    icon->setPixmap(Theme::appPixmap(144).scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    icon->setAlignment(Qt::AlignCenter);
    v->addWidget(icon);
    auto *hero = new QLabel(QStringLiteral("Water Quality Analyzer"));
    hero->setObjectName(QStringLiteral("Hero"));
    hero->setAlignment(Qt::AlignCenter);
    v->addWidget(hero);
    auto *sub = new QLabel(QStringLiteral("Upload a CSV dataset to explore statistics, trends and anomalies.\n"
                                          "Every analysis is saved to your history, just like a chat."));
    sub->setObjectName(QStringLiteral("Subtitle"));
    sub->setAlignment(Qt::AlignCenter);
    v->addWidget(sub);
    v->addSpacing(16);

    m_dropZone = new DropZone;
    m_dropZone->setMinimumHeight(230);
    auto *dz = new QVBoxLayout(m_dropZone);
    dz->setSpacing(10);
    dz->addStretch();
    auto *dropTitle = new QLabel(QStringLiteral("Drop a CSV file here"));
    dropTitle->setObjectName(QStringLiteral("DropTitle"));
    dropTitle->setAlignment(Qt::AlignCenter);
    dz->addWidget(dropTitle);
    auto *orLbl = mutedLabel(QStringLiteral("or"));
    orLbl->setAlignment(Qt::AlignCenter);
    dz->addWidget(orLbl);
    auto *btns = new QHBoxLayout;
    btns->addStretch();
    auto *browse = new QPushButton(QStringLiteral("Browse files"));
    browse->setObjectName(QStringLiteral("Primary"));
    browse->setCursor(Qt::PointingHandCursor);
    browse->setShortcut(QKeySequence::Open);
    connect(browse, &QPushButton::clicked, this, &MainWindow::browseFile);
    auto *sample = new QPushButton(QStringLiteral("Try sample dataset"));
    sample->setCursor(Qt::PointingHandCursor);
    connect(sample, &QPushButton::clicked, this, &MainWindow::openSample);
    btns->addWidget(browse);
    btns->addSpacing(8);
    btns->addWidget(sample);
    btns->addStretch();
    dz->addLayout(btns);
    dz->addStretch();
    connect(m_dropZone, &DropZone::fileDropped, this, &MainWindow::loadFile);
    v->addWidget(m_dropZone);

    auto *hint = new QLabel(QStringLiteral("Header in the first row  ·  date/time column detected automatically  ·  "
                                           "comma, semicolon or tab delimited  ·  UTF-8 or system encoding"));
    hint->setObjectName(QStringLiteral("Small"));
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    v->addWidget(hint);

    outer->addWidget(col, 0, Qt::AlignHCenter);
    outer->addStretch(2);
    return page;
}

QWidget *MainWindow::makeKpi(const QString &caption, QLabel *&value, QLabel *&hint)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("KpiCard"));
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(16, 12, 16, 12);
    v->setSpacing(2);
    auto *c = new QLabel(caption.toUpper());
    c->setObjectName(QStringLiteral("KpiCaption"));
    value = new QLabel(QStringLiteral("—"));
    value->setObjectName(QStringLiteral("KpiValue"));
    hint = new QLabel;
    hint->setObjectName(QStringLiteral("KpiHint"));
    v->addWidget(c);
    v->addWidget(value);
    v->addWidget(hint);
    return card;
}

QWidget *MainWindow::buildWorkspace()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("Page"));
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(28, 20, 28, 20);
    v->setSpacing(16);

    // ---- header -------------------------------------------------------------
    auto *head = new QHBoxLayout;
    head->setSpacing(8);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(2);
    m_titleLbl = new QLabel;
    m_titleLbl->setObjectName(QStringLiteral("Title"));
    m_subtitleLbl = new QLabel;
    m_subtitleLbl->setObjectName(QStringLiteral("Subtitle"));
    m_subtitleLbl->setMinimumWidth(0);
    m_subtitleLbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_warnLbl = new QLabel;
    m_warnLbl->setObjectName(QStringLiteral("Warn"));
    m_warnLbl->setCursor(Qt::WhatsThisCursor);
    auto *subRow = new QHBoxLayout;
    subRow->setSpacing(12);
    subRow->addWidget(m_subtitleLbl, 1);
    subRow->addWidget(m_warnLbl);
    subRow->addStretch();
    titles->addWidget(m_titleLbl);
    titles->addLayout(subRow);
    head->addLayout(titles, 1);

    m_method = new QComboBox;
    m_method->addItem(AnalysisSettings::methodName(AnomalyMethod::Hampel), int(AnomalyMethod::Hampel));
    m_method->addItem(AnalysisSettings::methodName(AnomalyMethod::ZScore), int(AnomalyMethod::ZScore));
    m_method->addItem(AnalysisSettings::methodName(AnomalyMethod::IQR), int(AnomalyMethod::IQR));
    m_method->setItemData(0, QStringLiteral("Rolling median ± t·MAD. Robust to trends and daily cycles — best for sensor spikes."), Qt::ToolTipRole);
    m_method->setItemData(1, QStringLiteral("|x − mean| > t·σ over the whole series."), Qt::ToolTipRole);
    m_method->setItemData(2, QStringLiteral("Outside [Q1 − k·IQR, Q3 + k·IQR] (Tukey fences)."), Qt::ToolTipRole);
    m_threshold = new QDoubleSpinBox;
    m_threshold->setRange(0.5, 10.0);
    m_threshold->setSingleStep(0.1);
    m_threshold->setDecimals(1);
    m_threshold->setToolTip(QStringLiteral("Sensitivity: lower values flag more points"));
    m_window = new QSpinBox;
    m_window->setRange(3, 1001);
    m_window->setSingleStep(2);
    m_window->setSuffix(QStringLiteral(" pts"));
    m_window->setToolTip(QStringLiteral("Rolling window for the Hampel filter and the moving average"));
    connect(m_method, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onMethodChanged);
    connect(m_threshold, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::onSettingsEdited);
    connect(m_window, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::onSettingsEdited);

    auto *exportBtn = new QPushButton(QStringLiteral("Export  ▾"));
    auto *menu = new QMenu(exportBtn);
    menu->addAction(QStringLiteral("Chart as PNG…"), this, &MainWindow::exportChart);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Statistics (CSV)…"), this, &MainWindow::exportStats);
    menu->addAction(QStringLiteral("Anomalies (CSV)…"), this, &MainWindow::exportAnomalies);
    menu->addAction(QStringLiteral("Korean WQI grades (CSV)…"), this, &MainWindow::exportWqi);
    exportBtn->setMenu(menu);

    head->addWidget(mutedLabel(QStringLiteral("Detection")));
    head->addWidget(m_method);
    head->addSpacing(6);
    head->addWidget(mutedLabel(QStringLiteral("Threshold")));
    head->addWidget(m_threshold);
    head->addSpacing(6);
    head->addWidget(mutedLabel(QStringLiteral("Window")));
    head->addWidget(m_window);
    head->addSpacing(10);
    head->addWidget(exportBtn);
    v->addLayout(head);

    // ---- KPIs ---------------------------------------------------------------
    auto *kpis = new QHBoxLayout;
    kpis->setSpacing(12);
    const QStringList captions = {QStringLiteral("Rows"), QStringLiteral("Parameters"), QStringLiteral("Time span"),
                                  QStringLiteral("Missing"), QStringLiteral("Anomalies")};
    for (int i = 0; i < 5; ++i)
        kpis->addWidget(makeKpi(captions[i], m_kpiValue[i], m_kpiHint[i]), 1);
    v->addLayout(kpis);

    // ---- tab row ------------------------------------------------------------
    auto *tabRow = new QHBoxLayout;
    m_tabBar = new QTabBar;
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setDocumentMode(true);
    for (const char *t : {"Overview", "Time series", "Distribution", "Anomalies", "Statistics", "Korean WQI"})
        m_tabBar->addTab(QLatin1String(t));
    tabRow->addWidget(m_tabBar);
    tabRow->addStretch();
    m_paramBox = new QWidget;
    auto *pl = new QHBoxLayout(m_paramBox);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(mutedLabel(QStringLiteral("Parameter")));
    m_param = new QComboBox;
    m_param->setMinimumWidth(220);
    m_param->setMaxVisibleItems(20);
    connect(m_param, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onParameterChanged);
    pl->addWidget(m_param);
    tabRow->addWidget(m_paramBox);
    v->addLayout(tabRow);

    m_pages = new QStackedWidget;
    connect(m_tabBar, &QTabBar::currentChanged, this, &MainWindow::onTabChanged);

    // Overview
    {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_cards = new CardGrid;
        scroll->setWidget(m_cards);
        scroll->viewport()->setAutoFillBackground(false);
        m_pages->addWidget(scroll);
    }
    // Time series
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(10);
        auto *opts = new QHBoxLayout;
        m_showSmooth = new QCheckBox(QStringLiteral("Moving average"));
        m_showBand = new QCheckBox(QStringLiteral("Threshold band"));
        m_showAnoms = new QCheckBox(QStringLiteral("Anomalies"));
        for (QCheckBox *cb : {m_showSmooth, m_showBand, m_showAnoms}) {
            cb->setChecked(true);
            connect(cb, &QCheckBox::toggled, this, [this] { renderTimeSeries(); });
            opts->addWidget(cb);
            opts->addSpacing(8);
        }
        opts->addStretch();
        opts->addWidget(mutedLabel(QStringLiteral("Scroll to zoom  ·  Ctrl+scroll zooms Y  ·  drag to pan  ·  double-click to reset")));
        l->addLayout(opts);
        m_tsView = new InteractiveChartView;
        l->addWidget(wrapCard(m_tsView), 1);
        m_pages->addWidget(w);
    }
    // Distribution
    {
        auto *w = new QWidget;
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(14);
        m_distView = new InteractiveChartView;
        l->addWidget(wrapCard(m_distView), 1);
        m_distStats = new QLabel;
        m_distStats->setTextFormat(Qt::RichText);
        m_distStats->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        auto *side = wrapCard(m_distStats, "Card", 18);
        side->setFixedWidth(270);
        l->addWidget(side);
        m_pages->addWidget(w);
    }
    // Anomalies
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(10);
        auto *row = new QHBoxLayout;
        row->addWidget(mutedLabel(QStringLiteral("Show")));
        m_anomFilter = new QComboBox;
        m_anomFilter->setMinimumWidth(200);
        connect(m_anomFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { renderAnomalyTable(); });
        row->addWidget(m_anomFilter);
        row->addSpacing(10);
        m_anomCount = mutedLabel(QString());
        row->addWidget(m_anomCount);
        row->addStretch();
        row->addWidget(mutedLabel(QStringLiteral("Double-click a row to locate it on the chart")));
        auto *exp = new QPushButton(QStringLiteral("Export CSV"));
        connect(exp, &QPushButton::clicked, this, &MainWindow::exportAnomalies);
        row->addWidget(exp);
        l->addLayout(row);
        m_anomTable = makeTable({QStringLiteral("Time"), QStringLiteral("Parameter"), QStringLiteral("Value"),
                                 QStringLiteral("Expected"), QStringLiteral("Deviation"), QStringLiteral("Score")});
        m_anomTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        connect(m_anomTable, &QTableWidget::cellDoubleClicked, this, &MainWindow::onAnomalyActivated);
        l->addWidget(m_anomTable, 1);
        m_anomEmpty = mutedLabel(QStringLiteral("No anomalies detected with the current settings."));
        m_anomEmpty->setAlignment(Qt::AlignCenter);
        l->addWidget(m_anomEmpty, 1);
        m_pages->addWidget(w);
    }
    // Statistics
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(10);
        auto *row = new QHBoxLayout;
        row->addWidget(mutedLabel(QStringLiteral("Descriptive statistics per parameter  ·  click a header to sort")));
        row->addStretch();
        auto *exp = new QPushButton(QStringLiteral("Export CSV"));
        connect(exp, &QPushButton::clicked, this, &MainWindow::exportStats);
        row->addWidget(exp);
        l->addLayout(row);
        m_statsTable = makeTable({QStringLiteral("Parameter"), QStringLiteral("Count"), QStringLiteral("Missing"),
                                  QStringLiteral("Mean"), QStringLiteral("Median"), QStringLiteral("Std dev"),
                                  QStringLiteral("Min"), QStringLiteral("Q1"), QStringLiteral("Q3"),
                                  QStringLiteral("Max"), QStringLiteral("Trend"), QStringLiteral("Anomalies")});
        m_statsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_statsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_statsTable->horizontalHeader()->setSectionResizeMode(10, QHeaderView::ResizeToContents);
        m_statsTable->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder); // keep file order
        l->addWidget(m_statsTable, 1);
        m_pages->addWidget(w);
    }

    // Korean WQI
    m_pages->addWidget(buildWqiPage());

    v->addWidget(m_pages, 1);
    onTabChanged(0);
    applySettingsToControls(AnalysisSettings{});
    return page;
}

// ============================================================================
// Loading & jobs
// ============================================================================
void MainWindow::newAnalysis()
{
    m_sessionId = -1;
    m_hasData = false;
    m_data = Dataset{};
    m_result = AnalysisResult{};
    m_sessionList->clearSelection();
    m_stack->setCurrentIndex(0);
}

void MainWindow::browseFile()
{
    QSettings s;
    const QString dir = s.value(QStringLiteral("lastDir"),
                                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open water quality dataset"), dir,
                                                      QStringLiteral("CSV files (*.csv *.txt *.tsv);;All files (*)"));
    if (path.isEmpty())
        return;
    s.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());
    loadFile(path);
}

void MainWindow::openSample()
{
    QFile f(QStringLiteral(":/data/sample_water_quality.csv"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    Job job;
    job.kind = JobKind::NewFile;
    job.fileName = QStringLiteral("sample_water_quality.csv");
    job.title = QStringLiteral("Sample stream data");
    job.csv = f.readAll();
    job.settings = AnalysisSettings{};
    startJob(job);
}

void MainWindow::loadFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Cannot open file"), f.errorString());
        return;
    }
    if (f.size() > 300LL * 1024 * 1024) {
        QMessageBox::warning(this, QStringLiteral("File too large"),
                             QStringLiteral("Files larger than 300 MB are not supported."));
        return;
    }
    Job job;
    job.kind = JobKind::NewFile;
    job.fileName = QFileInfo(path).fileName();
    job.title = QFileInfo(path).completeBaseName();
    job.csv = f.readAll();
    job.settings = AnalysisSettings{};
    startJob(job);
}

void MainWindow::startJob(const Job &job)
{
    if (m_watcher.isRunning()) {
        if (job.kind == JobKind::Reanalyze)
            m_rerunPending = true;
        else
            showToast(QStringLiteral("Please wait — an analysis is still running"));
        return;
    }
    m_job = job;
    setBusy(true);
    const Dataset current = m_data;
    m_watcher.setFuture(QtConcurrent::run([job, current]() {
        JobResult r;
        if (job.kind == JobKind::Reanalyze) {
            r.dataset = current;
        } else if (!CsvParser::parse(job.csv, job.fileName, r.dataset, &r.error)) {
            return r;
        }
        r.result = Analyzer::run(r.dataset, job.settings);
        r.ok = true;
        return r;
    }));
}

void MainWindow::onJobFinished()
{
    setBusy(false);
    JobResult r = m_watcher.result();
    if (!r.ok) {
        QMessageBox::warning(this, QStringLiteral("Could not analyze file"),
                             QStringLiteral("%1\n\n%2").arg(m_job.fileName, r.error));
        return;
    }

    m_data = std::move(r.dataset);
    m_result = std::move(r.result);
    m_hasData = true;
    const bool fresh = m_job.kind != JobKind::Reanalyze;

    switch (m_job.kind) {
    case JobKind::NewFile:
        m_title = m_job.title;
        m_fileName = m_job.fileName;
        m_csv = m_job.csv;
        m_sessionId = m_dbOk ? m_db.createSession(m_title, m_fileName, m_csv, m_job.settings.toJson(),
                                                  buildSummary(), m_data.rowCount, int(m_data.columns.size()),
                                                  int(m_result.anomalies.size()))
                             : -1;
        showToast(m_sessionId > 0 ? QStringLiteral("Analysis saved to history")
                                  : QStringLiteral("Analysis ready (not saved)"));
        break;
    case JobKind::OpenSession:
        m_sessionId = m_job.sessionId;
        m_title = m_job.title;
        m_fileName = m_job.fileName;
        m_csv = m_job.csv;
        break;
    case JobKind::Reanalyze:
        if (m_dbOk && m_sessionId > 0)
            m_db.updateAnalysis(m_sessionId, m_job.settings.toJson(), buildSummary(),
                                int(m_result.anomalies.size()));
        break;
    }

    applySettingsToControls(m_job.settings);
    refreshSessions();
    m_stack->setCurrentIndex(1);
    renderAll(fresh);

    if (m_rerunPending) {
        m_rerunPending = false;
        runReanalysis();
    }
}

void MainWindow::setBusy(bool busy)
{
    m_busy->setVisible(busy);
    if (busy)
        QApplication::setOverrideCursor(Qt::BusyCursor);
    else
        QApplication::restoreOverrideCursor();
}

void MainWindow::showToast(const QString &text)
{
    m_toast->setText(text);
    m_toast->adjustSize();
    QWidget *c = centralWidget();
    m_toast->move(c->width() - m_toast->width() - 28, c->height() - m_toast->height() - 24);
    m_toast->raise();
    m_toast->show();
    m_toastTimer->start(2600);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (!DropZone::csvPathFromMime(e->mimeData()).isEmpty())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    const QString path = DropZone::csvPathFromMime(e->mimeData());
    if (!path.isEmpty()) {
        e->acceptProposedAction();
        loadFile(path);
    }
}

// ============================================================================
// History sidebar
// ============================================================================
void MainWindow::refreshSessions()
{
    m_sessionList->clear();
    if (!m_dbOk) {
        m_emptyHistory->setText(QStringLiteral("History is unavailable."));
        m_emptyHistory->show();
        return;
    }
    const QVector<SessionInfo> all = m_db.listSessions();
    const QString filter = m_search->text().trimmed();
    const QDate today = QDate::currentDate();

    auto groupFor = [&](const SessionInfo &s) {
        if (s.pinned)
            return QStringLiteral("Pinned");
        const qint64 days = s.updated.date().daysTo(today);
        if (days <= 0)  return QStringLiteral("Today");
        if (days == 1)  return QStringLiteral("Yesterday");
        if (days < 7)   return QStringLiteral("Previous 7 days");
        if (days < 30)  return QStringLiteral("Previous 30 days");
        return s.updated.date().toString(QStringLiteral("MMMM yyyy"));
    };

    QString lastGroup;
    int shown = 0;
    for (const SessionInfo &s : all) {
        if (!filter.isEmpty() && !s.title.contains(filter, Qt::CaseInsensitive)
            && !s.fileName.contains(filter, Qt::CaseInsensitive))
            continue;
        const QString g = groupFor(s);
        if (g != lastGroup) {
            auto *h = new QListWidgetItem(g);
            h->setData(SessionRoles::Header, true);
            h->setFlags(Qt::NoItemFlags);
            m_sessionList->addItem(h);
            lastGroup = g;
        }
        auto *it = new QListWidgetItem(s.title);
        it->setData(SessionRoles::Id, s.id);
        it->setData(SessionRoles::Pinned, s.pinned);
        it->setData(SessionRoles::Subtitle,
                    QStringLiteral("%1 rows · %2 params · %3 anomalies")
                        .arg(fmtInt(s.rows)).arg(s.params).arg(s.anomalies));
        it->setToolTip(QStringLiteral("%1\nCreated %2\nUpdated %3")
                           .arg(s.fileName, s.created.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                                s.updated.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        m_sessionList->addItem(it);
        if (s.id == m_sessionId)
            it->setSelected(true);
        ++shown;
    }
    m_emptyHistory->setText(filter.isEmpty() ? QStringLiteral("No analyses yet.\nUpload a CSV to get started.")
                                             : QStringLiteral("No matches."));
    m_emptyHistory->setVisible(shown == 0);
}

void MainWindow::onSessionClicked(QListWidgetItem *item)
{
    if (!item || item->data(SessionRoles::Header).toBool())
        return;
    const qint64 id = item->data(SessionRoles::Id).toLongLong();
    if (id == m_sessionId && m_hasData) {
        m_stack->setCurrentIndex(1);
        return;
    }
    SessionRecord rec;
    if (!m_db.loadSession(id, rec)) {
        QMessageBox::warning(this, QStringLiteral("History"), QStringLiteral("This analysis could not be loaded."));
        return;
    }
    Job job;
    job.kind = JobKind::OpenSession;
    job.sessionId = id;
    job.title = rec.info.title;
    job.fileName = rec.info.fileName;
    job.csv = rec.csv;
    job.settings = AnalysisSettings::fromJson(rec.settings);
    startJob(job);
}

void MainWindow::onSessionContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_sessionList->itemAt(pos);
    if (!item || item->data(SessionRoles::Header).toBool())
        return;
    const qint64 id = item->data(SessionRoles::Id).toLongLong();
    const bool pinned = item->data(SessionRoles::Pinned).toBool();

    QMenu menu(this);
    QAction *rename = menu.addAction(QStringLiteral("Rename…"));
    QAction *pin = menu.addAction(pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin to top"));
    menu.addSeparator();
    QAction *del = menu.addAction(QStringLiteral("Delete"));
    QAction *chosen = menu.exec(m_sessionList->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == rename) {
        bool ok = false;
        const QString t = QInputDialog::getText(this, QStringLiteral("Rename analysis"), QStringLiteral("Title"),
                                                QLineEdit::Normal, item->text(), &ok).trimmed();
        if (ok && !t.isEmpty() && m_db.rename(id, t)) {
            if (id == m_sessionId) {
                m_title = t;
                renderHeader();
            }
            refreshSessions();
        }
    } else if (chosen == pin) {
        m_db.setPinned(id, !pinned);
        refreshSessions();
    } else if (chosen == del) {
        if (QMessageBox::question(this, QStringLiteral("Delete analysis"),
                                  QStringLiteral("Delete “%1” from history? This cannot be undone.").arg(item->text()))
            != QMessageBox::Yes)
            return;
        m_db.remove(id);
        if (id == m_sessionId)
            newAnalysis();
        refreshSessions();
        showToast(QStringLiteral("Analysis deleted"));
    }
}

// ============================================================================
// Settings
// ============================================================================
void MainWindow::applySettingsToControls(const AnalysisSettings &s)
{
    const QSignalBlocker b1(m_method), b2(m_threshold), b3(m_window);
    m_method->setCurrentIndex(m_method->findData(int(s.method)));
    m_threshold->setValue(s.threshold);
    m_window->setValue(s.window);
    m_window->setEnabled(true);
}

AnalysisSettings MainWindow::settingsFromControls() const
{
    AnalysisSettings s;
    s.method = AnomalyMethod(m_method->currentData().toInt());
    s.threshold = m_threshold->value();
    s.window = m_window->value();
    return s;
}

void MainWindow::onMethodChanged(int)
{
    const QSignalBlocker b(m_threshold);
    m_threshold->setValue(AnalysisSettings::defaultThreshold(AnomalyMethod(m_method->currentData().toInt())));
    onSettingsEdited();
}

void MainWindow::onSettingsEdited()
{
    if (m_hasData)
        m_debounce->start();
}

void MainWindow::runReanalysis()
{
    if (!m_hasData)
        return;
    Job job;
    job.kind = JobKind::Reanalyze;
    job.sessionId = m_sessionId;
    job.fileName = m_fileName;
    job.settings = settingsFromControls();
    startJob(job);
}

QJsonObject MainWindow::buildSummary() const
{
    QJsonObject o;
    o[QStringLiteral("rows")] = m_data.rowCount;
    o[QStringLiteral("parameters")] = QJsonArray::fromStringList(m_data.columns);
    o[QStringLiteral("anomalies")] = int(m_result.anomalies.size());
    o[QStringLiteral("missing")] = m_result.totalMissing;
    if (m_data.hasTime()) {
        o[QStringLiteral("start")] = m_data.time.first().toString(Qt::ISODate);
        o[QStringLiteral("end")] = m_data.time.last().toString(Qt::ISODate);
    }
    return o;
}

// ============================================================================
// Rendering
// ============================================================================
double MainWindow::xFor(int row) const
{
    return m_data.hasTime() ? double(m_data.time[row].toMSecsSinceEpoch()) : double(row + 1);
}

void MainWindow::renderAll(bool fresh)
{
    renderHeader();
    renderKpis();

    {
        const QSignalBlocker b(m_param);
        const int prev = fresh ? 0 : m_param->currentIndex();
        m_param->clear();
        m_param->addItems(m_data.columns);
        m_param->setCurrentIndex(std::clamp(prev, 0, int(m_data.columns.size()) - 1));
    }
    {
        const QSignalBlocker b(m_anomFilter);
        const int prev = fresh ? 0 : m_anomFilter->currentIndex();
        m_anomFilter->clear();
        m_anomFilter->addItem(QStringLiteral("All parameters"));
        for (int c = 0; c < m_data.columns.size(); ++c)
            m_anomFilter->addItem(QStringLiteral("%1  (%2)").arg(m_data.columns[c]).arg(m_result.stats[c].anomalies));
        m_anomFilter->setCurrentIndex(std::clamp(prev, 0, m_anomFilter->count() - 1));
    }

    renderOverview();
    renderTimeSeries();
    renderDistribution();
    renderStatsTable();
    renderAnomalyTable();
    renderWqi();
    if (fresh)
        m_tabBar->setCurrentIndex(0);
}

void MainWindow::renderHeader()
{
    m_titleLbl->setText(m_title);
    QString sub = QStringLiteral("%1  ·  %2 rows").arg(m_fileName, fmtInt(m_data.rowCount));
    if (m_data.hasTime())
        sub += QStringLiteral("  ·  %1 → %2")
                   .arg(m_data.time.first().toString(QStringLiteral("yyyy-MM-dd")),
                        m_data.time.last().toString(QStringLiteral("yyyy-MM-dd")));
    m_subtitleLbl->setText(sub);
    m_warnLbl->setVisible(!m_data.warnings.isEmpty());
    m_warnLbl->setText(QStringLiteral("⚠ %1 note%2").arg(m_data.warnings.size()).arg(m_data.warnings.size() == 1 ? "" : "s"));
    m_warnLbl->setToolTip(QStringLiteral("• ") + m_data.warnings.join(QStringLiteral("\n• ")));
}

void MainWindow::renderKpis()
{
    m_kpiValue[0]->setText(fmtInt(m_data.rowCount));
    m_kpiHint[0]->setText(m_data.hasTime() ? QStringLiteral("time column: %1").arg(m_data.timeColumn)
                                           : QStringLiteral("no time column"));

    m_kpiValue[1]->setText(QString::number(m_data.columns.size()));
    m_kpiHint[1]->setText(m_data.skippedColumns.isEmpty()
                              ? QStringLiteral("numeric columns")
                              : QStringLiteral("%1 text column(s) skipped").arg(m_data.skippedColumns.size()));

    if (m_data.hasTime() && m_data.rowCount > 1) {
        m_kpiValue[2]->setText(fmtDuration(m_data.time.first().msecsTo(m_data.time.last())));
        QVector<qint64> d;
        d.reserve(m_data.rowCount - 1);
        for (int i = 1; i < m_data.rowCount; ++i)
            d << m_data.time[i - 1].msecsTo(m_data.time[i]);
        std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
        m_kpiHint[2]->setText(QStringLiteral("sampled every %1").arg(fmtInterval(d[d.size() / 2])));
    } else {
        m_kpiValue[2]->setText(QStringLiteral("—"));
        m_kpiHint[2]->setText(QStringLiteral("rows plotted by index"));
    }

    const double pct = m_result.totalCells > 0 ? 100.0 * m_result.totalMissing / m_result.totalCells : 0.0;
    m_kpiValue[3]->setText(QStringLiteral("%1%").arg(QString::number(pct, 'f', pct < 10 ? 1 : 0)));
    m_kpiHint[3]->setText(QStringLiteral("%1 of %2 values").arg(fmtInt(m_result.totalMissing), fmtInt(m_result.totalCells)));

    const int n = int(m_result.anomalies.size());
    m_kpiValue[4]->setText(fmtInt(n));
    m_kpiValue[4]->setProperty("danger", n > 0);
    m_kpiValue[4]->style()->unpolish(m_kpiValue[4]);
    m_kpiValue[4]->style()->polish(m_kpiValue[4]);
    m_kpiHint[4]->setText(QStringLiteral("%1, t = %2").arg(AnalysisSettings::methodName(m_result.settings.method))
                              .arg(m_result.settings.threshold, 0, 'f', 1));
}

void MainWindow::renderOverview()
{
    QList<QWidget *> cards;
    for (int c = 0; c < m_data.columns.size(); ++c) {
        const ColumnStats &s = m_result.stats[c];
        const QVector<double> &vals = m_data.values[c];
        QVector<QPointF> line, marks;
        for (int i : decimate(vals, 300))
            line << QPointF(xFor(i), vals[i]);
        for (int i : m_result.anomalyRows[c])
            marks << QPointF(xFor(i), vals[i]);
        const QString range = s.count > 0 ? QStringLiteral("mean  ·  range %1 – %2").arg(fmtNum(s.min), fmtNum(s.max))
                                          : QStringLiteral("no data");
        auto *card = new ParamCard(c, s.name, fmtNum(s.mean), range, s.anomalies, line, marks, m_data.hasTime());
        connect(card, &ParamCard::clicked, this, [this](int index) {
            m_param->setCurrentIndex(index);
            m_tabBar->setCurrentIndex(1);
        });
        cards << card;
    }
    m_cards->setCards(cards);
}

void MainWindow::renderTimeSeries()
{
    const int c = m_param->currentIndex();
    if (!m_hasData || c < 0 || c >= m_data.columns.size()) {
        m_tsView->replaceChart(makeChart());
        return;
    }
    const ThemePalette &p = Theme::palette();
    const QVector<double> &vals = m_data.values[c];
    const ColumnStats &st = m_result.stats[c];
    const bool hasTime = m_data.hasTime();
    const QString name = m_data.columns[c];

    QChart *ch = makeChart();

    auto *raw = new QLineSeries;
    raw->setName(name);
    QPen rp(p.accent);
    rp.setWidthF(1.4);
    raw->setPen(rp);
    QList<QPointF> pts;
    for (int i : decimate(vals, kMaxPlotPoints))
        pts << QPointF(xFor(i), vals[i]);
    raw->replace(pts);
    ch->addSeries(raw);

    const double x0 = xFor(0), x1 = xFor(m_data.rowCount - 1);

    if (m_showSmooth->isChecked() && c < m_result.smooth.size()) {
        auto *sm = new QLineSeries;
        sm->setName(QStringLiteral("Moving average (%1 pts)").arg(std::max(3, m_result.settings.window | 1)));
        QColor sc = p.text;
        sc.setAlpha(170);
        QPen sp(sc);
        sp.setWidthF(2.0);
        sm->setPen(sp);
        const QVector<double> &sv = m_result.smooth[c];
        const int step = std::max(1, int(sv.size()) / kMaxPlotPoints);
        QList<QPointF> spts;
        for (int i = 0; i < sv.size(); i += step)
            if (std::isfinite(sv[i]))
                spts << QPointF(xFor(i), sv[i]);
        sm->replace(spts);
        ch->addSeries(sm);
    }

    if (m_showBand->isChecked() && st.hasBounds) {
        QColor bc = p.danger;
        bc.setAlpha(190);
        QPen bp(bc, 1.2, Qt::DashLine);
        for (double y : {st.upper, st.lower}) {
            auto *b = new QLineSeries;
            b->setName(QStringLiteral("Threshold"));
            b->setPen(bp);
            b->append(x0, y);
            b->append(x1, y);
            ch->addSeries(b);
            if (y == st.lower)
                for (QLegendMarker *m : ch->legend()->markers(b))
                    m->setVisible(false);
        }
    }

    QHash<int, double> scoreAt;
    for (const Anomaly &a : m_result.anomalies)
        if (a.column == c)
            scoreAt.insert(a.row, a.score);

    if (m_showAnoms->isChecked() && !m_result.anomalyRows[c].isEmpty()) {
        auto *sc = new QScatterSeries;
        sc->setName(QStringLiteral("Anomalies (%1)").arg(m_result.anomalyRows[c].size()));
        sc->setMarkerShape(QScatterSeries::MarkerShapeCircle);
        sc->setMarkerSize(9);
        sc->setColor(p.danger);
        sc->setBorderColor(p.surface);
        QList<QPointF> apts;
        for (int i : m_result.anomalyRows[c]) {
            apts << QPointF(xFor(i), vals[i]);
            if (apts.size() >= kMaxTableRows)
                break;
        }
        sc->replace(apts);
        ch->addSeries(sc);
    }

    // axes
    QAbstractAxis *ax = nullptr;
    if (hasTime) {
        auto *dt = new QDateTimeAxis;
        dt->setTickCount(8);
        dt->setFormat(dtAxisFormat(qint64(x1 - x0)));
        dt->setRange(QDateTime::fromMSecsSinceEpoch(qint64(x0)), QDateTime::fromMSecsSinceEpoch(qint64(x1)));
        connect(dt, &QDateTimeAxis::rangeChanged, dt, [dt](const QDateTime &a, const QDateTime &b) {
            dt->setFormat(dtAxisFormat(a.msecsTo(b)));
        });
        ax = dt;
    } else {
        auto *va = new QValueAxis;
        va->setLabelFormat(QStringLiteral("%.0f"));
        va->setTitleText(QStringLiteral("Row"));
        va->setRange(x0, std::max(x1, x0 + 1));
        ax = va;
    }
    auto *ay = new QValueAxis;
    double y0 = st.min, y1 = st.max;
    if (!(y1 > y0)) {
        y0 -= 1;
        y1 += 1;
    }
    const double pad = (y1 - y0) * 0.08;
    ay->setRange(y0 - pad, y1 + pad);
    ay->setTickCount(7);
    ay->applyNiceNumbers();
    ay->setLabelFormat(QStringLiteral("%.4g"));
    ay->setTitleText(name);
    styleAxis(ax);
    styleAxis(ay);
    ch->addAxis(ax, Qt::AlignBottom);
    ch->addAxis(ay, Qt::AlignLeft);
    for (QAbstractSeries *s : ch->series()) {
        s->attachAxis(ax);
        s->attachAxis(ay);
    }

    m_tsView->replaceChart(ch);

    // hover: nearest finite sample by x (full resolution, not the decimated plot)
    QVector<double> xs(m_data.rowCount);
    for (int i = 0; i < m_data.rowCount; ++i)
        xs[i] = xFor(i);
    const QVector<double> smooth = c < m_result.smooth.size() ? m_result.smooth[c] : QVector<double>();
    m_tsView->setHover(raw, [xs, vals, smooth, scoreAt, name, hasTime](const QPointF &v, QPointF &anchor, QString &text) {
        if (xs.isEmpty())
            return false;
        int i = int(std::lower_bound(xs.begin(), xs.end(), v.x()) - xs.begin());
        i = std::clamp(i, 0, int(xs.size()) - 1);
        if (i > 0 && std::fabs(xs[i - 1] - v.x()) < std::fabs(xs[i] - v.x()))
            --i;
        int best = -1;
        for (int d = 0; d < 50 && best < 0; ++d) {
            if (i - d >= 0 && std::isfinite(vals[i - d])) best = i - d;
            else if (i + d < vals.size() && std::isfinite(vals[i + d])) best = i + d;
        }
        if (best < 0)
            return false;
        anchor = QPointF(xs[best], vals[best]);
        text = hasTime ? QDateTime::fromMSecsSinceEpoch(qint64(xs[best])).toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                       : QStringLiteral("Row %1").arg(best + 1);
        text += QStringLiteral("\n%1:  %2").arg(name, fmtNum(vals[best]));
        if (best < smooth.size() && std::isfinite(smooth[best]))
            text += QStringLiteral("\nMoving avg:  %1").arg(fmtNum(smooth[best]));
        if (scoreAt.contains(best))
            text += QStringLiteral("\n⚠ Anomaly  ·  score %1").arg(scoreAt.value(best), 0, 'f', 1);
        return true;
    });
    m_tsView->rememberView();
}

void MainWindow::renderDistribution()
{
    const int c = m_param->currentIndex();
    const ThemePalette &p = Theme::palette();
    if (!m_hasData || c < 0 || c >= m_data.columns.size()) {
        m_distView->replaceChart(makeChart());
        m_distStats->clear();
        return;
    }
    const ColumnStats &st = m_result.stats[c];
    QVector<double> clean;
    for (double x : m_data.values[c])
        if (std::isfinite(x))
            clean << x;

    QChart *ch = makeChart();
    if (clean.isEmpty()) {
        m_distView->replaceChart(ch);
        m_distStats->setText(QStringLiteral("No data"));
        return;
    }

    // Freedman–Diaconis bin width, clamped to 8..48 bins
    const int n = int(clean.size());
    const double span = st.max - st.min;
    int bins = 1;
    if (span > 0) {
        const double iqr = st.q3 - st.q1;
        const double h = iqr > 0 ? 2.0 * iqr / std::cbrt(double(n)) : span / std::ceil(std::log2(n) + 1);
        bins = std::clamp(int(std::ceil(span / h)), 8, 48);
    }
    const double lo = span > 0 ? st.min : st.min - 0.5;
    const double bw = span > 0 ? span / bins : 1.0;
    QVector<int> counts(bins, 0);
    for (double x : clean)
        counts[std::clamp(int((x - lo) / bw), 0, bins - 1)]++;
    const int maxCount = *std::max_element(counts.begin(), counts.end());

    auto *upper = new QLineSeries;
    upper->append(lo, 0);
    for (int b = 0; b < bins; ++b) {
        upper->append(lo + b * bw, counts[b]);
        upper->append(lo + (b + 1) * bw, counts[b]);
    }
    upper->append(lo + bins * bw, 0);
    auto *area = new QAreaSeries(upper);
    area->setName(QStringLiteral("Frequency"));
    QColor fill = p.accent;
    fill.setAlpha(p.dark ? 90 : 70);
    area->setColor(fill);
    area->setBorderColor(p.accent);
    ch->addSeries(area);

    auto vline = [&](double x, const QString &label, QPen pen) {
        auto *s = new QLineSeries;
        s->setName(label);
        s->setPen(pen);
        s->append(x, 0);
        s->append(x, maxCount * 1.08);
        ch->addSeries(s);
        return s;
    };
    QColor tc = p.text;
    tc.setAlpha(200);
    vline(st.mean, QStringLiteral("Mean"), QPen(tc, 1.6, Qt::DashLine));
    vline(st.median, QStringLiteral("Median"), QPen(p.warn, 1.6, Qt::DashLine));
    double xmin = lo, xmax = lo + bins * bw;
    if (st.hasBounds) {
        QColor bc = p.danger;
        bc.setAlpha(190);
        bool first = true;
        for (double b : {st.lower, st.upper}) {
            if (b < xmin - span * 0.5 || b > xmax + span * 0.5)
                continue;
            auto *s = vline(b, QStringLiteral("Threshold"), QPen(bc, 1.2, Qt::DotLine));
            if (!first)
                for (QLegendMarker *m : ch->legend()->markers(s))
                    m->setVisible(false);
            first = false;
            xmin = std::min(xmin, b);
            xmax = std::max(xmax, b);
        }
    }

    auto *ax = new QValueAxis;
    const double xpad = (xmax - xmin) * 0.03;
    ax->setRange(xmin - xpad, xmax + xpad);
    ax->setTickCount(9);
    ax->applyNiceNumbers();
    ax->setLabelFormat(QStringLiteral("%.4g"));
    ax->setTitleText(m_data.columns[c]);
    auto *ay = new QValueAxis;
    ay->setRange(0, maxCount * 1.12);
    ay->setTickCount(6);
    ay->applyNiceNumbers();
    ay->setLabelFormat(QStringLiteral("%.0f"));
    ay->setTitleText(QStringLiteral("Count"));
    styleAxis(ax);
    styleAxis(ay);
    ch->addAxis(ax, Qt::AlignBottom);
    ch->addAxis(ay, Qt::AlignLeft);
    for (QAbstractSeries *s : ch->series()) {
        s->attachAxis(ax);
        s->attachAxis(ay);
    }
    m_distView->replaceChart(ch);
    m_distView->setHover(area, [counts, lo, bw, n](const QPointF &v, QPointF &anchor, QString &text) {
        const int b = int(std::floor((v.x() - lo) / bw));
        if (b < 0 || b >= counts.size())
            return false;
        anchor = QPointF(lo + (b + 0.5) * bw, counts[b]);
        text = QStringLiteral("%1 – %2\nCount:  %3  (%4%)")
                   .arg(fmtNum(lo + b * bw), fmtNum(lo + (b + 1) * bw))
                   .arg(counts[b])
                   .arg(100.0 * counts[b] / n, 0, 'f', 1);
        return true;
    }, false);
    m_distView->rememberView();

    // side panel
    const QString muted = p.textMuted.name();
    auto row = [&](const QString &k, const QString &val) {
        return QStringLiteral("<tr><td style='color:%3; padding:5px 18px 5px 0'>%1</td>"
                              "<td align='right' style='padding:5px 0'><b>%2</b></td></tr>").arg(k, val, muted);
    };
    QString html = QStringLiteral("<div style='font-size:15px; font-weight:600; margin-bottom:8px'>%1</div><table>")
                       .arg(m_data.columns[c].toHtmlEscaped());
    html += row(QStringLiteral("Count"), fmtInt(st.count));
    html += row(QStringLiteral("Missing"), fmtInt(st.missing));
    html += row(QStringLiteral("Mean"), fmtNum(st.mean));
    html += row(QStringLiteral("Median"), fmtNum(st.median));
    html += row(QStringLiteral("Std dev"), fmtNum(st.stddev));
    html += row(QStringLiteral("Min"), fmtNum(st.min));
    html += row(QStringLiteral("Q1"), fmtNum(st.q1));
    html += row(QStringLiteral("Q3"), fmtNum(st.q3));
    html += row(QStringLiteral("Max"), fmtNum(st.max));
    html += row(QStringLiteral("IQR"), fmtNum(st.q3 - st.q1));
    html += row(QStringLiteral("Skewness"), QString::number(st.skewness, 'f', 2));
    html += row(QStringLiteral("Trend"), fmtNum(st.slope) + m_result.slopeUnit);
    html += row(QStringLiteral("Anomalies"), fmtInt(st.anomalies));
    html += QStringLiteral("</table>");
    if (st.hasBounds)
        html += QStringLiteral("<p style='color:%1; margin-top:10px'>Threshold band: %2 – %3</p>")
                    .arg(muted, fmtNum(st.lower), fmtNum(st.upper));
    m_distStats->setText(html);
}

void MainWindow::renderStatsTable()
{
    QTableWidget *t = m_statsTable;
    t->setSortingEnabled(false);
    t->setRowCount(int(m_result.stats.size()));
    for (int r = 0; r < m_result.stats.size(); ++r) {
        const ColumnStats &s = m_result.stats[r];
        t->setItem(r, 0, new QTableWidgetItem(s.name));
        t->setItem(r, 1, new NumItem(s.count, fmtInt(s.count)));
        t->setItem(r, 2, new NumItem(s.missing, fmtInt(s.missing)));
        const double vals[] = {s.mean, s.median, s.stddev, s.min, s.q1, s.q3, s.max};
        for (int k = 0; k < 7; ++k)
            t->setItem(r, 3 + k, new NumItem(vals[k], fmtNum(vals[k])));
        t->setItem(r, 10, new NumItem(s.slope, fmtNum(s.slope) + m_result.slopeUnit));
        auto *an = new NumItem(s.anomalies, fmtInt(s.anomalies));
        if (s.anomalies > 0)
            an->setForeground(Theme::palette().danger);
        t->setItem(r, 11, an);
    }
    t->setSortingEnabled(true);
}

void MainWindow::renderAnomalyTable()
{
    QTableWidget *t = m_anomTable;
    const int filter = m_anomFilter->currentIndex() - 1; // -1 = all
    QVector<const Anomaly *> rows;
    for (const Anomaly &a : m_result.anomalies)
        if (filter < 0 || a.column == filter)
            rows << &a;

    const int shown = std::min(int(rows.size()), kMaxTableRows);
    m_anomCount->setText(rows.size() > kMaxTableRows
                             ? QStringLiteral("%1 anomalies (showing first %2)").arg(fmtInt(rows.size()), fmtInt(shown))
                             : QStringLiteral("%1 anomal%2").arg(fmtInt(rows.size())).arg(rows.size() == 1 ? "y" : "ies"));
    t->horizontalHeaderItem(0)->setText(m_data.hasTime() ? QStringLiteral("Time") : QStringLiteral("Row"));

    t->setSortingEnabled(false);
    t->setRowCount(shown);
    const QColor danger = Theme::palette().danger;
    for (int r = 0; r < shown; ++r) {
        const Anomaly &a = *rows[r];
        QTableWidgetItem *first = m_data.hasTime()
            ? new QTableWidgetItem(m_data.time[a.row].toString(QStringLiteral("yyyy-MM-dd HH:mm")))
            : new NumItem(a.row + 1, QString::number(a.row + 1));
        first->setData(Qt::UserRole, a.row);
        first->setData(Qt::UserRole + 1, a.column);
        if (!m_data.hasTime())
            first->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        t->setItem(r, 0, first);
        t->setItem(r, 1, new QTableWidgetItem(m_data.columns[a.column]));
        auto *val = new NumItem(a.value, fmtNum(a.value));
        val->setForeground(danger);
        t->setItem(r, 2, val);
        t->setItem(r, 3, new NumItem(a.expected, fmtNum(a.expected)));
        const double dev = a.value - a.expected;
        t->setItem(r, 4, new NumItem(dev, (dev > 0 ? QStringLiteral("+") : QString()) + fmtNum(dev)));
        t->setItem(r, 5, new NumItem(a.score, QString::number(a.score, 'f', 1)));
    }
    t->setSortingEnabled(true);
    t->setVisible(shown > 0);
    m_anomEmpty->setVisible(shown == 0);
}

// ============================================================================
// Interaction
// ============================================================================
void MainWindow::onParameterChanged(int)
{
    renderTimeSeries();
    renderDistribution();
}

void MainWindow::onTabChanged(int index)
{
    m_pages->setCurrentIndex(index);
    m_paramBox->setVisible(index == 1 || index == 2);
}

void MainWindow::onAnomalyActivated(int row, int)
{
    QTableWidgetItem *it = m_anomTable->item(row, 0);
    if (!it)
        return;
    const int r = it->data(Qt::UserRole).toInt();
    const int c = it->data(Qt::UserRole + 1).toInt();
    m_param->setCurrentIndex(c);
    m_tabBar->setCurrentIndex(1);
    const double span = xFor(m_data.rowCount - 1) - xFor(0);
    const double half = m_data.hasTime() ? std::max(span * 0.02, 6.0 * 3600000.0) : std::max(span * 0.02, 20.0);
    m_tsView->focusX(xFor(r), half);
}

void MainWindow::updateThemeButton()
{
    m_themeBtn->setText(Theme::mode() == Theme::Mode::Dark ? QStringLiteral("☀  Light mode")
                                                           : QStringLiteral("☾  Dark mode"));
}

void MainWindow::toggleTheme()
{
    Theme::setMode(Theme::mode() == Theme::Mode::Dark ? Theme::Mode::Light : Theme::Mode::Dark);
    QSettings().setValue(QStringLiteral("theme"), Theme::mode() == Theme::Mode::Light ? "light" : "dark");
    qApp->setStyleSheet(Theme::styleSheet());
    updateThemeButton();
    m_sessionList->viewport()->update();
    if (m_hasData) {
        const int tab = m_tabBar->currentIndex();
        renderAll(false);
        m_tabBar->setCurrentIndex(tab);
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("About AquaLens"),
                       QStringLiteral("<h3>AquaLens 1.0</h3>"
                                      "<p>Water quality data analyzer: statistics, time-series charts "
                                      "and anomaly detection for CSV datasets.</p>"
                                      "<p>C++17 · Qt %1 (Widgets, Charts, SQL, Concurrent) · SQLite</p>"
                                      "<p style='color:gray'>History database:<br>%2</p>")
                           .arg(QString::fromLatin1(qVersion()), QDir::toNativeSeparators(m_db.path())));
}

// ============================================================================
// Export
// ============================================================================
void MainWindow::exportChart()
{
    if (!m_hasData)
        return;
    const bool wqi = m_tabBar->currentIndex() == 5;
    QWidget *view = wqi ? m_wqiPage : (m_tabBar->currentIndex() == 2 ? static_cast<QWidget *>(m_distView) : m_tsView);
    if (!wqi && m_tabBar->currentIndex() != 1 && m_tabBar->currentIndex() != 2)
        m_tabBar->setCurrentIndex(1);
    const QString base = QStringLiteral("%1_%2.png").arg(QFileInfo(m_fileName).completeBaseName(),
                                                         wqi ? QStringLiteral("korean_wqi") : m_param->currentText());
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export chart"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(base),
        QStringLiteral("PNG image (*.png)"));
    if (path.isEmpty())
        return;
    const qreal dpr = 2.0;
    QPixmap pm(view->size() * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(wqi ? Theme::palette().bg : Theme::palette().surface);
    QPainter painter(&pm);
    view->render(&painter);
    painter.end();
    if (pm.save(path))
        showToast(QStringLiteral("Chart exported"));
}

void MainWindow::exportStats()
{
    if (!m_hasData)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export statistics"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QFileInfo(m_fileName).completeBaseName() + QStringLiteral("_statistics.csv")),
        QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&f);
    out << "parameter,count,missing,mean,median,std,min,q1,q3,max,skewness,trend" << m_result.slopeUnit.mid(1)
        << ",anomalies\n";
    for (const ColumnStats &s : std::as_const(m_result.stats))
        out << csvField(s.name) << ',' << s.count << ',' << s.missing << ',' << s.mean << ',' << s.median << ','
            << s.stddev << ',' << s.min << ',' << s.q1 << ',' << s.q3 << ',' << s.max << ',' << s.skewness << ','
            << s.slope << ',' << s.anomalies << '\n';
    showToast(QStringLiteral("Statistics exported"));
}

void MainWindow::exportAnomalies()
{
    if (!m_hasData)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export anomalies"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QFileInfo(m_fileName).completeBaseName() + QStringLiteral("_anomalies.csv")),
        QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&f);
    out << (m_data.hasTime() ? "time" : "row") << ",parameter,value,expected,score,method,threshold\n";
    const QString method = AnalysisSettings::methodName(m_result.settings.method);
    for (const Anomaly &a : std::as_const(m_result.anomalies)) {
        out << (m_data.hasTime() ? m_data.time[a.row].toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                 : QString::number(a.row + 1))
            << ',' << csvField(m_data.columns[a.column]) << ',' << a.value << ',' << a.expected << ','
            << a.score << ',' << method << ',' << m_result.settings.threshold << '\n';
    }
    showToast(QStringLiteral("%1 anomalies exported").arg(m_result.anomalies.size()));
}

// ============================================================================
// Korean Water Quality Index (생활환경기준 grades, NIER)
// ============================================================================
QWidget *MainWindow::buildWqiPage()
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_wqiPage = new QWidget;
    m_wqiPage->setObjectName(QStringLiteral("ScrollBody"));
    auto *v = new QVBoxLayout(m_wqiPage);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(14);

    // ---- summary card -------------------------------------------------------
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("Card"));
    auto *cv = new QVBoxLayout(card);
    cv->setContentsMargins(18, 16, 18, 14);
    cv->setSpacing(12);

    auto *row = new QHBoxLayout;
    row->setSpacing(18);
    m_wqiGrade = new QLabel(QStringLiteral("—"));
    m_wqiGrade->setAlignment(Qt::AlignCenter);
    m_wqiGrade->setMinimumWidth(92);
    row->addWidget(m_wqiGrade, 0, Qt::AlignVCenter);

    auto *mid = new QVBoxLayout;
    mid->setSpacing(3);
    auto *cap = new QLabel(QStringLiteral("KOREAN WATER QUALITY INDEX  ·  OVERALL GRADE"));
    cap->setObjectName(QStringLiteral("KpiCaption"));
    m_wqiGradeName = new QLabel;
    m_wqiGradeName->setObjectName(QStringLiteral("Title"));
    m_wqiUse = mutedLabel(QString());
    m_wqiUse->setWordWrap(true);
    m_wqiDetail = new QLabel;
    m_wqiDetail->setTextFormat(Qt::RichText);
    m_wqiDetail->setWordWrap(true);
    mid->addWidget(cap);
    mid->addWidget(m_wqiGradeName);
    mid->addWidget(m_wqiUse);
    mid->addSpacing(4);
    mid->addWidget(m_wqiDetail);
    row->addLayout(mid, 1);

    auto *ctl = new QGridLayout;
    ctl->setHorizontalSpacing(8);
    ctl->setVerticalSpacing(8);
    m_wqiBody = new QComboBox;
    m_wqiBody->addItem(KoreanWqi::bodyName(KoreanWqi::WaterBody::River), int(KoreanWqi::WaterBody::River));
    m_wqiBody->addItem(KoreanWqi::bodyName(KoreanWqi::WaterBody::Lake), int(KoreanWqi::WaterBody::Lake));
    m_wqiBody->setToolTip(QStringLiteral("Which set of living-environment standards to apply"));
    m_wqiStat = new QComboBox;
    m_wqiStat->addItem(QStringLiteral("Mean (평균)"), int(KoreanWqi::Statistic::Mean));
    m_wqiStat->addItem(QStringLiteral("Median (중앙값)"), int(KoreanWqi::Statistic::Median));
    m_wqiStat->setToolTip(QStringLiteral("Representative value used to grade each parameter"));
    ctl->addWidget(mutedLabel(QStringLiteral("Water body")), 0, 0);
    ctl->addWidget(m_wqiBody, 0, 1);
    ctl->addWidget(mutedLabel(QStringLiteral("Statistic")), 1, 0);
    ctl->addWidget(m_wqiStat, 1, 1);
    auto *src = new QLabel(QStringLiteral("<a href='%1' style='color:%2; text-decoration:none'>Source: NIER 생활환경기준 ↗</a>")
                               .arg(KoreanWqi::sourceUrl(), Theme::palette().accent.name()));
    src->setTextFormat(Qt::RichText);
    src->setOpenExternalLinks(true);
    src->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    src->setToolTip(KoreanWqi::sourceUrl());
    ctl->addWidget(src, 2, 0, 1, 2, Qt::AlignRight);
    row->addLayout(ctl);
    cv->addLayout(row);

    cv->addWidget(new GradeLegend);
    m_wqiMethod = new QLabel;
    m_wqiMethod->setObjectName(QStringLiteral("Small"));
    m_wqiMethod->setWordWrap(true);
    cv->addWidget(m_wqiMethod);
    v->addWidget(card);

    m_wqiEmpty = mutedLabel(QString());
    m_wqiEmpty->setAlignment(Qt::AlignCenter);
    m_wqiEmpty->setWordWrap(true);
    m_wqiEmpty->setContentsMargins(0, 30, 0, 30);
    v->addWidget(m_wqiEmpty);

    m_wqiCards = new CardGrid;
    v->addWidget(m_wqiCards, 1);

    scroll->setWidget(m_wqiPage);
    scroll->viewport()->setAutoFillBackground(false);

    QSettings settings;
    m_wqiBody->setCurrentIndex(std::clamp(settings.value(QStringLiteral("wqi/body"), 0).toInt(), 0, 1));
    m_wqiStat->setCurrentIndex(std::clamp(settings.value(QStringLiteral("wqi/statistic"), 0).toInt(), 0, 1));
    connect(m_wqiBody, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        QSettings().setValue(QStringLiteral("wqi/body"), i);
        renderWqi();
    });
    connect(m_wqiStat, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        QSettings().setValue(QStringLiteral("wqi/statistic"), i);
        renderWqi();
    });
    return scroll;
}

void MainWindow::renderWqi()
{
    using namespace KoreanWqi;
    if (!m_wqiCards)
        return;
    const auto body = WaterBody(m_wqiBody->currentData().toInt());
    const auto stat = Statistic(m_wqiStat->currentData().toInt());
    m_wqi = m_hasData ? evaluate(m_data, body, stat) : Result{};
    const ThemePalette &p = Theme::palette();

    // ---- summary ------------------------------------------------------------
    const int g = m_wqi.overallGrade;
    m_wqiGrade->setText(gradeCode(g));
    m_wqiGrade->setStyleSheet(g >= 0 ? gradePillStyle(g, 30, 10, 20) + QStringLiteral("border-radius:14px;")
                                     : QStringLiteral("font-size:30px; font-weight:700; padding:10px 20px; "
                                                      "border-radius:14px; background:%1; color:%2;")
                                           .arg(p.surface2.name(), p.textMuted.name()));
    m_wqiGradeName->setText(g >= 0 ? QStringLiteral("%1  ·  %2").arg(gradeNameKo(g), gradeNameEn(g))
                                   : QStringLiteral("Not assessed"));
    m_wqiUse->setText(g >= 0 ? QStringLiteral("Grade %1 water (NIER): %2.").arg(gradeCode(g), gradeUse(g)) : QString());

    const QString muted = p.textMuted.name();
    QStringList assessed, excluded, otherBody;
    for (const ParamResult &pr : std::as_const(m_wqi.params)) {
        const QString name = m_data.columns[pr.column].toHtmlEscaped();
        if (pr.included && pr.grade >= 0)
            assessed << QStringLiteral("%1 <span style='color:%2'>%3</span>")
                            .arg(name, gradeColor(pr.grade).name(), gradeCode(pr.grade));
        else
            excluded << QStringLiteral("%1 (%2)").arg(name, pr.status.toHtmlEscaped());
    }
    QStringList uncovered;
    for (const QString &c : std::as_const(m_wqi.unmatched)) {
        if (detect(c) != Param::None)
            otherBody << c.toHtmlEscaped();
        else
            uncovered << c.toHtmlEscaped();
    }
    QStringList lines;
    if (!m_wqi.limiting.isEmpty())
        lines << QStringLiteral("<b>Limiting parameter:</b> %1").arg(m_wqi.limiting.toHtmlEscaped());
    if (!assessed.isEmpty())
        lines << QStringLiteral("<b>Graded:</b> %1").arg(assessed.join(QStringLiteral(",&nbsp; ")));
    if (!excluded.isEmpty())
        lines << QStringLiteral("<span style='color:%1'>Not in the overall grade: %2</span>")
                     .arg(muted, excluded.join(QStringLiteral(", ")));
    if (!otherBody.isEmpty())
        lines << QStringLiteral("<span style='color:%1'>No %2 standard for: %3 (switch the water body to grade them)</span>")
                     .arg(muted, body == WaterBody::River ? QStringLiteral("river") : QStringLiteral("lake"),
                          otherBody.join(QStringLiteral(", ")));
    if (!uncovered.isEmpty())
        lines << QStringLiteral("<span style='color:%1'>Not covered by the standard: %2</span>")
                     .arg(muted, uncovered.join(QStringLiteral(", ")));
    m_wqiDetail->setText(lines.join(QStringLiteral("<br>")));

    m_wqiMethod->setText(
        QStringLiteral("Every sample is graded against the %1 living-environment standards (생활환경기준, Framework Act on "
                       "Environmental Policy). A parameter's grade is the grade of its %2; the overall grade is the grade "
                       "of the most limiting parameter. Units assumed: mg/L (Chl-a mg/m³, coliforms CFU/100 mL). "
                       "Click a card to open its time series.")
            .arg(body == WaterBody::River ? QStringLiteral("river") : QStringLiteral("lake"),
                 stat == Statistic::Mean ? QStringLiteral("mean") : QStringLiteral("median")));

    // ---- one card per graded parameter -------------------------------------------
    QList<QWidget *> cards;
    for (const ParamResult &pr : std::as_const(m_wqi.params)) {
        auto *card = new WqiCard(m_data.columns[pr.column], pr, stat);
        connect(card, &WqiCard::clicked, this, [this](int column) {
            m_param->setCurrentIndex(column);
            m_tabBar->setCurrentIndex(1);
        });
        cards << card;
    }
    m_wqiCards->setCards(cards);
    m_wqiCards->setVisible(!cards.isEmpty());
    m_wqiEmpty->setVisible(cards.isEmpty());
    m_wqiEmpty->setText(
        QStringLiteral("None of the columns matches a parameter of the Korean %1 standard.\n"
                       "Recognised names include pH, BOD, COD, TOC, SS, DO, T-P / TP, T-N / TN, Chl-a, total and "
                       "fecal coliform, and the Korean names (용존산소, 총인, 총질소, 총유기탄소 …).")
            .arg(body == WaterBody::River ? QStringLiteral("river") : QStringLiteral("lake")));
}

void MainWindow::exportWqi()
{
    using namespace KoreanWqi;
    if (!m_hasData)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Korean WQI grades"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QFileInfo(m_fileName).completeBaseName() + QStringLiteral("_korean_wqi.csv")),
        QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true); // Excel opens the Korean grade names correctly
    out << "column,parameter,unit,water_body,statistic,value,grade,grade_ko,grade_en,in_overall,status,count,"
           "mean,median,min,max,q1,q3,p05,p95,share_Ia,share_Ib,share_II,share_III,share_IV,share_V,share_VI,"
           "grade_III_or_better\n";
    const QString body = m_wqi.body == WaterBody::River ? QStringLiteral("river") : QStringLiteral("lake");
    const QString stat = m_wqi.statistic == Statistic::Mean ? QStringLiteral("mean") : QStringLiteral("median");
    for (const ParamResult &pr : std::as_const(m_wqi.params)) {
        out << csvField(m_data.columns[pr.column]) << ',' << csvField(pr.standard->label) << ','
            << csvField(pr.standard->unit) << ',' << body << ',' << stat << ',' << pr.value << ','
            << gradeCode(pr.grade) << ',' << gradeNameKo(pr.grade) << ',' << gradeNameEn(pr.grade) << ','
            << (pr.included ? "yes" : "no") << ',' << csvField(pr.status) << ',' << pr.count << ',' << pr.mean << ','
            << pr.median << ',' << pr.min << ',' << pr.max << ',' << pr.q1 << ',' << pr.q3 << ',' << pr.p05 << ','
            << pr.p95;
        for (int g = 0; g < kGrades; ++g)
            out << ',' << pr.share[g];
        out << ',' << pr.compliance << '\n';
    }
    out << "OVERALL,,," << body << ',' << stat << ",," << gradeCode(m_wqi.overallGrade) << ','
        << gradeNameKo(m_wqi.overallGrade) << ',' << gradeNameEn(m_wqi.overallGrade) << ",yes,"
        << csvField(QStringLiteral("limiting: ") + m_wqi.limiting) << '\n';
    showToast(QStringLiteral("Korean WQI grades exported"));
}
