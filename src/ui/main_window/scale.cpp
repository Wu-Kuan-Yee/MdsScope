// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.h"
#include "shared.h"


void MainWindow::applyScaleToAll()
{
    PlotWidget* current = currentPlotWidget();
    if (!current) {
        return;
    }
    const QRectF view = current->currentView();
    if (!view.isValid() || view.width() <= 0.0) {
        return;
    }
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot == current) {
                continue;
            }
            plot->applyXRangeAutoY(view.left(), view.right());
        }
    }
    setStatus("Applied current X scale to other panels");
}

void MainWindow::applyYScaleToAll()
{
    PlotWidget* current = currentPlotWidget();
    if (!current) {
        return;
    }
    const QRectF view = current->currentView();
    if (!view.isValid() || view.height() <= 0.0) {
        return;
    }
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot == current) {
                continue;
            }
            plot->applyYRangeKeepX(view.top(), view.bottom());
        }
    }
    setStatus("Applied current Y scale to other panels");
}

void MainWindow::resetCurrentScale()
{
    PlotWidget* current = currentPlotWidget();
    if (!current) {
        return;
    }
    if (selectedColumn_ >= 0 && selectedRow_ >= 0
        && selectedColumn_ < config_.columns.size()
        && selectedRow_ < config_.columns[selectedColumn_].size()) {
        clearCustomRanges(&config_.columns[selectedColumn_][selectedRow_]);
        displayConfig_ = expandedShotLayout(config_);
        if (selectedColumn_ < displayConfig_.columns.size()
            && selectedRow_ < displayConfig_.columns[selectedColumn_].size()) {
            current->setSpec(displayConfig_.columns[selectedColumn_][selectedRow_]);
        }
        updateTopInfoLabels();
        current = currentPlotWidget();
        if (!current) {
            return;
        }
    }
    current->resetScale();
    setStatus("Reset current panel to auto scale");
}

void MainWindow::resetScales()
{
    const bool updatesEnabled = gridHost_ ? gridHost_->updatesEnabled() : true;
    if (gridHost_ && updatesEnabled) {
        gridHost_->setUpdatesEnabled(false);
    }
    for (auto& col : config_.columns) {
        for (PlotSpec& plot : col) {
            clearCustomRanges(&plot);
        }
    }
    syncDisplayConfig();
    updateTopInfoLabels();
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot) {
                plot->resetScale(false);
            }
        }
    }
    if (gridHost_ && updatesEnabled) {
        gridHost_->setUpdatesEnabled(true);
    }
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot && plot->isVisible()) {
                plot->update();
            }
        }
    }
    setStatus("Reset all panels to auto scale");
}
