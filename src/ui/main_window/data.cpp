// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.h"
#include "shared.h"
#include "mds_client.h"

namespace {
QString signalKey(int column, int row, int signal)
{
    return QString::number(column) + ',' + QString::number(row) + ',' + QString::number(signal);
}

QString readModeKey(DataReadMode readMode)
{
    switch (readMode) {
    case DataReadMode::Thin:
        return QStringLiteral("thin");
    case DataReadMode::Medium:
        return QStringLiteral("medium");
    case DataReadMode::Full:
        return QStringLiteral("full");
    }
    return QStringLiteral("thin");
}
}


void MainWindow::refreshData()
{
    pendingPrewarmRefresh_ = false;
    cancelPrewarmConnections();
    // Any normal refresh invalidates a pending "Continue": the shot, config or
    // read mode may have changed, so resuming the old fetch no longer applies.
    clearDataPause();
    syncDisplayConfig();
    const DataReadMode readMode = dataModeCombo_
                                      ? static_cast<DataReadMode>(dataModeCombo_->currentData().toInt())
                                      : DataReadMode::Thin;
    const QString key = refreshKey(readMode);
    if (runningDataFetches_ > 0) {
        if (key == activeRefreshKey_ || key == queuedRefreshKey_) {
            setStatus("Data refresh already running for current shot");
            return;
        }
        cancelDataFetch();
        pendingRefresh_ = false;
        queuedRefreshKey_.clear();
        queuedLoadedSignals_.clear();
        queuedLoadedSignalApply_ = false;
        for (auto& col : plotWidgets_) {
            for (PlotWidget* plot : col) {
                plot->clearSeries();
            }
        }
        attemptedSignals_.clear();
        streamedOk_ = 0;
        streamedFailed_ = 0;
        launchDataFetch(displayConfig_, readMode, key);
        return;
    }
    pendingRefresh_ = false;
    queuedLoadedSignals_.clear();
    queuedLoadedSignalApply_ = false;
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            plot->clearSeries();
        }
    }
    attemptedSignals_.clear();
    streamedOk_ = 0;
    streamedFailed_ = 0;
    launchDataFetch(displayConfig_, readMode, key);
}

void MainWindow::launchDataFetch(const LayoutConfig& snapshot, DataReadMode readMode, const QString& key)
{
    activeFetchSnapshot_ = snapshot;
    activeFetchReadMode_ = readMode;
    activeRefreshKey_ = key;
    queuedRefreshKey_.clear();
    const int generation = ++activeDataFetchGeneration_;
    dataCancel_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = dataCancel_;
    setStatus(readMode == DataReadMode::Full ? "Fetching MDS data (full)..."
                  : readMode == DataReadMode::Medium ? "Fetching MDS data (medium)..."
                  : "Fetching MDS data (thin)...");
    auto* watcher = new QFutureWatcher<QVector<LoadedSignal>>(this);
    ++runningDataFetches_;
    connect(watcher, &QFutureWatcher<QVector<LoadedSignal>>::finished, this, [this, watcher, key, generation] {
        const QVector<LoadedSignal> loaded = watcher->result();
        watcher->deleteLater();
        runningDataFetches_ = std::max(0, runningDataFetches_ - 1);
        if (generation != activeDataFetchGeneration_ || key != activeRefreshKey_) {
            return;
        }
        applyLoadedSignals(loaded);
        // A prewarm may have finished while this fetch was in flight; its finished
        // handler leaves pendingPrewarmRefresh_ set in that case. Pick up the
        // deferred initial refresh now that the fetch has drained and nothing else
        // is running (applyLoadedSignals clears activeRefreshKey_ on completion).
        maybeStartDeferredRefresh();
    });
    watcher->setFuture(QtConcurrent::run([this, snapshot, readMode, key, cancel] {
        auto loaded = fetchMdsSignals(snapshot, readMode, [this, key, cancel](const LoadedSignal& item) {
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                return;
            }
            QMetaObject::invokeMethod(this, [this, key, item, cancel] {
                if ((!cancel || !cancel->load(std::memory_order_relaxed)) && key == activeRefreshKey_) {
                    queueLoadedSignal(item);
                }
            }, Qt::QueuedConnection);
        }, cancel);
        return loaded;
    }));
}

void MainWindow::onStopOrContinue()
{
    if (dataRefreshPaused_) {
        resumeDataRefresh();
    } else {
        stopDataRefresh();
    }
}

int MainWindow::countRemainingSignals(const LayoutConfig& snapshot) const
{
    int remaining = 0;
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            const PlotSpec& plot = snapshot.columns[c][r];
            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                if (plot.signalSpecs[s].hidden) {
                    continue;
                }
                if (!attemptedSignals_.contains(signalKey(c, r, s))) {
                    ++remaining;
                }
            }
        }
    }
    return remaining;
}

void MainWindow::stopDataRefresh()
{
    const bool wasRunning = runningDataFetches_ > 0;
    cancelDataFetch();
    cancelPanelFetch();
    const int remaining = countRemainingSignals(activeFetchSnapshot_);
    const QString activeKey = activeRefreshKey_;
    const DataReadMode activeMode = activeFetchReadMode_;
    activeRefreshKey_.clear();
    queuedRefreshKey_.clear();
    pendingRefresh_ = false;
    pendingResume_ = false;
    activePanelRefreshKey_.clear();
    queuedPanelRefreshKey_.clear();
    pendingPanelRefresh_ = false;
    queuedLoadedSignals_.clear();
    queuedLoadedSignalApply_ = false;
    if (wasRunning && remaining > 0) {
        pausedSnapshot_ = activeFetchSnapshot_;
        pausedReadMode_ = activeMode;
        pausedKey_ = activeKey;
        setStopButtonPaused(true);
        setStatus(QString("Data refresh paused: %1 signals remaining").arg(remaining));
    } else {
        clearDataPause();
        setStatus(wasRunning ? "Data refresh stopped" : "No data refresh running");
    }
}

void MainWindow::resumeDataRefresh()
{
    if (!dataRefreshPaused_) {
        return;
    }
    // Fetch only signals that were never attempted; mark the already-attempted
    // ones hidden so fetchMdsSignals skips them while keeping signal indices
    // stable (item.signal is the raw slot index).
    LayoutConfig remainingSnapshot = pausedSnapshot_;
    for (int c = 0; c < remainingSnapshot.columns.size(); ++c) {
        for (int r = 0; r < remainingSnapshot.columns[c].size(); ++r) {
            PlotSpec& plot = remainingSnapshot.columns[c][r];
            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                if (attemptedSignals_.contains(signalKey(c, r, s))) {
                    plot.signalSpecs[s].hidden = true;
                }
            }
        }
    }
    const DataReadMode readMode = pausedReadMode_;
    const QString key = pausedKey_;
    clearDataPause();
    if (runningDataFetches_ > 0) {
        // Old (cancelled) fetch is still winding down; queue the resume so we
        // do not oversubscribe the thread pool with a second fetch.
        cancelDataFetch();
        pendingResume_ = true;
        pendingResumeSnapshot_ = remainingSnapshot;
        pausedReadMode_ = readMode;
        pausedKey_ = key;
        activeRefreshKey_.clear();
        queuedLoadedSignals_.clear();
        queuedLoadedSignalApply_ = false;
        setStatus("Resuming after current fetch stops...");
        return;
    }
    setStatus("Resuming data fetch...");
    launchDataFetch(remainingSnapshot, readMode, key);
}

void MainWindow::clearDataPause()
{
    pendingResume_ = false;
    if (dataRefreshPaused_) {
        setStopButtonPaused(false);
    }
}

void MainWindow::setStopButtonPaused(bool paused)
{
    dataRefreshPaused_ = paused;
    if (stopButton_) {
        stopButton_->setText(paused ? "Continue" : "Stop");
    }
}

void MainWindow::cancelDataFetch()
{
    if (dataCancel_) {
        dataCancel_->store(true, std::memory_order_relaxed);
    }
}

void MainWindow::cancelPanelFetch()
{
    if (panelCancel_) {
        panelCancel_->store(true, std::memory_order_relaxed);
    }
}

void MainWindow::cancelPrewarmConnections()
{
    if (warmCancel_) {
        warmCancel_->store(true, std::memory_order_relaxed);
    }
}

bool MainWindow::prewarmConnections()
{
    if (config_.columns.isEmpty()) {
        return false;
    }
    if (warmWatcher_.isRunning()) {
        return false;
    }

    const LayoutConfig snapshot = expandedShotLayout(config_);
    warmCancel_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = warmCancel_;
    warmWatcher_.setFuture(QtConcurrent::run([snapshot, cancel] {
        warmMdsConnections(snapshot, cancel);
    }));
    return true;
}

bool MainWindow::canStartDeferredRefresh() const
{
    return runningDataFetches_ <= 0
        && activeRefreshKey_.isEmpty()
        && !latestShotFetchRunning_
        && !panelWatcher_.isRunning()
        && activePanelRefreshKey_.isEmpty()
        && !pendingPanelRefresh_;
}

void MainWindow::maybeStartDeferredRefresh()
{
    if (pendingPrewarmRefresh_ && canStartDeferredRefresh()) {
        pendingPrewarmRefresh_ = false;
        refreshData();
    }
}

QString MainWindow::refreshKey(DataReadMode readMode) const
{
    const QString shot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    return QString("%1|%2|%3")
        .arg(shot)
        .arg(readModeKey(readMode))
        .arg(layoutRefreshSignature(config_));
}

QString MainWindow::panelRefreshKey(int column, int row, int signal, DataReadMode readMode) const
{
    QString panelSignature;
    if (column >= 0 && row >= 0
        && column < config_.columns.size()
        && row < config_.columns[column].size()) {
        panelSignature = plotRefreshSignature(config_.columns[column][row]);
    }
    return QString("%1|%2|%3|%4|%5")
        .arg(column)
        .arg(row)
        .arg(signal)
        .arg(readModeKey(readMode))
        .arg(panelSignature);
}

void MainWindow::refreshOne(int column, int row, int signal)
{
    if (column < 0 || row < 0 || column >= config_.columns.size() || row >= config_.columns[column].size()) {
        return;
    }
    if (column >= plotWidgets_.size() || row >= plotWidgets_[column].size()) {
        return;
    }

    const DataReadMode readMode = dataModeCombo_
                                      ? static_cast<DataReadMode>(dataModeCombo_->currentData().toInt())
                                      : DataReadMode::Thin;
    const QString key = panelRefreshKey(column, row, signal, readMode);

    activeRefreshKey_.clear();
    pendingRefresh_ = false;
    queuedRefreshKey_.clear();
    queuedLoadedSignals_.clear();
    queuedLoadedSignalApply_ = false;

    if (panelWatcher_.isRunning()) {
        if (key == activePanelRefreshKey_ || key == queuedPanelRefreshKey_) {
            setStatus(QString("Panel refresh already running: col %1 row %2").arg(column + 1).arg(row + 1));
            return;
        }
        if (panelCancel_) {
            cancelPanelFetch();
        }
        pendingPanelRefresh_ = true;
        pendingPanelColumn_ = column;
        pendingPanelRow_ = row;
        pendingPanelSignal_ = signal;
        queuedPanelRefreshKey_ = key;
        activePanelRefreshKey_.clear();
        setStatus(QString("Panel refresh queued: col %1 row %2").arg(column + 1).arg(row + 1));
        return;
    }

    syncDisplayConfig();
    LayoutConfig snapshot = displayConfig_;
    const bool singleSignalRefresh = false;
    Q_UNUSED(signal);
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            if (c == column && r == row) {
                continue;
            }
            snapshot.columns[c][r].signalSpecs.clear();
        }
    }
    if (!singleSignalRefresh) {
        plotWidgets_[column][row]->clearSeries();
    }
    activePanelRefreshKey_ = key;
    queuedPanelRefreshKey_.clear();
    panelCancel_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = panelCancel_;
    setStatus(singleSignalRefresh
                  ? QString("Fetching signal data: col %1 row %2 source %3").arg(column + 1).arg(row + 1).arg(signal + 1)
                  : QString("Fetching panel data: col %1 row %2").arg(column + 1).arg(row + 1));
    panelWatcher_.setFuture(QtConcurrent::run([snapshot, readMode, singleSignalRefresh, cancel] {
        QVector<LoadedSignal> loaded = fetchMdsSignals(snapshot, readMode, {}, cancel);
        Q_UNUSED(singleSignalRefresh);
        return loaded;
    }));
}

void MainWindow::queueLoadedSignal(const LoadedSignal& item)
{
    queuedLoadedSignals_.push_back(item);
    if (queuedLoadedSignalApply_) {
        return;
    }
    queuedLoadedSignalApply_ = true;
    QTimer::singleShot(16, this, [this] {
        flushQueuedLoadedSignals();
    });
}

void MainWindow::flushQueuedLoadedSignals()
{
    if (queuedLoadedSignals_.isEmpty()) {
        queuedLoadedSignalApply_ = false;
        return;
    }
    QVector<LoadedSignal> batch;
    batch.swap(queuedLoadedSignals_);
    queuedLoadedSignalApply_ = false;
    for (const LoadedSignal& item : std::as_const(batch)) {
        applyLoadedSignal(item);
    }
}

void MainWindow::applyLoadedSignal(const LoadedSignal& item)
{
    if (item.column < 0 || item.row < 0 || item.column >= plotWidgets_.size() || item.row >= plotWidgets_[item.column].size()) {
        return;
    }
    if (item.column >= displayConfig_.columns.size()
        || item.row >= displayConfig_.columns[item.column].size()
        || !loadedSignalMatchesConfig(displayConfig_, item)) {
        return;
    }

    plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
    // Record every delivered slot (loaded or failed) so a later Continue only
    // re-fetches signals that were never attempted.
    attemptedSignals_.insert(signalKey(item.column, item.row, item.signal));
    if (!item.series.hasData()) {
        ++streamedFailed_;
    } else {
        ++streamedOk_;
        rememberLoadedSourceSignal(item);
    }
    const int streamedTotal = streamedOk_ + streamedFailed_;
    if (streamedTotal == 1 || streamedTotal % 8 == 0) {
        setStatus(QString("Data refresh: %1 signals loaded, %2 failed...").arg(streamedOk_).arg(streamedFailed_));
    }
}

void MainWindow::applyLoadedSignals(const QVector<LoadedSignal>& loaded)
{
    flushQueuedLoadedSignals();
    if (activeRefreshKey_.isEmpty()) {
        if (pendingResume_) {
            pendingResume_ = false;
            launchDataFetch(pendingResumeSnapshot_, pausedReadMode_, pausedKey_);
            return;
        }
        if (pendingRefresh_) {
            pendingRefresh_ = false;
            refreshData();
        }
        return;
    }
    if (pendingRefresh_) {
        pendingRefresh_ = false;
        activeRefreshKey_.clear();
        refreshData();
        return;
    }
    activeRefreshKey_.clear();
    if (streamedOk_ + streamedFailed_ >= loaded.size()) {
        setStatus(QString("Data refresh done: %1 signals loaded, %2 failed").arg(streamedOk_).arg(streamedFailed_));
        return;
    }

    int ok = 0;
    int failed = 0;
    for (const LoadedSignal& item : loaded) {
        if (item.column < plotWidgets_.size() && item.row < plotWidgets_[item.column].size()) {
            if (!loadedSignalMatchesConfig(displayConfig_, item)) {
                continue;
            }
            plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
            if (!item.series.hasData()) {
                ++failed;
            } else {
                ++ok;
                rememberLoadedSourceSignal(item);
            }
        }
    }
    setStatus(QString("Data refresh done: %1 signals loaded, %2 failed").arg(ok).arg(failed));
}

void MainWindow::applyPanelLoadedSignals(const QVector<LoadedSignal>& loaded)
{
    int ok = 0;
    int failed = 0;
    for (const LoadedSignal& item : loaded) {
        if (item.column < 0 || item.row < 0
            || item.column >= plotWidgets_.size()
            || item.row >= plotWidgets_[item.column].size()
            || !loadedSignalMatchesConfig(displayConfig_, item)) {
            continue;
        }
        plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
        if (item.series.hasData()) {
            ++ok;
            rememberLoadedSourceSignal(item);
        } else {
            ++failed;
        }
    }
    setStatus(QString("Panel refresh done: %1 signals loaded, %2 failed").arg(ok).arg(failed));
}

void MainWindow::rememberLoadedSourceSignal(const LoadedSignal& item)
{
    if (!item.series.hasData()
        || item.column < 0 || item.row < 0 || item.signal < 0
        || item.column >= displayConfig_.columns.size()
        || item.row >= displayConfig_.columns[item.column].size()
        || item.signal >= displayConfig_.columns[item.column][item.row].signalSpecs.size()) {
        return;
    }
    const SignalSpec& sig = displayConfig_.columns[item.column][item.row].signalSpecs[item.signal];
    const QString tree = sig.experiment.trimmed().toLower();
    const QString signal = normalizedMdsSignal(sig.yExpr).trimmed().toLower();
    if (tree.isEmpty() || signal.isEmpty()) {
        return;
    }
    const QString key = tree + QChar('\n') + signal;
    if (rememberedSourceSignals_.contains(key)) {
        return;
    }
    rememberedSourceSignals_.insert(key);
    addSourceIndexSignal(sig.experiment, sig.yExpr);
}
