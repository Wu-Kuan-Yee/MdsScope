// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.h"
#include "layout_dialog.h"
#include "shared.h"
#include "signal_dialogs.h"
#include "point_overlay.h"

namespace {
struct PreservedPanelData {
    int column = -1;
    int row = -1;
    QVector<SignalSeries> series;
    bool hasView = false;
    QRectF view;
};
}

void MainWindow::rebuildGrid()
{
    const int previousRows = gridLayout_->rowCount();
    const int previousColumns = gridLayout_->columnCount();
    while (QLayoutItem* item = gridLayout_->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    for (int r = 0; r < previousRows; ++r) {
        gridLayout_->setRowStretch(r, 0);
    }
    for (int c = 0; c < previousColumns; ++c) {
        gridLayout_->setColumnStretch(c, 0);
    }
    activePointPlot_ = nullptr;
    pointSyncSource_ = nullptr;
    pointSyncQueued_ = false;
    singlePanelMaximized_ = false;
    maximizedColumn_ = -1;
    maximizedRow_ = -1;
    if (globalPointOverlay_) {
        globalPointOverlay_->clearReadouts();
    }
    syncDisplayConfig();
    plotWidgets_.clear();
    plotWidgets_.resize(displayConfig_.columns.size());

    for (int c = 0; c < displayConfig_.columns.size(); ++c) {
        auto* columnHost = new QWidget(gridHost_);
        columnHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto* columnLayout = new QVBoxLayout(columnHost);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(0);
        plotWidgets_[c].resize(displayConfig_.columns[c].size());
        for (int r = 0; r < displayConfig_.columns[c].size(); ++r) {
            auto* plot = new PlotWidget(columnHost);
            plot->setSpec(displayConfig_.columns[c][r]);
            plot->setLargeDisplayMode(false);
            if (zoomButton_ && zoomButton_->isChecked()) {
                plot->setInteractionMode(InteractionMode::Zoom);
            } else {
                plot->setInteractionMode(InteractionMode::Point);
            }
            plot->setContextMenuPolicy(Qt::CustomContextMenu);
            plotWidgets_[c][r] = plot;
            columnLayout->addWidget(plot, 1);
            connect(plot, &QWidget::customContextMenuRequested, this, [this, plot, c, r](const QPoint& pos) {
                showPanelContextMenu(plot, c, r, pos);
            });
            connect(plot, &PlotWidget::selected, this, [this, plot, c, r] {
                selectPlot(c, r);
                if (pointButton_ && pointButton_->isChecked()) {
                    activePointPlot_ = plot;
                }
            });
            connect(plot, &PlotWidget::pointTrackingStopped, this, [this, plot] {
                if (activePointPlot_ == plot) {
                    activePointPlot_ = nullptr;
                }
                pointSyncSource_ = nullptr;
                pointSyncQueued_ = false;
                pendingPointX_ = qQNaN();
                ++pointSyncGeneration_;
            });
            connect(plot, &PlotWidget::pointXChanged, this, [this, plot](double x) {
                if (!(pointButton_ && pointButton_->isChecked())) {
                    return;
                }
                if (!std::isfinite(x)) {
                    if (activePointPlot_ == plot) {
                        activePointPlot_ = nullptr;
                    }
                    pointSyncSource_ = nullptr;
                    pointSyncQueued_ = false;
                    pendingPointX_ = qQNaN();
                    for (auto& col : plotWidgets_) {
                        for (PlotWidget* other : col) {
                            if (other) {
                                other->clearSyncedPoint();
                            }
                        }
                    }
                    return;
                }
                activePointPlot_ = plot;
                schedulePointSync(plot, x);
            });
        }
        gridLayout_->addWidget(columnHost, 0, c);
        gridLayout_->setColumnStretch(c, 1);
    }
    gridLayout_->setRowStretch(0, 1);
    gridHost_->updateGeometry();
}

void MainWindow::syncDisplayConfig()
{
    const LayoutConfig previous = displayConfig_;
    displayConfig_ = expandedShotLayout(config_);
    for (int c = 0; c < plotWidgets_.size() && c < displayConfig_.columns.size(); ++c) {
        for (int r = 0; r < plotWidgets_[c].size() && r < displayConfig_.columns[c].size(); ++r) {
            if (plotWidgets_[c][r]) {
                const bool changed = c >= previous.columns.size()
                                     || r >= previous.columns[c].size()
                                     || plotRefreshSignature(previous.columns[c][r]) != plotRefreshSignature(displayConfig_.columns[c][r]);
                if (changed) {
                    plotWidgets_[c][r]->setSpec(displayConfig_.columns[c][r]);
                }
            }
        }
    }
}

void MainWindow::selectPlot(int column, int row)
{
    if (column < 0 || row < 0 || column >= plotWidgets_.size() || row >= plotWidgets_[column].size()) {
        return;
    }
    selectedColumn_ = column;
    selectedRow_ = row;
    for (int c = 0; c < plotWidgets_.size(); ++c) {
        for (int r = 0; r < plotWidgets_[c].size(); ++r) {
            plotWidgets_[c][r]->setSelected(c == column && r == row);
        }
    }
    const auto& plot = config_.columns[column][row];
    updateTopInfoLabels();
    setStatus(QString("Selected col %1 row %2: %3").arg(column + 1).arg(row + 1).arg(plot.title.isEmpty() ? plot.shot : plot.title));
}

void MainWindow::openLayoutSetupDialog()
{
    if (config_.columns.isEmpty()) {
        config_.columns.resize(1);
    }

    LayoutSetupDialog dialog(config_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    auto makePanel = [this] {
        PlotSpec plot = defaultPlotFromSelection();
        plot.title.clear();
        plot.signalSpecs.clear();
        if (shotEdit_ && !shotEdit_->text().trimmed().isEmpty()) {
            plot.shot = shotEdit_->text().trimmed();
        }
        plot.customXRange = false;
        plot.customYRange = false;
        plot.xmin = qQNaN();
        plot.xmax = qQNaN();
        plot.ymin = qQNaN();
        plot.ymax = qQNaN();
        return plot;
    };

    const QVector<QVector<LayoutCanvas::Item>> layout = dialog.layoutItems();
    if (layoutItemsMatchConfig(layout, config_)) {
        setStatus("Layout unchanged");
        return;
    }

    LayoutConfig next = config_;
    next.columns.clear();
    int selectColumn = -1;
    int selectRow = -1;
    int newPanels = 0;
    int keptPanels = 0;
    QVector<PreservedPanelData> preservedPanels;

    for (const auto& column : layout) {
        QVector<PlotSpec> nextColumn;
        for (const LayoutCanvas::Item& item : column) {
            if (item.isNew) {
                nextColumn.push_back(makePanel());
                if (selectColumn < 0) {
                    selectColumn = next.columns.size();
                    selectRow = nextColumn.size() - 1;
                }
                ++newPanels;
                continue;
            }
            if (item.originalColumn >= 0
                && item.originalColumn < config_.columns.size()
                && item.originalRow >= 0
                && item.originalRow < config_.columns[item.originalColumn].size()) {
                if (item.originalColumn < plotWidgets_.size()
                    && item.originalRow < plotWidgets_[item.originalColumn].size()
                    && plotWidgets_[item.originalColumn][item.originalRow]) {
                    PreservedPanelData preserved;
                    preserved.column = next.columns.size();
                    preserved.row = nextColumn.size();
                    preserved.series = plotWidgets_[item.originalColumn][item.originalRow]->seriesSnapshot();
                    preserved.hasView = plotWidgets_[item.originalColumn][item.originalRow]->hasView();
                    if (preserved.hasView) {
                        preserved.view = plotWidgets_[item.originalColumn][item.originalRow]->currentView();
                    }
                    preservedPanels.push_back(std::move(preserved));
                }
                nextColumn.push_back(config_.columns[item.originalColumn][item.originalRow]);
                ++keptPanels;
            }
        }
        if (!nextColumn.isEmpty()) {
            next.columns.push_back(std::move(nextColumn));
        }
    }

    if (next.columns.isEmpty()) {
        next.columns.resize(1);
    }
    if (selectColumn < 0) {
        selectColumn = selectedColumn_ >= 0 ? selectedColumn_ : 0;
        selectColumn = std::clamp(selectColumn, 0, std::max(0, static_cast<int>(next.columns.size()) - 1));
        selectRow = next.columns[selectColumn].isEmpty()
            ? -1
            : std::clamp(selectedRow_ >= 0 ? selectedRow_ : 0,
                         0,
                         static_cast<int>(next.columns[selectColumn].size()) - 1);
    }
    const int oldPanels = std::accumulate(config_.columns.begin(), config_.columns.end(), 0, [](int total, const QVector<PlotSpec>& column) {
        return total + column.size();
    });
    const int deletedPanels = std::max(0, oldPanels - keptPanels);
    config_ = std::move(next);
    setStatus(QString("Layout updated: %1 deleted, %2 inserted").arg(deletedPanels).arg(newPanels));

    rebuildGrid();
    if (selectColumn >= 0 && selectColumn < plotWidgets_.size()
        && selectRow >= 0 && selectRow < plotWidgets_[selectColumn].size()) {
        selectPlot(selectColumn, selectRow);
    }
    bool shotChanged = false;
    const QString currentShot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    if (!currentShot.isEmpty()) {
        const QString before = layoutRefreshSignature(config_);
        setAllPlotShots(currentShot);
        shotChanged = before != layoutRefreshSignature(config_);
    }
    if (shotChanged) {
        refreshData();
        return;
    }
    for (const PreservedPanelData& preserved : std::as_const(preservedPanels)) {
        if (preserved.column < 0 || preserved.row < 0
            || preserved.column >= plotWidgets_.size()
            || preserved.row >= plotWidgets_[preserved.column].size()
            || !plotWidgets_[preserved.column][preserved.row]) {
            continue;
        }
        PlotWidget* plot = plotWidgets_[preserved.column][preserved.row];
        for (int i = 0; i < preserved.series.size(); ++i) {
            plot->setSeries(i, preserved.series[i]);
        }
        if (preserved.hasView) {
            plot->applyView(preserved.view);
        }
    }
}

PlotSpec MainWindow::defaultPlotFromSelection() const
{
    if (selectedColumn_ >= 0 && selectedRow_ >= 0 &&
        selectedColumn_ < config_.columns.size() && selectedRow_ < config_.columns[selectedColumn_].size()) {
        PlotSpec plot = config_.columns[selectedColumn_][selectedRow_];
        plot.title.clear();
        plot.signalSpecs.clear();
        return plot;
    }
    PlotSpec plot;
    plot.shot = "0";
    return plot;
}

void MainWindow::addPlotBelow()
{
    if (config_.columns.isEmpty()) {
        config_.columns.resize(1);
    }
    const int column = selectedColumn_ >= 0 ? selectedColumn_ : 0;
    const int row = selectedRow_ >= 0 ? selectedRow_ + 1 : config_.columns[column].size();
    SignalDialog dialog(defaultPlotFromSelection(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    PlotSpec plot = defaultPlotFromSelection();
    plot.shot = dialog.shot();
    SignalSpec sig = dialog.signal();
    if (sig.yExpr.isEmpty()) {
        QMessageBox::warning(this, "Add Plot", "Y expr is required.");
        return;
    }
    plot.title = sig.yExpr;
    plot.signalSpecs.push_back(sig);
    config_.columns[column].insert(row, plot);
    rebuildGrid();
    selectPlot(column, row);
    refreshOne(column, row, 0);
}

void MainWindow::deleteCurrentPlot()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0) {
        return;
    }
    config_.columns[selectedColumn_].removeAt(selectedRow_);
    const int newColumn = selectedColumn_;
    const int newRow = std::min(selectedRow_, static_cast<int>(config_.columns[newColumn].size()) - 1);
    rebuildGrid();
    if (newRow >= 0) {
        selectPlot(newColumn, newRow);
    }
    refreshData();
}

void MainWindow::addSignalToCurrentPlot()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0) {
        return;
    }
    PlotSpec& plot = config_.columns[selectedColumn_][selectedRow_];
    SignalDialog dialog(plot, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    SignalSpec sig = dialog.signal();
    if (sig.yExpr.isEmpty()) {
        QMessageBox::warning(this, "Add Signal", "Y expr is required.");
        return;
    }
    sig.colorName = colorForIndex(plot.signalSpecs.size());
    plot.shot = dialog.shot();
    plot.signalSpecs.push_back(sig);
    normalizePresetColors(plot.signalSpecs);
    syncDisplayConfig();
    refreshOne(selectedColumn_, selectedRow_, -1);
}

void MainWindow::deleteSignalFromCurrentPlot()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0) {
        return;
    }
    PlotSpec& plot = config_.columns[selectedColumn_][selectedRow_];
    if (plot.signalSpecs.isEmpty()) {
        return;
    }
    QStringList items;
    for (const auto& sig : plot.signalSpecs) {
        items.push_back(sig.yExpr);
    }
    bool ok = false;
    const QString choice = QInputDialog::getItem(this, "Delete Signal", "Signal", items, 0, false, &ok);
    if (!ok) {
        return;
    }
    const int idx = items.indexOf(choice);
    if (idx >= 0) {
        plot.signalSpecs.removeAt(idx);
        normalizePresetColors(plot.signalSpecs);
        syncDisplayConfig();
        refreshOne(selectedColumn_, selectedRow_, -1);
    }
}

void MainWindow::panelSetupForCurrentPanel()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0
        || selectedColumn_ >= config_.columns.size()
        || selectedRow_ >= config_.columns[selectedColumn_].size()
        || selectedColumn_ >= plotWidgets_.size()
        || selectedRow_ >= plotWidgets_[selectedColumn_].size()) {
        return;
    }

    PlotSpec& plot = config_.columns[selectedColumn_][selectedRow_];
    PanelSetupDialog dialog(plot, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    dialog.applyTo(&plot);
    displayConfig_ = expandedShotLayout(config_);
    plotWidgets_[selectedColumn_][selectedRow_]->setSpec(displayConfig_.columns[selectedColumn_][selectedRow_]);
    plotWidgets_[selectedColumn_][selectedRow_]->resetScale();
    updateTopInfoLabels();
    setStatus(QString("Updated panel setup: col %1 row %2").arg(selectedColumn_ + 1).arg(selectedRow_ + 1));
}

void MainWindow::dataSourceSetupForCurrentPanel()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0
        || selectedColumn_ >= config_.columns.size()
        || selectedRow_ >= config_.columns[selectedColumn_].size()
        || selectedColumn_ >= plotWidgets_.size()
        || selectedRow_ >= plotWidgets_[selectedColumn_].size()) {
        return;
    }

    PlotSpec& plot = config_.columns[selectedColumn_][selectedRow_];
    const QString currentShot = shotEdit_ ? shotEdit_->text().trimmed() : plot.shot;
    DataSourceDialog dialog(plot, currentShot, appSourceIndexDir(rootPath_), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<SignalSpec> specs = dialog.signalSpecs();
    if (specs.isEmpty()) {
        QMessageBox::warning(this, "Data Source Setup", "At least one signal is required.");
        return;
    }

    const bool dataSourceChanged = !signalDataSourcesEqual(plot.signalSpecs, specs);
    const bool specChanged = !signalSpecsEqual(plot.signalSpecs, specs);
    if (!specChanged) {
        setStatus(QString("Panel unchanged: col %1 row %2").arg(selectedColumn_ + 1).arg(selectedRow_ + 1));
        return;
    }
    plot.signalSpecs = std::move(specs);
    normalizePresetColors(plot.signalSpecs);
    plot.title = plot.signalSpecs.front().yExpr;
    if (dataSourceChanged) {
        plot.customXRange = false;
        plot.customYRange = false;
        plot.xmin = qQNaN();
        plot.xmax = qQNaN();
        plot.ymin = qQNaN();
        plot.ymax = qQNaN();
    }

    PlotWidget* widget = plotWidgets_[selectedColumn_][selectedRow_];
    const bool restoreView = !dataSourceChanged && widget->hasView();
    const QRectF previousView = restoreView ? widget->currentView() : QRectF();
    syncDisplayConfig();
    if (restoreView) {
        widget->applyView(previousView);
    }
    updateTopInfoLabels();
    if (dataSourceChanged) {
        refreshOne(selectedColumn_, selectedRow_, -1);
    } else {
        setStatus(QString("Updated panel style: col %1 row %2").arg(selectedColumn_ + 1).arg(selectedRow_ + 1));
    }
}

void MainWindow::maximizeCurrentPanel()
{
    if (!currentPlotWidget()) {
        return;
    }
    singlePanelMaximized_ = true;
    maximizedColumn_ = selectedColumn_;
    maximizedRow_ = selectedRow_;
    for (int c = 0; c < plotWidgets_.size(); ++c) {
        QWidget* columnHost = plotWidgets_[c].isEmpty() ? nullptr : plotWidgets_[c].first()->parentWidget();
        if (columnHost) {
            columnHost->setVisible(c == maximizedColumn_);
            columnHost->setSizePolicy(c == maximizedColumn_ ? QSizePolicy::Expanding : QSizePolicy::Ignored,
                                      QSizePolicy::Expanding);
            if (auto* columnLayout = qobject_cast<QBoxLayout*>(columnHost->layout())) {
                for (int r = 0; r < plotWidgets_[c].size(); ++r) {
                    columnLayout->setStretch(r, c == maximizedColumn_ && r == maximizedRow_ ? 1 : 0);
                }
            }
        }
        gridLayout_->setColumnStretch(c, c == maximizedColumn_ ? 1 : 0);
        for (int r = 0; r < plotWidgets_[c].size(); ++r) {
            const bool visible = c == maximizedColumn_ && r == maximizedRow_;
            plotWidgets_[c][r]->setVisible(visible);
            plotWidgets_[c][r]->setLargeDisplayMode(visible);
        }
    }
    if (scrollArea_ && scrollArea_->viewport()) {
        gridHost_->setMinimumSize(scrollArea_->viewport()->size());
    }
    gridHost_->updateGeometry();
    setStatus(QString("Max panel col %1 row %2").arg(maximizedColumn_ + 1).arg(maximizedRow_ + 1));
}

void MainWindow::showAllPanels()
{
    singlePanelMaximized_ = false;
    maximizedColumn_ = -1;
    maximizedRow_ = -1;
    for (int c = 0; c < plotWidgets_.size(); ++c) {
        auto& col = plotWidgets_[c];
        QWidget* columnHost = col.isEmpty() ? nullptr : col.first()->parentWidget();
        if (columnHost) {
            columnHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            columnHost->show();
            if (auto* columnLayout = qobject_cast<QBoxLayout*>(columnHost->layout())) {
                for (int r = 0; r < col.size(); ++r) {
                    columnLayout->setStretch(r, 1);
                }
            }
        }
        gridLayout_->setColumnStretch(c, 1);
        for (PlotWidget* plot : col) {
            if (plot) {
                plot->setLargeDisplayMode(false);
                plot->show();
            }
        }
    }
    gridHost_->setMinimumSize(QSize(0, 0));
    gridHost_->updateGeometry();
    setStatus("Show all panels");
}
