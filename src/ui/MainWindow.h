#pragma once

#include "Analyzer.h"
#include "CsvParser.h"
#include "Database.h"
#include "KoreanWqi.h"

#include <QFutureWatcher>
#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabBar;
class QTableWidget;
class QTimer;
class CardGrid;
class DropZone;
class InteractiveChartView;

/** Output of a background parse + analysis job. */
struct JobResult
{
    bool ok = false;
    QString error;
    Dataset dataset;
    AnalysisResult result;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString &path) { loadFile(path); }
    void openSampleData() { openSample(); }

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private slots:
    void newAnalysis();
    void browseFile();
    void openSample();
    void onSessionClicked(QListWidgetItem *item);
    void onSessionContextMenu(const QPoint &pos);
    void onMethodChanged(int index);
    void onSettingsEdited();
    void runReanalysis();
    void onJobFinished();
    void onParameterChanged(int index);
    void onTabChanged(int index);
    void onAnomalyActivated(int row, int column);
    void toggleTheme();
    void exportChart();
    void exportStats();
    void exportAnomalies();
    void exportWqi();
    void showAbout();

private:
    enum class JobKind { NewFile, OpenSession, Reanalyze };
    struct Job
    {
        JobKind kind = JobKind::NewFile;
        qint64 sessionId = -1;
        QString title;
        QString fileName;
        QByteArray csv;
        AnalysisSettings settings;
    };

    QWidget *buildSidebar();
    QWidget *buildWelcome();
    QWidget *buildWorkspace();
    QWidget *makeKpi(const QString &caption, QLabel *&value, QLabel *&hint);

    void loadFile(const QString &path);
    void startJob(const Job &job);
    void setBusy(bool busy);
    void showToast(const QString &text);

    void refreshSessions();
    void applySettingsToControls(const AnalysisSettings &s);
    AnalysisSettings settingsFromControls() const;
    QJsonObject buildSummary() const;
    void updateThemeButton();

    void renderAll(bool fresh);
    void renderHeader();
    void renderKpis();
    void renderOverview();
    void renderTimeSeries();
    void renderDistribution();
    void renderStatsTable();
    void renderAnomalyTable();
    void renderWqi();
    QWidget *buildWqiPage();
    double xFor(int row) const;

    // ---- state --------------------------------------------------------------
    Database m_db;
    bool m_dbOk = false;
    qint64 m_sessionId = -1;
    QString m_title;
    QString m_fileName;
    QByteArray m_csv;
    Dataset m_data;
    AnalysisResult m_result;
    KoreanWqi::Result m_wqi;
    bool m_hasData = false;

    QFutureWatcher<JobResult> m_watcher;
    Job m_job;
    bool m_rerunPending = false;
    QTimer *m_debounce = nullptr;
    QTimer *m_toastTimer = nullptr;

    // ---- widgets ------------------------------------------------------------
    QListWidget *m_sessionList = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_emptyHistory = nullptr;
    QPushButton *m_themeBtn = nullptr;
    QProgressBar *m_busy = nullptr;
    QStackedWidget *m_stack = nullptr;
    DropZone *m_dropZone = nullptr;
    QLabel *m_toast = nullptr;

    QLabel *m_titleLbl = nullptr;
    QLabel *m_subtitleLbl = nullptr;
    QLabel *m_warnLbl = nullptr;
    QComboBox *m_method = nullptr;
    QDoubleSpinBox *m_threshold = nullptr;
    QSpinBox *m_window = nullptr;
    QLabel *m_kpiValue[5] = {};
    QLabel *m_kpiHint[5] = {};

    QTabBar *m_tabBar = nullptr;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_paramBox = nullptr;
    QComboBox *m_param = nullptr;

    CardGrid *m_cards = nullptr;
    InteractiveChartView *m_tsView = nullptr;
    QCheckBox *m_showSmooth = nullptr;
    QCheckBox *m_showBand = nullptr;
    QCheckBox *m_showAnoms = nullptr;
    InteractiveChartView *m_distView = nullptr;
    QLabel *m_distStats = nullptr;
    QComboBox *m_anomFilter = nullptr;
    QLabel *m_anomCount = nullptr;
    QTableWidget *m_anomTable = nullptr;
    QLabel *m_anomEmpty = nullptr;
    QTableWidget *m_statsTable = nullptr;

    // Korean WQI page
    QWidget *m_wqiPage = nullptr;
    QComboBox *m_wqiBody = nullptr;
    QComboBox *m_wqiStat = nullptr;
    QLabel *m_wqiGrade = nullptr;
    QLabel *m_wqiGradeName = nullptr;
    QLabel *m_wqiUse = nullptr;
    QLabel *m_wqiDetail = nullptr;
    QLabel *m_wqiMethod = nullptr;
    QLabel *m_wqiEmpty = nullptr;
    CardGrid *m_wqiCards = nullptr;
};
