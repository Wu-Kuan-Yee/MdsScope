#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <QColor>
#include <QHash>
#include <QMainWindow>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QScrollArea>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QObject;
class QPainter;
class QPushButton;
class QComboBox;
class QRadioButton;
class QResizeEvent;
class QToolBar;
class QToolButton;
class GlobalPointOverlay;
class PointOverlay;

enum class InteractionMode {
    Zoom,
    Point,
    Pan,
};

enum class DataReadMode {
    Thin,
    Full,
};

int runMdsScopeBenchmark(const QString& configPath,
                         DataReadMode readMode = DataReadMode::Thin,
                         const QString& shotOverride = {},
                         bool summaryOnly = false,
                         bool prewarm = false);
void shutdownMdsScopeWorkers();

struct SignalSpec {
    QString shot;
    QString yExpr;
    QString xExpr;
    QString experiment;
    QString serverIp;
    QString colorName;
    bool manualColor = false;
    bool hidden = false;
};

struct PlotSpec {
    QString shot;
    QString title;
    QString xLabel;
    QString yLabel;
    int extractionPoints = 2000;
    bool grid = true;
    bool customXRange = false;
    bool customYRange = false;
    double xmin = qQNaN();
    double xmax = qQNaN();
    double ymin = qQNaN();
    double ymax = qQNaN();
    QVector<SignalSpec> signalSpecs;
};

struct LayoutConfig {
    QString filePath;
    QVector<QVector<PlotSpec>> columns;
};

struct SignalSeries {
    QString name;
    QString error;
    QVector<QPointF> points;
    QVector<float> uniformY;
    QVector<float> minYBlocks;
    QVector<float> maxYBlocks;
    double uniformStart = 0.0;
    double uniformStep = 1.0;
    double uniformMinY = qQNaN();
    double uniformMaxY = qQNaN();
    int minMaxBlockSize = 0;

    bool hasUniformData() const { return !uniformY.isEmpty(); }
    bool hasData() const { return hasUniformData() || !points.isEmpty(); }
    int pointCount() const { return hasUniformData() ? static_cast<int>(uniformY.size()) : static_cast<int>(points.size()); }
    QPointF pointAt(int index) const
    {
        if (hasUniformData()) {
            return QPointF(uniformStart + static_cast<double>(index) * uniformStep, uniformY[index]);
        }
        return points[index];
    }
};

struct LoadedSignal {
    int column = -1;
    int row = -1;
    int signal = -1;
    QString shot;
    SignalSeries series;
};

struct PointReadout {
    bool visible = false;
    bool showText = true;
    QRectF plotRect;
    QPointF pixel;
    QPointF data;
    QString text;
    QColor color = QColor("#333333");
};

class LoginDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(QString rootPath, QWidget* parent = nullptr);

private:
    void loadProperties();
    void tryLogin();

    QString rootPath_;
    QHash<QString, QString> properties_;
    QLineEdit* userEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* loginButton_ = nullptr;
};

class PlotWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PlotWidget(QWidget* parent = nullptr);

    void setSpec(PlotSpec spec);
    const PlotSpec& spec() const { return spec_; }
    void setSeries(int index, SignalSeries series);
    void clearSeries();
    void setSelected(bool selected);
    void setLargeDisplayMode(bool enabled);
    void refreshStyle();
    void setInteractionMode(InteractionMode mode);
    void resetScale(bool repaint = true);
    void applyView(const QRectF& view);
    void applyXRangeAutoY(double xmin, double xmax);
    QRectF currentView() const;
    bool hasView() const { return hasView_; }
    void setPointX(double x);
    void setSyncedPointX(double x, int seriesIndex);
    void clearSyncedPoint();
    bool updatePointFromGlobalPosition(const QPointF& globalPos, double* dataX = nullptr);
    PointReadout pointReadoutForX(double x, int seriesIndex, QWidget* target, bool includeText = true) const;
    int activePointSeriesIndex() const { return hoverSeriesIndex_; }

signals:
    void selected();
    void pointXChanged(double x);
    void pointTrackingStopped();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF plotRect() const;
    QRectF dataBounds() const;
    QRectF effectiveView() const;
    QPointF dataToPixel(const QPointF& p, const QRectF& view, const QRectF& pr) const;
    QPointF pixelToData(const QPointF& p, const QRectF& view, const QRectF& pr) const;
    QColor seriesColor(int index) const;
    void rebuildSeriesColorCache();
    void rebuildLegendCache();
    void expandFlatRange(QRectF& range) const;
    QVector<QPointF> displayPointsForSeries(const SignalSeries& series, const QRectF& view, double pixelWidth) const;
    bool updateHover(const QPointF& pixelPos, bool lockSeries = false);
    bool updateHoverForSeriesX(int seriesIndex, double dataX, bool lockSeries);
    bool stepActivePoint(int delta);
    bool nearestPointForSeries(int seriesIndex, double dataX, const QPointF* pixelPos, QPointF* point, QPointF* pixel, double* pixelDistance) const;
    int nearestSeriesAtPixel(const QPointF& pixelPos, double maxDistance, QPointF* point, QPointF* pixel) const;
    int legendSeriesAt(const QPointF& pixelPos) const;
    void renderBasePlot(QPainter& painter) const;
    void drawSyncedPoint(QPainter& painter) const;
    void drawZoomRubberBand(QPainter& painter) const;
    void drawSelectionBorder(QPainter& painter) const;
    QRect selectionBorderDirtyRect() const;
    QRect zoomRubberBandDirtyRect(const QRectF& band) const;
    QRect syncedPointDirtyRect(const PointReadout& readout) const;
    void invalidatePlotCache();
    void updatePointOverlay();
    void clearPointOverlay();
    void scheduleUpdate();
    void schedulePointHoverUpdate(const QPointF& pixelPos);

    PlotSpec spec_;
    QVector<SignalSeries> series_;
    QVector<QColor> seriesColors_;
    QStringList legendLabels_;
    QVector<int> legendSeriesIndexes_;
    QRectF view_;
    mutable QRectF cachedDataBounds_;
    mutable bool dataBoundsDirty_ = true;
    bool hasView_ = false;
    bool selected_ = false;
    bool largeDisplayMode_ = false;
    InteractionMode interactionMode_ = InteractionMode::Point;
    bool dragging_ = false;
    bool zooming_ = false;
    bool updateQueued_ = false;
    QPointF lastDragPos_;
    QPointF zoomStart_;
    QRectF zoomRubberBand_;
    QPointF hoverPixel_;
    QPointF hoverData_;
    QString hoverText_;
    PointReadout syncedPoint_;
    mutable QPixmap baseCache_;
    mutable QSize baseCacheSize_;
    mutable bool baseCacheDirty_ = true;
    int hoverSeriesIndex_ = -1;
    bool hoverSeriesLocked_ = false;
    bool pointTrackingActive_ = false;
    bool pointHoverQueued_ = false;
    QPointF pendingPointHoverPos_;
    PointOverlay* pointOverlay_ = nullptr;
};

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QString rootPath, QWidget* parent = nullptr);

private:
    void buildUi();
    void loadEnvironmentList(bool useLatestWhenNoCurrentShot = false);
    void loadSelectedEnvironment();
    void openEnvironmentFile();
    bool loadEnvironmentFile(const QString& path, bool useLatestWhenNoCurrentShot = false);
    void rebuildGrid();
    void selectPlot(int column, int row);
    void setInteractionMode(InteractionMode mode);
    void applyShot();
    void stepShot(int delta);
    void latestShot();
    void openLoginDialog();
    void applyLoginSuccessStatus(const QString& statusText);
    void updateLoginActionIcon();
    void fetchLatestShotAsync();
    void updateShotControlsFromConfig(const QString& preferredShot = {});
    void setAllPlotShots(const QString& shot);
    QString maxShotInConfig() const;
    QString latestShotFromApi() const;
    void refreshData();
    void prewarmConnections(bool refreshAfter);
    QString refreshKey(DataReadMode readMode) const;
    QString panelRefreshKey(int column, int row, int signal, DataReadMode readMode) const;
    void refreshOne(int column, int row, int signal);
    void queueLoadedSignal(const LoadedSignal& item);
    void flushQueuedLoadedSignals();
    void applyLoadedSignal(const LoadedSignal& item);
    void applyLoadedSignals(const QVector<LoadedSignal>& loaded);
    void applyPanelLoadedSignals(const QVector<LoadedSignal>& loaded);
    void rememberLoadedSourceSignal(const LoadedSignal& item);
    void addPlotBelow();
    void deleteCurrentPlot();
    void addSignalToCurrentPlot();
    void deleteSignalFromCurrentPlot();
    void panelSetupForCurrentPanel();
    void dataSourceSetupForCurrentPanel();
    void openExportDataDialog();
    void exportCurrentPanelData();
    void exportDataForPanels(const QVector<QPair<int, int>>& panels, const QString& baseDirPath);
    void applyScaleToAll();
    void resetCurrentScale();
    void resetScales();
    void maximizeCurrentPanel();
    void showAllPanels();
    void showPanelContextMenu(PlotWidget* plot, int column, int row, const QPoint& pos);
    void openLayoutSetupDialog();
    void openCustomizeDialog();
    void applyUiFont();
    void refreshPlotFonts();
    void saveCurrentEnvironment();
    void saveCurrentEnvironmentAs();
    bool saveEnvironmentFile(const QString& path) const;
    bool saveWebscpEnvironmentFile(const QString& path) const;
    PlotSpec defaultPlotFromSelection() const;
    void updateTopInfoLabels();
    bool loadShotSummaryFromApi(const QString& shot,
                                QString* ip,
                                QString* pulse,
                                QString* it,
                                QString* time) const;
    void setStatus(const QString& text);
    PlotWidget* currentPlotWidget() const;
    void schedulePointSync(PlotWidget* source, double x);
    void scheduleTopInfoUpdate(const QString& shot);

    QString rootPath_;
    QString environmentPath_;
    QString exportBasePath_;
    LayoutConfig config_;
    QListWidget* environmentList_ = nullptr;
    QWidget* gridHost_ = nullptr;
    QGridLayout* gridLayout_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* topInfoLabel_ = nullptr;
    QLabel* ipInfoLabel_ = nullptr;
    QLabel* pulseInfoLabel_ = nullptr;
    QLabel* itInfoLabel_ = nullptr;
    QLabel* timeInfoLabel_ = nullptr;
    QLineEdit* shotEdit_ = nullptr;
    QComboBox* dataModeCombo_ = nullptr;
    QAction* loginAction_ = nullptr;
    QToolButton* zoomButton_ = nullptr;
    QToolButton* pointButton_ = nullptr;
    QVector<QVector<PlotWidget*>> plotWidgets_;
    QFutureWatcher<QVector<LoadedSignal>> dataWatcher_;
    QFutureWatcher<QVector<LoadedSignal>> panelWatcher_;
    QFutureWatcher<void> warmWatcher_;
    QString topSummaryShot_;
    QString topSummaryIp_;
    QString topSummaryPulse_;
    QString topSummaryIt_;
    QString topSummaryTime_;
    QString pendingTopSummaryShot_;
    int topSummaryGeneration_ = 0;
    int selectedColumn_ = -1;
    int selectedRow_ = -1;
    bool pendingRefresh_ = false;
    bool pendingPanelRefresh_ = false;
    int pendingPanelColumn_ = -1;
    int pendingPanelRow_ = -1;
    int pendingPanelSignal_ = -1;
    bool warmRefreshPending_ = false;
    bool singlePanelMaximized_ = false;
    int maximizedColumn_ = -1;
    int maximizedRow_ = -1;
    InteractionMode currentInteractionMode_ = InteractionMode::Zoom;
    QString activeRefreshKey_;
    QString queuedRefreshKey_;
    QString activePanelRefreshKey_;
    QString queuedPanelRefreshKey_;
    int streamedOk_ = 0;
    int streamedFailed_ = 0;
    QVector<LoadedSignal> queuedLoadedSignals_;
    bool queuedLoadedSignalApply_ = false;
    QString latestShot_;
    bool latestShotFetchRunning_ = false;
    int latestShotGeneration_ = 0;
    GlobalPointOverlay* globalPointOverlay_ = nullptr;
    PlotWidget* activePointPlot_ = nullptr;
    PlotWidget* pointSyncSource_ = nullptr;
    bool pointSyncQueued_ = false;
    int pointSyncGeneration_ = 0;
    double pendingPointX_ = qQNaN();
};
