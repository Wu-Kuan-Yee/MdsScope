// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "helpers.hpp"

PlotWidget::PlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(120, 60);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
}

void PlotWidget::setSpec(PlotSpec spec)
{
    spec_ = std::move(spec);
    rebuildSeriesColorCache();
    rebuildLegendCache();
    series_.resize(spec_.signalSpecs.size());
    dataBoundsDirty_ = true;
    invalidatePlotCache();
    syncedPoint_ = {};
    hoverText_.clear();
    hoverSeriesIndex_ = -1;
    hoverSeriesLocked_ = false;
    pointTrackingActive_ = false;
    ++pointHoverGeneration_;
    pointHoverQueued_ = false;
    hasView_ = false;
    view_ = {};
    update();
}

QVector<SignalSeries> PlotWidget::seriesSnapshot() const
{
    return series_;
}

void PlotWidget::setSeries(int index, SignalSeries series)
{
    if (index >= series_.size()) {
        series_.resize(index + 1);
    }
    const int pointCount = series.pointCount();
    const int expectedBlocks = (pointCount + kMinMaxBlockSize - 1) / kMinMaxBlockSize;
    const bool indexValid = pointCount < kMinMaxBlockSize * 4
                            ? series.minMaxBlockSize == 0
                            : series.minMaxBlockSize == kMinMaxBlockSize
                                  && series.minYBlocks.size() == expectedBlocks
                                  && series.maxYBlocks.size() == expectedBlocks;
    if (!indexValid) {
        rebuildMinMaxIndex(series);
    }
    series_[index] = std::move(series);
    dataBoundsDirty_ = true;
    invalidatePlotCache();
    scheduleUpdate();
}

void PlotWidget::clearSeries()
{
    for (auto& s : series_) {
        s = {};
    }
    dataBoundsDirty_ = true;
    invalidatePlotCache();
    clearSyncedPoint();
    hasView_ = false;
    hoverText_.clear();
    hoverSeriesIndex_ = -1;
    hoverSeriesLocked_ = false;
    pointTrackingActive_ = false;
    ++pointHoverGeneration_;
    pointHoverQueued_ = false;
    scheduleUpdate();
}

void PlotWidget::scheduleUpdate()
{
    if (!isVisible()) {
        return;
    }
    if (updateQueued_) {
        return;
    }
    updateQueued_ = true;
    QTimer::singleShot(16, this, [this] {
        updateQueued_ = false;
        update();
    });
}

void PlotWidget::schedulePointHoverUpdate(const QPointF& pixelPos)
{
    pendingPointHoverPos_ = pixelPos;
    if (pointHoverQueued_) {
        return;
    }
    pointHoverQueued_ = true;
    const int generation = pointHoverGeneration_;
    QTimer::singleShot(16, this, [this, generation] {
        if (generation != pointHoverGeneration_) {
            return;
        }
        pointHoverQueued_ = false;
        if (interactionMode_ != InteractionMode::Point || !pointTrackingActive_ || !hoverSeriesLocked_) {
            return;
        }
        const bool changed = updateHover(pendingPointHoverPos_);
        if (changed && !hoverText_.isEmpty()) {
            emit pointXChanged(hoverData_.x());
        }
    });
}

void PlotWidget::deactivatePointTracking()
{
    pointTrackingActive_ = false;
    ++pointHoverGeneration_;
    pointHoverQueued_ = false;
    hoverText_.clear();
    hoverSeriesIndex_ = -1;
    hoverSeriesLocked_ = false;
}

void PlotWidget::setSelected(bool selected)
{
    if (selected_ == selected) {
        return;
    }
    selected_ = selected;
    const QRect dirty = selectionBorderDirtyRect();
    if (dirty.isValid() && !dirty.isEmpty()) {
        update(dirty);
    }
}

void PlotWidget::setLargeDisplayMode(bool enabled)
{
    if (largeDisplayMode_ == enabled) {
        return;
    }
    largeDisplayMode_ = enabled;
    invalidatePlotCache();
    update();
}

void PlotWidget::refreshStyle()
{
    invalidatePlotCache();
    update();
}

void PlotWidget::setInteractionMode(InteractionMode mode)
{
    if (interactionMode_ == mode && !dragging_ && !zooming_) {
        return;
    }
    const QRect oldZoomDirty = zoomRubberBandDirtyRect(zoomRubberBand_);
    interactionMode_ = mode;
    dragging_ = false;
    zooming_ = false;
    zoomRubberBand_ = {};
    if (interactionMode_ != InteractionMode::Point) {
        hoverText_.clear();
        hoverSeriesIndex_ = -1;
        hoverSeriesLocked_ = false;
        pointTrackingActive_ = false;
        ++pointHoverGeneration_;
        pointHoverQueued_ = false;
        clearSyncedPoint();
    }
    if (oldZoomDirty.isValid() && !oldZoomDirty.isEmpty()) {
        update(oldZoomDirty);
    }
}

QRectF PlotWidget::currentView() const
{
    return effectiveView();
}

QColor PlotWidget::seriesColor(int index) const
{
    if (index >= 0 && index < seriesColors_.size()) {
        return seriesColors_[index];
    }
    return QColor(colorForIndex(index));
}

void PlotWidget::rebuildSeriesColorCache()
{
    seriesColors_.clear();
    seriesColors_.reserve(spec_.signalSpecs.size());
    for (int i = 0; i < spec_.signalSpecs.size(); ++i) {
        const QString colorName = spec_.signalSpecs[i].colorName;
        const QColor color(colorName.isEmpty() ? colorForIndex(i) : colorName);
        seriesColors_.push_back(color.isValid() ? color : QColor(colorForIndex(i)));
    }
}

void PlotWidget::rebuildLegendCache()
{
    legendLabels_.clear();
    legendSeriesIndexes_.clear();
    legendLabels_.reserve(spec_.signalSpecs.size());
    legendSeriesIndexes_.reserve(spec_.signalSpecs.size());
    for (int i = 0; i < spec_.signalSpecs.size(); ++i) {
        const SignalSpec& sig = spec_.signalSpecs[i];
        if (sig.hidden) {
            continue;
        }
        QString label = normalizedMdsSignal(sig.yExpr);
        if (label.startsWith('\\')) {
            label.remove(0, 1);
        }
        const QString shot = effectiveSignalShot(spec_, sig);
        if (!shot.isEmpty()) {
            label += " " + shot;
        }
        if (label.isEmpty()) {
            continue;
        }
        legendLabels_.push_back(label);
        legendSeriesIndexes_.push_back(i);
    }
}

void PlotWidget::invalidatePlotCache()
{
    baseCacheDirty_ = true;
}
