// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "shared.hpp"
#include "mds_client.hpp"
#include "ui/plot/helpers.hpp"

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
    // A manual load must not block the UI or interrupt the global prewarm.
    // Queue its refresh; the warm-watcher completion starts it on the same
    // persistent worker connections.
    if (warmWatcher_.isRunning()) {
        clearDataPause();
        syncDisplayConfig();
        pendingRefresh_ = true;
        queuedRefreshKey_.clear();
        setStatus("Configuration loaded; waiting for global MDS prewarm...");
        return;
    }
    // Any normal refresh invalidates a pending "Continue": the shot, config or
    // read mode may have changed, so resuming the old fetch no longer applies.
    clearDataPause();
    syncDisplayConfig();
    const DataReadMode readMode = dataModeCombo_
                                      ? static_cast<DataReadMode>(dataModeCombo_->currentData().toInt())
                                      : DataReadMode::Thin;
    const QString key = refreshKey(readMode);
    if (runningDataFetches_ > 0 || panelWatcher_.isRunning()) {
        if (key == activeRefreshKey_ || key == queuedRefreshKey_) {
            setStatus("Data refresh already running for current shot");
            return;
        }
        cancelDataFetch();
        cancelPanelFetch();
        activeRefreshKey_.clear();
        activePanelRefreshKey_.clear();
        ++activeDataFetchGeneration_;
        pendingRefresh_ = true;
        queuedRefreshKey_ = key;
        pendingPanelRefresh_ = false;
        queuedPanelRefreshKey_.clear();
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
        setStatus("Data refresh queued until current work stops...");
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
    LayoutConfig fetchSnapshot;
    if (!prepareSshLayout(snapshot, &fetchSnapshot)) {
        activeRefreshKey_.clear();
        return;
    }
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
        runningDataFetches_ = std::max(0, runningDataFetches_ - 1);
        if (generation == activeDataFetchGeneration_ && key == activeRefreshKey_) {
            applyLoadedSignals(watcher->result());
        }
        watcher->deleteLater();
        startPendingFetchIfIdle();
    });
    watcher->setFuture(QtConcurrent::run([this, fetchSnapshot, readMode, key, cancel] {
        auto loaded = fetchMdsSignals(fetchSnapshot, readMode, [this, key, cancel](const LoadedSignal& item) {
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                return;
            }
            LoadedSignal prepared = item;
            rebuildMinMaxIndex(prepared.series);
            QMetaObject::invokeMethod(this, [this, key, item = std::move(prepared), cancel]() mutable {
                if ((!cancel || !cancel->load(std::memory_order_relaxed)) && key == activeRefreshKey_) {
                    queueLoadedSignal(std::move(item));
                }
            }, Qt::QueuedConnection);
        }, cancel, true);
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
    if (runningDataFetches_ > 0 || panelWatcher_.isRunning() || warmWatcher_.isRunning()) {
        // Old (cancelled) fetch is still winding down; queue the resume so we
        // do not oversubscribe the thread pool with a second fetch.
        cancelDataFetch();
        cancelPanelFetch();
        cancelPrewarmConnections();
        activePanelRefreshKey_.clear();
        pendingPanelRefresh_ = false;
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

void MainWindow::startPendingFetchIfIdle()
{
    if (runningDataFetches_ > 0 || panelWatcher_.isRunning() || warmWatcher_.isRunning()) {
        return;
    }
    if (pendingResume_) {
        pendingResume_ = false;
        launchDataFetch(pendingResumeSnapshot_, pausedReadMode_, pausedKey_);
        return;
    }
    if (pendingRefresh_) {
        pendingRefresh_ = false;
        queuedRefreshKey_.clear();
        refreshData();
        return;
    }
    if (pendingPanelRefresh_) {
        const int column = pendingPanelColumn_;
        const int row = pendingPanelRow_;
        const int signal = pendingPanelSignal_;
        pendingPanelRefresh_ = false;
        pendingPanelColumn_ = -1;
        pendingPanelRow_ = -1;
        pendingPanelSignal_ = -1;
        queuedPanelRefreshKey_.clear();
        refreshOne(column, row, signal);
        return;
    }
    maybeStartDeferredRefresh();
}

bool MainWindow::prewarmConnections()
{
    if (config_.columns.isEmpty()) {
        return false;
    }
    if (warmWatcher_.isRunning() || runningDataFetches_ > 0 || panelWatcher_.isRunning()) {
        return false;
    }

    const LayoutConfig source = expandedShotLayout(config_);
    LayoutConfig snapshot;
    if (!prepareSshLayout(source, &snapshot)) {
        return false;
    }
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

    cancelPrewarmConnections();
    pendingRefresh_ = false;
    queuedRefreshKey_.clear();
    queuedLoadedSignals_.clear();
    queuedLoadedSignalApply_ = false;

    if (runningDataFetches_ > 0 || panelWatcher_.isRunning() || warmWatcher_.isRunning()) {
        if (key == activePanelRefreshKey_ || key == queuedPanelRefreshKey_) {
            setStatus(QString("Panel refresh already running: col %1 row %2").arg(column + 1).arg(row + 1));
            return;
        }
        cancelDataFetch();
        cancelPanelFetch();
        activeRefreshKey_.clear();
        activePanelRefreshKey_.clear();
        ++activeDataFetchGeneration_;
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
    const bool singleSignalRefresh = signal >= 0
                                     && signal < snapshot.columns[column][row].signalSpecs.size();
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            if (c == column && r == row) {
                if (singleSignalRefresh) {
                    for (int s = 0; s < snapshot.columns[c][r].signalSpecs.size(); ++s) {
                        if (s != signal) {
                            snapshot.columns[c][r].signalSpecs[s].hidden = true;
                        }
                    }
                }
                continue;
            }
            snapshot.columns[c][r].signalSpecs.clear();
        }
    }
    LayoutConfig fetchSnapshot;
    if (!prepareSshLayout(snapshot, &fetchSnapshot)) {
        activePanelRefreshKey_.clear();
        return;
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
    panelWatcher_.setFuture(QtConcurrent::run([fetchSnapshot, readMode, singleSignalRefresh, cancel] {
        QVector<LoadedSignal> loaded = fetchMdsSignals(fetchSnapshot, readMode, {}, cancel, true);
        for (LoadedSignal& item : loaded) {
            rebuildMinMaxIndex(item.series);
        }
        Q_UNUSED(singleSignalRefresh);
        return loaded;
    }));
}

void MainWindow::queueLoadedSignal(LoadedSignal item)
{
    queuedLoadedSignals_.push_back(std::move(item));
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
    for (LoadedSignal& item : batch) {
        applyLoadedSignal(std::move(item));
    }
}

void MainWindow::applyLoadedSignal(LoadedSignal item)
{
    if (item.column < 0 || item.row < 0 || item.column >= plotWidgets_.size() || item.row >= plotWidgets_[item.column].size()) {
        return;
    }
    if (item.column >= displayConfig_.columns.size()
        || item.row >= displayConfig_.columns[item.column].size()
        || !loadedSignalMatchesConfig(displayConfig_, item)) {
        return;
    }

    // Remember the source signal and capture hasData before the series is moved
    // into setSeries (both read item.series, which move leaves empty).
    const bool hasData = item.series.hasData();
    if (hasData) {
        rememberLoadedSourceSignal(item);
    }
    plotWidgets_[item.column][item.row]->setSeries(item.signal, std::move(item.series));
    // Record every delivered slot (loaded or failed) so a later Continue only
    // re-fetches signals that were never attempted.
    attemptedSignals_.insert(signalKey(item.column, item.row, item.signal));
    if (!hasData) {
        ++streamedFailed_;
    } else {
        ++streamedOk_;
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
        const QString key = signalKey(item.column, item.row, item.signal);
        if (attemptedSignals_.contains(key)) {
            continue;
        }
        if (item.column < plotWidgets_.size() && item.row < plotWidgets_[item.column].size()) {
            if (!loadedSignalMatchesConfig(displayConfig_, item)) {
                continue;
            }
            plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
            attemptedSignals_.insert(key);
            if (!item.series.hasData()) {
                ++failed;
            } else {
                ++ok;
                rememberLoadedSourceSignal(item);
            }
        }
    }
    setStatus(QString("Data refresh done: %1 signals loaded, %2 failed")
                  .arg(streamedOk_ + ok)
                  .arg(streamedFailed_ + failed));
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
