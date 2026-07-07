// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.h"
#include "point_overlay.h"

namespace {
constexpr int kMinMaxBlockSize = 256;

bool signalVisible(const PlotSpec& spec, int index)
{
    return index < 0 || index >= spec.signalSpecs.size() || !spec.signalSpecs[index].hidden;
}

void rebuildMinMaxIndex(SignalSeries& series)
{
    const int n = series.hasUniformData() ? series.uniformY.size() : series.points.size();
    series.minYBlocks.clear();
    series.maxYBlocks.clear();
    series.minMaxBlockSize = 0;
    if (n < kMinMaxBlockSize * 4) {
        return;
    }

    const int blockCount = (n + kMinMaxBlockSize - 1) / kMinMaxBlockSize;
    series.minYBlocks.reserve(blockCount);
    series.maxYBlocks.reserve(blockCount);
    for (int block = 0; block < blockCount; ++block) {
        const int begin = block * kMinMaxBlockSize;
        const int end = std::min(n, begin + kMinMaxBlockSize);
        float minY = std::numeric_limits<float>::infinity();
        float maxY = -std::numeric_limits<float>::infinity();
        for (int i = begin; i < end; ++i) {
            const float y = series.hasUniformData() ? series.uniformY[i] : static_cast<float>(series.points[i].y());
            if (!std::isfinite(y)) {
                continue;
            }
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
        series.minYBlocks.push_back(minY);
        series.maxYBlocks.push_back(maxY);
    }
    series.minMaxBlockSize = kMinMaxBlockSize;
}

template <typename ValueAt>
void considerIndexedYRange(const SignalSeries& series,
                           int startIndex,
                           int endIndex,
                           ValueAt valueAt,
                           double* minY,
                           double* maxY)
{
    if (!minY || !maxY || startIndex > endIndex) {
        return;
    }
    auto consider = [&](double y) {
        if (!std::isfinite(y)) {
            return;
        }
        *minY = std::min(*minY, y);
        *maxY = std::max(*maxY, y);
    };

    const int blockSize = series.minMaxBlockSize;
    if (blockSize <= 0 || series.minYBlocks.isEmpty() || series.maxYBlocks.isEmpty()) {
        for (int i = startIndex; i <= endIndex; ++i) {
            consider(valueAt(i));
        }
        return;
    }

    while (startIndex <= endIndex && startIndex % blockSize != 0) {
        consider(valueAt(startIndex++));
    }
    while (startIndex + blockSize - 1 <= endIndex) {
        const int block = startIndex / blockSize;
        if (block >= 0 && block < series.minYBlocks.size()) {
            consider(series.minYBlocks[block]);
            consider(series.maxYBlocks[block]);
        }
        startIndex += blockSize;
    }
    while (startIndex <= endIndex) {
        consider(valueAt(startIndex++));
    }
}

QVector<QPointF> displayUniformPointsUsingMinMaxIndex(const SignalSeries& series,
                                                      int startIndex,
                                                      int endIndex,
                                                      double pixelWidth)
{
    const int visibleCount = endIndex - startIndex + 1;
    const int targetPoints = std::max(2, static_cast<int>(pixelWidth * 2.0));
    const int buckets = std::max(1, targetPoints / 2);
    QVector<QPointF> out;
    out.reserve(std::min(visibleCount, buckets * 2));

    auto valueAt = [&](int index) {
        return static_cast<double>(series.uniformY[index]);
    };

    for (int b = 0; b < buckets; ++b) {
        const int bucketStart = startIndex + static_cast<int>((static_cast<qint64>(b) * visibleCount) / buckets);
        const int bucketEnd = startIndex + static_cast<int>((static_cast<qint64>(b + 1) * visibleCount) / buckets) - 1;
        if (bucketEnd < bucketStart) {
            continue;
        }

        double minY = std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();
        considerIndexedYRange(series, bucketStart, bucketEnd, valueAt, &minY, &maxY);
        if (!std::isfinite(minY) || !std::isfinite(maxY)) {
            continue;
        }

        const double x = series.uniformStart
                         + (static_cast<double>(bucketStart + bucketEnd) * 0.5) * series.uniformStep;
        if (minY == maxY) {
            out.push_back(QPointF(x, minY));
        } else {
            out.push_back(QPointF(x, minY));
            out.push_back(QPointF(x, maxY));
        }
    }
    return out;
}

QVector<QPointF> displayPointSeriesUsingMinMaxIndex(const SignalSeries& series,
                                                    int startIndex,
                                                    int endIndex,
                                                    double pixelWidth)
{
    const int visibleCount = endIndex - startIndex + 1;
    const int targetPoints = std::max(2, static_cast<int>(pixelWidth * 2.0));
    const int buckets = std::max(1, targetPoints / 2);
    QVector<QPointF> out;
    out.reserve(std::min(visibleCount, buckets * 2));

    auto valueAt = [&](int index) {
        return series.points[index].y();
    };

    for (int b = 0; b < buckets; ++b) {
        const int bucketStart = startIndex + static_cast<int>((static_cast<qint64>(b) * visibleCount) / buckets);
        const int bucketEnd = startIndex + static_cast<int>((static_cast<qint64>(b + 1) * visibleCount) / buckets) - 1;
        if (bucketEnd < bucketStart) {
            continue;
        }

        double minY = std::numeric_limits<double>::infinity();
        double maxY = -std::numeric_limits<double>::infinity();
        considerIndexedYRange(series, bucketStart, bucketEnd, valueAt, &minY, &maxY);
        if (!std::isfinite(minY) || !std::isfinite(maxY)) {
            continue;
        }

        const double x = (series.points[bucketStart].x() + series.points[bucketEnd].x()) * 0.5;
        if (minY == maxY) {
            out.push_back(QPointF(x, minY));
        } else {
            out.push_back(QPointF(x, minY));
            out.push_back(QPointF(x, maxY));
        }
    }
    return out;
}
}

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
    pointHoverQueued_ = false;
    clearPointOverlay();
    hasView_ = false;
    view_ = {};
    update();
}

void PlotWidget::setSeries(int index, SignalSeries series)
{
    if (index >= series_.size()) {
        series_.resize(index + 1);
    }
    rebuildMinMaxIndex(series);
    series_[index] = std::move(series);
    dataBoundsDirty_ = true;
    invalidatePlotCache();
    if (!hasView_) {
        view_ = dataBounds();
        expandFlatRange(view_);
    }
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
    pointHoverQueued_ = false;
    clearPointOverlay();
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
    QTimer::singleShot(16, this, [this] {
        pointHoverQueued_ = false;
        if (interactionMode_ != InteractionMode::Point || !pointTrackingActive_ || !hoverSeriesLocked_) {
            return;
        }
        const bool changed = updateHover(pendingPointHoverPos_);
        if (changed) {
            updatePointOverlay();
        }
        if (changed && !hoverText_.isEmpty()) {
            emit pointXChanged(hoverData_.x());
        }
    });
}

void PlotWidget::updatePointOverlay()
{
    clearPointOverlay();
}

void PlotWidget::clearPointOverlay()
{
    if (pointOverlay_) {
        pointOverlay_->clearPoint();
    }
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
        pointHoverQueued_ = false;
        clearPointOverlay();
        clearSyncedPoint();
    }
    if (oldZoomDirty.isValid() && !oldZoomDirty.isEmpty()) {
        update(oldZoomDirty);
    }
}

void PlotWidget::resetScale(bool repaint)
{
    hasView_ = false;
    view_ = {};
    invalidatePlotCache();
    updatePointOverlay();
    if (repaint) {
        update();
    }
}

void PlotWidget::applyView(const QRectF& view)
{
    if (!view.isValid() || view.width() <= 0 || view.height() <= 0) {
        return;
    }
    view_ = view;
    hasView_ = true;
    invalidatePlotCache();
    update();
}

void PlotWidget::applyXRangeAutoY(double xmin, double xmax)
{
    if (!std::isfinite(xmin) || !std::isfinite(xmax) || xmin == xmax) {
        return;
    }
    if (xmax < xmin) {
        std::swap(xmin, xmax);
    }

    double minY = std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (const SignalSeries& s : series_) {
        auto considerY = [&](double y) {
            if (!std::isfinite(y)) {
                return;
            }
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        };

        if (s.hasUniformData() && s.uniformStep != 0.0) {
            const int n = s.uniformY.size();
            if (n <= 0) {
                continue;
            }
            const double firstX = s.uniformStart;
            const double lastX = s.uniformStart + static_cast<double>(n - 1) * s.uniformStep;
            const double dataMinX = std::min(firstX, lastX);
            const double dataMaxX = std::max(firstX, lastX);
            if (xmin <= dataMinX && xmax >= dataMaxX
                && std::isfinite(s.uniformMinY) && std::isfinite(s.uniformMaxY)) {
                considerY(s.uniformMinY);
                considerY(s.uniformMaxY);
                continue;
            }
            int startIndex = static_cast<int>(std::floor((xmin - s.uniformStart) / s.uniformStep));
            int endIndex = static_cast<int>(std::ceil((xmax - s.uniformStart) / s.uniformStep));
            if (s.uniformStep < 0) {
                startIndex = static_cast<int>(std::floor((xmax - s.uniformStart) / s.uniformStep));
                endIndex = static_cast<int>(std::ceil((xmin - s.uniformStart) / s.uniformStep));
            }
            startIndex = std::clamp(startIndex, 0, n - 1);
            endIndex = std::clamp(endIndex, 0, n - 1);
            if (endIndex < startIndex) {
                std::swap(startIndex, endIndex);
            }
            considerIndexedYRange(s, startIndex, endIndex, [&](int i) {
                return static_cast<double>(s.uniformY[i]);
            }, &minY, &maxY);
        } else {
            if (s.points.isEmpty()) {
                continue;
            }
            auto lowerByX = [](const QPointF& point, double x) {
                return point.x() < x;
            };
            const auto beginIt = std::lower_bound(s.points.cbegin(), s.points.cend(), xmin, lowerByX);
            const auto endIt = std::upper_bound(s.points.cbegin(), s.points.cend(), xmax, [](double x, const QPointF& point) {
                return x < point.x();
            });
            if (beginIt != endIt) {
                const int startIndex = static_cast<int>(std::distance(s.points.cbegin(), beginIt));
                const int endIndex = static_cast<int>(std::distance(s.points.cbegin(), endIt)) - 1;
                considerIndexedYRange(s, startIndex, endIndex, [&](int i) {
                    return s.points[i].y();
                }, &minY, &maxY);
            }
        }
    }

    if (!std::isfinite(minY) || !std::isfinite(maxY)) {
        QRectF bounds = dataBounds();
        minY = bounds.top();
        maxY = bounds.bottom();
    }
    const double yPad = (maxY - minY) * 0.04;
    if (yPad > 0.0) {
        minY -= yPad;
        maxY += yPad;
    }
    QRectF next(QPointF(xmin, minY), QPointF(xmax, maxY));
    expandFlatRange(next);
    view_ = next;
    hasView_ = true;
    invalidatePlotCache();
    update();
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

QRect PlotWidget::syncedPointDirtyRect(const PointReadout& readout) const
{
    if (!readout.visible || !readout.plotRect.contains(readout.pixel)) {
        return {};
    }
    QRect dirty(qRound(readout.pixel.x()) - 1,
                qRound(readout.plotRect.top()),
                3,
                std::max(1, qRound(readout.plotRect.height())));
    dirty = dirty.united(QRect(qRound(readout.plotRect.left()),
                               qRound(readout.pixel.y()) - 1,
                               std::max(1, qRound(readout.plotRect.width())),
                               3));
    if (readout.showText && !readout.text.isEmpty()) {
        QRect textRect(qRound(readout.pixel.x()) + 4, qRound(readout.pixel.y()) - 20, 188, 22);
        if (textRect.right() > qRound(readout.plotRect.right())) {
            textRect.moveRight(qRound(readout.pixel.x()) - 4);
        }
        if (textRect.left() < qRound(readout.plotRect.left())) {
            textRect.moveLeft(qRound(readout.plotRect.left()) + 2);
        }
        if (textRect.top() < qRound(readout.plotRect.top())) {
            textRect.moveTop(qRound(readout.pixel.y()) + 4);
        }
        dirty = dirty.united(textRect);
    }
    return dirty.adjusted(-2, -2, 2, 2).intersected(rect());
}

void PlotWidget::drawSyncedPoint(QPainter& painter) const
{
    if (!syncedPoint_.visible || !syncedPoint_.plotRect.contains(syncedPoint_.pixel)) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(syncedPoint_.color, 1));
    painter.drawLine(QPointF(syncedPoint_.pixel.x(), syncedPoint_.plotRect.top()),
                     QPointF(syncedPoint_.pixel.x(), syncedPoint_.plotRect.bottom()));
    painter.drawLine(QPointF(syncedPoint_.plotRect.left(), syncedPoint_.pixel.y()),
                     QPointF(syncedPoint_.plotRect.right(), syncedPoint_.pixel.y()));
    if (syncedPoint_.showText && !syncedPoint_.text.isEmpty()) {
        QRectF textRect(syncedPoint_.pixel.x() + 4, syncedPoint_.pixel.y() - 20, 188, 22);
        if (textRect.right() > syncedPoint_.plotRect.right()) {
            textRect.moveRight(syncedPoint_.pixel.x() - 4);
        }
        if (textRect.left() < syncedPoint_.plotRect.left()) {
            textRect.moveLeft(syncedPoint_.plotRect.left() + 2);
        }
        if (textRect.top() < syncedPoint_.plotRect.top()) {
            textRect.moveTop(syncedPoint_.pixel.y() + 4);
        }
        QFont pointFont = painter.font();
        pointFont.setPointSize(std::max(8, pointFont.pointSize() + 1));
        pointFont.setBold(false);
        painter.setFont(pointFont);
        painter.setPen(syncedPoint_.color);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, syncedPoint_.text);
    }
    painter.restore();
}

void PlotWidget::drawZoomRubberBand(QPainter& painter) const
{
    if (!zooming_ || !zoomRubberBand_.isValid()) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor("#1d4ed8"), 1, Qt::DashLine));
    painter.setBrush(QColor(37, 99, 235, 35));
    painter.drawRect(zoomRubberBand_.normalized());
    painter.restore();
}

void PlotWidget::drawSelectionBorder(QPainter& painter) const
{
    if (!selected_) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor("#ff00ff"), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    painter.restore();
}

QRect PlotWidget::selectionBorderDirtyRect() const
{
    if (rect().isEmpty()) {
        return {};
    }
    const QRect outer = rect();
    return outer.adjusted(0, 0, -1, -1);
}

QRect PlotWidget::zoomRubberBandDirtyRect(const QRectF& band) const
{
    if (!band.isValid() || band.isEmpty()) {
        return {};
    }
    return band.normalized().toAlignedRect().adjusted(-3, -3, 3, 3).intersected(rect());
}

QRectF PlotWidget::plotRect() const
{
    const FontSettings& fonts = fontSettings();
    QFont axisFont(fonts.family, fonts.axisSize + (largeDisplayMode_ ? 4 : 0));
    axisFont.setPointSize(std::max(7, axisFont.pointSize()));
    const QFontMetrics axisFm(axisFont);
    const int bottomMargin = std::max(18, axisFm.height() + 6);
    return rect().adjusted(2, 3, -3, -bottomMargin);
}

QRectF PlotWidget::dataBounds() const
{
    if (!dataBoundsDirty_ && cachedDataBounds_.isValid()) {
        return cachedDataBounds_;
    }
    double minX = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < series_.size(); ++i) {
        if (i < spec_.signalSpecs.size() && spec_.signalSpecs[i].hidden) {
            continue;
        }
        const auto& s = series_[i];
        if (s.hasUniformData()) {
            const double x0 = s.uniformStart;
            const double x1 = s.uniformStart + static_cast<double>(s.uniformY.size() - 1) * s.uniformStep;
            minX = std::min(minX, std::min(x0, x1));
            maxX = std::max(maxX, std::max(x0, x1));
            if (std::isfinite(s.uniformMinY) && std::isfinite(s.uniformMaxY)) {
                minY = std::min(minY, s.uniformMinY);
                maxY = std::max(maxY, s.uniformMaxY);
            }
        }
        for (const QPointF& p : s.points) {
            minX = std::min(minX, p.x());
            maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y());
            maxY = std::max(maxY, p.y());
        }
    }
    const bool fixedMinX = spec_.customXRange && std::isfinite(spec_.xmin);
    const bool fixedMaxX = spec_.customXRange && std::isfinite(spec_.xmax);
    const bool fixedMinY = spec_.customYRange && std::isfinite(spec_.ymin);
    const bool fixedMaxY = spec_.customYRange && std::isfinite(spec_.ymax);
    if (fixedMinX) minX = spec_.xmin;
    if (fixedMaxX) maxX = spec_.xmax;
    if (fixedMinY) minY = spec_.ymin;
    if (fixedMaxY) maxY = spec_.ymax;
    if (!std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minY) || !std::isfinite(maxY)) {
        cachedDataBounds_ = QRectF(0, -1, 1, 2);
        dataBoundsDirty_ = false;
        return cachedDataBounds_;
    }
    const double xPad = (maxX - minX) * 0.01;
    if (xPad > 0.0) {
        if (!fixedMinX) minX -= xPad;
        if (!fixedMaxX) maxX += xPad;
    }
    const double yPad = (maxY - minY) * 0.04;
    if (yPad > 0.0) {
        if (!fixedMinY) minY -= yPad;
        if (!fixedMaxY) maxY += yPad;
    }
    QRectF bounds(QPointF(minX, minY), QPointF(maxX, maxY));
    expandFlatRange(bounds);
    cachedDataBounds_ = bounds;
    dataBoundsDirty_ = false;
    return cachedDataBounds_;
}

QRectF PlotWidget::effectiveView() const
{
    if (hasView_ && view_.isValid()) {
        return view_;
    }
    return dataBounds();
}

void PlotWidget::expandFlatRange(QRectF& range) const
{
    if (range.width() <= 0) {
        range.setLeft(range.left() - 0.5);
        range.setRight(range.right() + 0.5);
    }
    if (range.height() <= 0) {
        range.setTop(range.top() - 0.5);
        range.setBottom(range.bottom() + 0.5);
    }
}

QPointF PlotWidget::dataToPixel(const QPointF& p, const QRectF& view, const QRectF& pr) const
{
    const double x = pr.left() + (p.x() - view.left()) / view.width() * pr.width();
    const double y = pr.bottom() - (p.y() - view.top()) / view.height() * pr.height();
    return QPointF(x, y);
}

QPointF PlotWidget::pixelToData(const QPointF& p, const QRectF& view, const QRectF& pr) const
{
    const double x = view.left() + (p.x() - pr.left()) / pr.width() * view.width();
    const double y = view.top() + (pr.bottom() - p.y()) / pr.height() * view.height();
    return QPointF(x, y);
}

QVector<QPointF> PlotWidget::displayPointsForSeries(const SignalSeries& series, const QRectF& view, double pixelWidth) const
{
    if (!series.hasUniformData()) {
        if (series.points.isEmpty()) {
            return {};
        }

        auto lowerByX = [](const QPointF& point, double x) {
            return point.x() < x;
        };
        const auto beginIt = std::lower_bound(series.points.cbegin(), series.points.cend(), view.left(), lowerByX);
        const auto endIt = std::upper_bound(series.points.cbegin(), series.points.cend(), view.right(), [](double x, const QPointF& point) {
            return x < point.x();
        });
        int startIndex = static_cast<int>(std::distance(series.points.cbegin(), beginIt));
        int endIndex = static_cast<int>(std::distance(series.points.cbegin(), endIt)) - 1;
        if (endIndex < startIndex) {
            const int nearest = std::clamp(startIndex, 0, static_cast<int>(series.points.size()) - 1);
            return QVector<QPointF>{series.points[nearest]};
        }
        if (startIndex > 0) {
            --startIndex;
        }
        if (endIndex + 1 < series.points.size()) {
            ++endIndex;
        }

        const int visibleCount = endIndex - startIndex + 1;
        const int targetPoints = std::max(2, static_cast<int>(pixelWidth * 2.0));
        if (series.minMaxBlockSize > 0
            && !series.minYBlocks.isEmpty()
            && visibleCount > targetPoints * 8
            && visibleCount > series.minMaxBlockSize * 8) {
            return displayPointSeriesUsingMinMaxIndex(series, startIndex, endIndex, pixelWidth);
        }
        if (visibleCount <= targetPoints) {
            QVector<QPointF> out;
            out.reserve(visibleCount);
            for (int i = startIndex; i <= endIndex; ++i) {
                out.push_back(series.points[i]);
            }
            return out;
        }

        const int buckets = std::max(1, targetPoints / 2);
        QVector<QPointF> out;
        out.reserve(std::min(visibleCount, buckets * 2));
        for (int b = 0; b < buckets; ++b) {
            const int bucketStart = startIndex + static_cast<int>((static_cast<qint64>(b) * visibleCount) / buckets);
            const int bucketEnd = startIndex + static_cast<int>((static_cast<qint64>(b + 1) * visibleCount) / buckets);
            if (bucketEnd <= bucketStart) {
                continue;
            }
            int minIndex = bucketStart;
            int maxIndex = bucketStart;
            for (int i = bucketStart + 1; i < bucketEnd; ++i) {
                if (series.points[i].y() < series.points[minIndex].y()) {
                    minIndex = i;
                }
                if (series.points[i].y() > series.points[maxIndex].y()) {
                    maxIndex = i;
                }
            }
            if (minIndex == maxIndex) {
                out.push_back(series.points[minIndex]);
            } else if (minIndex < maxIndex) {
                out.push_back(series.points[minIndex]);
                out.push_back(series.points[maxIndex]);
            } else {
                out.push_back(series.points[maxIndex]);
                out.push_back(series.points[minIndex]);
            }
        }
        return out;
    }
    const int n = series.uniformY.size();
    if (n <= 0 || !std::isfinite(series.uniformStep) || series.uniformStep == 0.0) {
        return {};
    }

    const double firstX = series.uniformStart;
    const double lastX = series.uniformStart + static_cast<double>(n - 1) * series.uniformStep;
    const double minDataX = std::min(firstX, lastX);
    const double maxDataX = std::max(firstX, lastX);
    if (view.right() < minDataX || view.left() > maxDataX) {
        return {};
    }

    int startIndex = static_cast<int>(std::floor((view.left() - series.uniformStart) / series.uniformStep));
    int endIndex = static_cast<int>(std::ceil((view.right() - series.uniformStart) / series.uniformStep));
    if (series.uniformStep < 0) {
        startIndex = static_cast<int>(std::floor((view.right() - series.uniformStart) / series.uniformStep));
        endIndex = static_cast<int>(std::ceil((view.left() - series.uniformStart) / series.uniformStep));
    }
    startIndex = std::clamp(startIndex, 0, n - 1);
    endIndex = std::clamp(endIndex, 0, n - 1);
    if (endIndex < startIndex) {
        std::swap(startIndex, endIndex);
    }

    const int visibleCount = endIndex - startIndex + 1;
    const int targetPoints = std::max(2, static_cast<int>(pixelWidth * 2.0));
    if (series.minMaxBlockSize > 0
        && !series.minYBlocks.isEmpty()
        && visibleCount > targetPoints * 8
        && visibleCount > series.minMaxBlockSize * 8) {
        return displayUniformPointsUsingMinMaxIndex(series, startIndex, endIndex, pixelWidth);
    }

    if (visibleCount <= targetPoints) {
        QVector<QPointF> out;
        out.reserve(visibleCount);
        for (int i = startIndex; i <= endIndex; ++i) {
            out.push_back(series.pointAt(i));
        }
        return out;
    }

    const int buckets = std::max(1, targetPoints / 2);
    QVector<QPointF> out;
    out.reserve(std::min(visibleCount, buckets * 2));
    for (int b = 0; b < buckets; ++b) {
        const int bucketStart = startIndex + static_cast<int>((static_cast<qint64>(b) * visibleCount) / buckets);
        const int bucketEnd = startIndex + static_cast<int>((static_cast<qint64>(b + 1) * visibleCount) / buckets);
        if (bucketEnd <= bucketStart) {
            continue;
        }
        int minIndex = bucketStart;
        int maxIndex = bucketStart;
        for (int i = bucketStart + 1; i < bucketEnd; ++i) {
            if (series.uniformY[i] < series.uniformY[minIndex]) {
                minIndex = i;
            }
            if (series.uniformY[i] > series.uniformY[maxIndex]) {
                maxIndex = i;
            }
        }
        if (minIndex == maxIndex) {
            out.push_back(series.pointAt(minIndex));
        } else if (minIndex < maxIndex) {
            out.push_back(series.pointAt(minIndex));
            out.push_back(series.pointAt(maxIndex));
        } else {
            out.push_back(series.pointAt(maxIndex));
            out.push_back(series.pointAt(minIndex));
        }
    }
    return out;
}

void PlotWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setClipRegion(event->region());
    const qreal dpr = devicePixelRatioF();
    const QSize pixmapSize(qCeil(width() * dpr), qCeil(height() * dpr));
    if (baseCacheDirty_ || baseCache_.isNull() || baseCacheSize_ != size() || baseCache_.size() != pixmapSize) {
        baseCache_ = QPixmap(pixmapSize);
        baseCache_.setDevicePixelRatio(dpr);
        baseCache_.fill(Qt::transparent);
        QPainter cachePainter(&baseCache_);
        renderBasePlot(cachePainter);
        baseCacheSize_ = size();
        baseCacheDirty_ = false;
    }
    painter.drawPixmap(0, 0, baseCache_);
    drawSelectionBorder(painter);
    drawSyncedPoint(painter);
    drawZoomRubberBand(painter);
}

void PlotWidget::renderBasePlot(QPainter& painter) const
{
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QPalette pal = palette();
    const QColor background = pal.color(QPalette::Base);
    const QColor frame = pal.color(QPalette::Mid);
    const QColor plotFrame = pal.color(QPalette::Midlight);
    const QColor textColor = pal.color(QPalette::Text);
    const QColor subtleText = pal.color(QPalette::Disabled, QPalette::Text);
    const QColor gridColor = pal.color(QPalette::AlternateBase).isValid()
                                 ? pal.color(QPalette::AlternateBase)
                                 : pal.color(QPalette::Midlight);
    const FontSettings& fonts = fontSettings();
    QFont axisFont(fonts.family, fonts.axisSize);
    int plotPointSize = fonts.axisSize;
    if (largeDisplayMode_) {
        plotPointSize += 4;
    }
    axisFont.setPointSize(std::max(7, plotPointSize));
    painter.setFont(axisFont);
    painter.fillRect(rect(), background);
    painter.setPen(QPen(frame, 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    const QRectF pr = plotRect();
    const QRectF view = effectiveView();
    painter.setPen(plotFrame);
    painter.drawRect(pr);
    if (spec_.grid) {
        painter.setPen(QPen(gridColor, 1));
        for (int i = 1; i < 5; ++i) {
            const double x = pr.left() + pr.width() * i / 5.0;
            const double y = pr.top() + pr.height() * i / 5.0;
            painter.drawLine(QPointF(x, pr.top()), QPointF(x, pr.bottom()));
            painter.drawLine(QPointF(pr.left(), y), QPointF(pr.right(), y));
        }
    }

    painter.setClipRect(pr.adjusted(1, 1, -1, -1));
    QVector<QPointF> renderedPixels;
    renderedPixels.reserve(4096);
    for (int i = 0; i < series_.size(); ++i) {
        if (i < spec_.signalSpecs.size() && spec_.signalSpecs[i].hidden) {
            continue;
        }
        const auto& s = series_[i];
        const QVector<QPointF> displayPoints = displayPointsForSeries(s, view, pr.width());
        if (displayPoints.size() < 2) {
            continue;
        }
        QVector<QPoint> polyline;
        polyline.reserve(displayPoints.size() + 1);
        QPointF pixel = dataToPixel(displayPoints.front(), view, pr);
        polyline.push_back(QPoint(qRound(pixel.x()), qRound(pixel.y())));
        if (renderedPixels.size() < 4096) {
            renderedPixels.push_back(pixel);
        }
        const int pointCount = static_cast<int>(displayPoints.size());
        const int stride = std::max(1, pointCount / std::max(1, static_cast<int>(pr.width() * 2)));
        for (int p = stride; p < displayPoints.size(); p += stride) {
            pixel = dataToPixel(displayPoints[p], view, pr);
            polyline.push_back(QPoint(qRound(pixel.x()), qRound(pixel.y())));
            if (renderedPixels.size() < 4096) {
                renderedPixels.push_back(pixel);
            }
        }
        pixel = dataToPixel(displayPoints.back(), view, pr);
        polyline.push_back(QPoint(qRound(pixel.x()), qRound(pixel.y())));
        if (renderedPixels.size() < 4096) {
            renderedPixels.push_back(pixel);
        }
        painter.setPen(QPen(seriesColor(i), 1));
        painter.drawPolyline(polyline.constData(), polyline.size());
    }
    painter.setClipping(false);

    painter.setPen(textColor);
    const QString titleText = spec_.title.trimmed();
    if (!titleText.isEmpty()) {
        QFont titleFont(fonts.family, fonts.legendSize + (largeDisplayMode_ ? 4 : 0));
        titleFont.setBold(true);
        painter.setFont(titleFont);
        const QFontMetrics titleFm(titleFont);
        const QString elidedTitle = titleFm.elidedText(titleText, Qt::ElideRight, std::max(20, static_cast<int>(pr.width() - 16)));
        painter.drawText(QRectF(pr.left() + 8, pr.top() + 2, pr.width() - 16, std::max(14, titleFm.height())),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         elidedTitle);
        painter.setFont(axisFont);
    }

    const int yTickCount = std::clamp(static_cast<int>(pr.height() / 34.0) + 1, 3, 6);
    const int xTickCount = std::clamp(static_cast<int>(pr.width() / 78.0) + 1, 3, 7);
    const QString yUnit = spec_.yLabel.trimmed().isEmpty() ? QStringLiteral("a.u.") : spec_.yLabel.trimmed();
    const QFontMetrics axisFm(axisFont);
    const double yLabelLeft = pr.left() + 4.0;
    QVector<double> yValues;
    yValues.reserve(yTickCount);
    for (int i = 0; i < yTickCount; ++i) {
        const double ratio = yTickCount == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(yTickCount - 1);
        const double yValue = view.bottom() - view.height() * ratio;
        yValues.push_back(yValue);
    }
    const QStringList yLabels = uniformAxisValues(yValues);
    int maxYLabelAdvance = 0;
    for (const QString& label : std::as_const(yLabels)) {
        maxYLabelAdvance = std::max(maxYLabelAdvance, axisFm.horizontalAdvance(label));
    }
    const int yLabelWidth = std::clamp(maxYLabelAdvance + 6,
                                       42,
                                       largeDisplayMode_ ? 110 : 78);
    const double yUnitLeft = pr.left() + 1.0;
    const int axisLabelHeight = std::max(14, axisFm.height() + 2);
    const double halfAxisLabelHeight = axisLabelHeight * 0.5;
    QVector<QRectF> yLabelRects;
    yLabelRects.reserve(yTickCount);
    QVector<QRectF> axisLabelRects;
    axisLabelRects.reserve(yTickCount + xTickCount + 1);
    for (int i = 0; i < yTickCount; ++i) {
        const double ratio = yTickCount == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(yTickCount - 1);
        const double yPixel = pr.top() + pr.height() * ratio;
        painter.drawLine(QPointF(pr.left(), yPixel), QPointF(pr.left() + 3, yPixel));
        double labelTop = yPixel - halfAxisLabelHeight;
        const double minLabelTop = pr.top() + 1.0;
        const double maxLabelTop = pr.bottom() - axisLabelHeight - 1.0;
        if (maxLabelTop >= minLabelTop) {
            labelTop = std::clamp(labelTop, minLabelTop, maxLabelTop);
        } else {
            labelTop = pr.center().y() - halfAxisLabelHeight;
        }
        const QRectF labelRect(yLabelLeft, labelTop, yLabelWidth, axisLabelHeight);
        yLabelRects.push_back(labelRect);
        axisLabelRects.push_back(labelRect);
        painter.drawText(labelRect,
                         Qt::AlignLeft | Qt::AlignVCenter,
                         yLabels[i]);
    }
    if (!yUnit.isEmpty()) {
        QFont unitFont(fonts.family, fonts.unitSize + (largeDisplayMode_ ? 4 : 0));
        painter.setFont(unitFont);
        const QFontMetrics unitFm(unitFont);
        QString yUnitText = yUnit;
        const int maxUnitAdvance = std::max(14, static_cast<int>(pr.height() - 4.0));
        if (unitFm.horizontalAdvance(yUnitText) + 6 > maxUnitAdvance) {
            yUnitText = unitFm.elidedText(yUnitText, Qt::ElideRight, maxUnitAdvance - 6);
        }
        const double unitAdvance = std::max(8, unitFm.horizontalAdvance(yUnitText)) + 6.0;
        const double unitVisualWidth = std::max(9.0, static_cast<double>(unitFm.height()));
        const double unitX = yUnitLeft + unitVisualWidth / 2.0;
        const double unitHalfHeight = unitAdvance / 2.0;
        const double unitHalfWidth = unitVisualWidth / 2.0;
        double unitY = pr.center().y();
        double bestGapDistance = std::numeric_limits<double>::infinity();
        for (int i = 0; i + 1 < yLabelRects.size(); ++i) {
            const double gapTop = yLabelRects[i].bottom();
            const double gapBottom = yLabelRects[i + 1].top();
            if (gapBottom <= gapTop) {
                continue;
            }
            const double candidateY = (gapTop + gapBottom) * 0.5;
            const double distance = std::abs(candidateY - pr.center().y());
            if (distance < bestGapDistance) {
                bestGapDistance = distance;
                unitY = candidateY;
            }
        }
        const double minUnitY = pr.top() + unitHalfHeight + 1.0;
        const double maxUnitY = pr.bottom() - unitHalfHeight - 1.0;
        if (maxUnitY >= minUnitY) {
            unitY = std::clamp(unitY, minUnitY, maxUnitY);
        }
        painter.save();
        const QPointF unitCenter(unitX, unitY);
        painter.translate(unitCenter);
        painter.rotate(-90.0);
        const QRectF unitRect(-unitAdvance / 2.0, -unitVisualWidth / 2.0, unitAdvance, unitVisualWidth);
        painter.drawText(unitRect, Qt::AlignCenter, yUnitText);
        painter.restore();
        axisLabelRects.push_back(QRectF(unitX - unitHalfWidth, unitY - unitHalfHeight, unitVisualWidth, unitAdvance));
        painter.setFont(axisFont);
    }

    painter.setPen(textColor);
    QVector<double> xTickPixels;
    xTickPixels.reserve(xTickCount);
    QVector<double> xValues;
    xValues.reserve(xTickCount);
    for (int i = 0; i < xTickCount; ++i) {
        const double ratio = xTickCount == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(xTickCount - 1);
        const double xValue = view.left() + view.width() * ratio;
        xValues.push_back(xValue);
        const double xPixel = pr.left() + pr.width() * ratio;
        xTickPixels.push_back(xPixel);
    }
    const QStringList xLabels = uniformAxisValues(xValues);
    for (int i = 0; i < xTickCount; ++i) {
        const double xPixel = xTickPixels[i];
        painter.drawLine(QPointF(xPixel, pr.bottom()), QPointF(xPixel, pr.bottom() + 2));
        QRectF textRect(xPixel - 38, pr.bottom() + 1, 76, axisLabelHeight);
        Qt::Alignment alignment = Qt::AlignHCenter | Qt::AlignVCenter;
        if (i == 0) {
            textRect.moveLeft(pr.left());
            alignment = Qt::AlignLeft | Qt::AlignVCenter;
        } else if (i == xTickCount - 1) {
            textRect.moveRight(pr.right());
            alignment = Qt::AlignRight | Qt::AlignVCenter;
        }
        axisLabelRects.push_back(textRect);
        painter.drawText(textRect, alignment, xLabels.value(i));
    }
    const QString xUnit = spec_.xLabel.trimmed().isEmpty() ? QStringLiteral("s") : spec_.xLabel.trimmed();
    double xUnitCenter = pr.center().x();
    if (xTickPixels.size() >= 2) {
        double bestDistance = std::numeric_limits<double>::infinity();
        for (int i = 0; i + 1 < xTickPixels.size(); ++i) {
            const double candidate = (xTickPixels[i] + xTickPixels[i + 1]) * 0.5;
            const double distance = std::abs(candidate - pr.center().x());
            if (distance < bestDistance) {
                bestDistance = distance;
                xUnitCenter = candidate;
            }
        }
    }
    const QRectF xUnitRect(xUnitCenter - 30, pr.bottom() - 14, 60, 12);
    axisLabelRects.push_back(xUnitRect);
    QFont unitFont(fonts.family, fonts.unitSize + (largeDisplayMode_ ? 4 : 0));
    painter.setFont(unitFont);
    painter.drawText(xUnitRect, Qt::AlignCenter, xUnit);

    QFont legendFont(fonts.family, fonts.legendSize + (largeDisplayMode_ ? 4 : 0));
    const QFontMetrics legendFm(legendFont);
    int legendWidth = 0;
    for (const QString& label : legendLabels_) {
        legendWidth = std::max(legendWidth, legendFm.horizontalAdvance(label));
    }
    if (!legendLabels_.isEmpty()) {
        legendWidth += 20;
        const int legendLineHeight = std::max(10, legendFm.height());
        const int legendHeight = static_cast<int>(legendLabels_.size()) * legendLineHeight + 4;
        const int pad = 1;
        const QRectF legendArea = pr.adjusted(2, 2, -2, -2);
        const double legendAreaWidth = std::max(1.0, legendArea.width());
        const double legendAreaHeight = std::max(1.0, legendArea.height());
        const double legendW = std::min<double>(legendWidth, legendAreaWidth);
        const double legendH = std::min<double>(legendHeight, legendAreaHeight);
        const QRectF legendRect(legendArea.right() - legendW - pad,
                                legendArea.top() + pad,
                                legendW,
                                legendH);
        painter.save();
        painter.setFont(legendFont);
        int legendX = static_cast<int>(legendRect.left()) + 2;
        int legendY = static_cast<int>(legendRect.top()) + 2;
        for (int i = 0; i < legendLabels_.size(); ++i) {
            if (legendY + legendLineHeight > pr.bottom()) {
                break;
            }
            const int seriesIndex = legendSeriesIndexes_.value(i, i);
            const QColor color = seriesColor(seriesIndex);
            const int squareSize = std::max(6, std::min(9, legendLineHeight - 3));
            const QRectF swatchRect(legendX,
                                    legendY + (legendLineHeight - squareSize) / 2.0,
                                    squareSize,
                                    squareSize);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawRect(swatchRect);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(color);
            painter.drawText(QRectF(legendX + squareSize + 4, legendY, legendRect.width() - squareSize - 6, legendLineHeight),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             legendLabels_[i]);
            legendY += legendLineHeight;
        }
        painter.restore();
    }
    QStringList errors;
    bool hasPoints = false;
    for (const auto& s : series_) {
        hasPoints = hasPoints || s.hasData();
        if (!s.error.isEmpty()) {
            errors.push_back(s.name + ": " + s.error);
        }
    }
    if (!hasPoints) {
        painter.setPen(subtleText);
        const QString text = errors.isEmpty() ? "No data loaded" : errors.join("; ");
        painter.drawText(pr.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, text);
    }
}

void PlotWidget::mousePressEvent(QMouseEvent* event)
{
    emit selected();
    const bool moveDrag = interactionMode_ == InteractionMode::Pan
                          || (interactionMode_ == InteractionMode::Zoom
                              && (event->button() == Qt::MiddleButton
                                  || (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ShiftModifier))));
    if ((event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton)
        || (event->button() == Qt::MiddleButton && !moveDrag)) {
        event->ignore();
        return;
    }
    setFocus(Qt::MouseFocusReason);
    if (moveDrag) {
        dragging_ = true;
        lastDragPos_ = event->position();
        setCursor(Qt::ClosedHandCursor);
    } else if (interactionMode_ == InteractionMode::Zoom) {
        zooming_ = true;
        zoomStart_ = event->position();
        zoomRubberBand_ = QRectF(zoomStart_, QSizeF());
    } else {
        pointTrackingActive_ = true;
        bool changed = false;
        const int legendIndex = legendSeriesAt(event->position());
        if (legendIndex >= 0) {
            const double dataX = hoverText_.isEmpty() ? pixelToData(event->position(), effectiveView(), plotRect()).x() : hoverData_.x();
            changed = updateHoverForSeriesX(legendIndex, dataX, true);
        } else {
            changed = updateHover(event->position(), true);
            if (!hoverSeriesLocked_) {
                const double dataX = pixelToData(event->position(), effectiveView(), plotRect()).x();
                changed = updateHoverForSeriesX(0, dataX, true) || changed;
            }
        }
        if (changed) {
            updatePointOverlay();
        }
        if (changed && !hoverText_.isEmpty()) {
            emit pointXChanged(hoverData_.x());
        }
    }
    event->accept();
}

void PlotWidget::keyPressEvent(QKeyEvent* event)
{
    if (interactionMode_ == InteractionMode::Point && event->modifiers() == Qt::NoModifier) {
        if (event->key() == Qt::Key_Escape) {
            if (pointTrackingActive_) {
                pointTrackingActive_ = false;
                pointHoverQueued_ = false;
                hoverText_.clear();
                hoverSeriesIndex_ = -1;
                hoverSeriesLocked_ = false;
                clearPointOverlay();
                emit pointTrackingStopped();
                event->accept();
                return;
            }
        } else if (event->key() == Qt::Key_Left) {
            if (stepActivePoint(-1)) {
                event->accept();
                return;
            }
        } else if (event->key() == Qt::Key_Right) {
            if (stepActivePoint(1)) {
                event->accept();
                return;
            }
        }
    }
    QWidget::keyPressEvent(event);
}

void PlotWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        invalidatePlotCache();
        update();
    }
}

void PlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QRectF pr = plotRect();
    QRectF view = effectiveView();
    const bool leftButtonDown = event->buttons().testFlag(Qt::LeftButton);
    const bool moveButtonDown = event->buttons().testFlag(Qt::MiddleButton)
                                || (leftButtonDown && event->modifiers().testFlag(Qt::ShiftModifier));
    if (!leftButtonDown && !moveButtonDown) {
        if (zooming_) {
            const QRect oldDirty = zoomRubberBandDirtyRect(zoomRubberBand_);
            zooming_ = false;
            zoomRubberBand_ = {};
            if (oldDirty.isValid() && !oldDirty.isEmpty()) {
                update(oldDirty);
            }
        }
        if (dragging_) {
            dragging_ = false;
            unsetCursor();
        }
    }
    if ((interactionMode_ == InteractionMode::Pan || interactionMode_ == InteractionMode::Zoom) && dragging_ && pr.isValid()) {
        const QPointF delta = event->position() - lastDragPos_;
        const double dx = -delta.x() / pr.width() * view.width();
        const double dy = delta.y() / pr.height() * view.height();
        view.translate(dx, dy);
        view_ = view;
        hasView_ = true;
        invalidatePlotCache();
        lastDragPos_ = event->position();
    }
    if (interactionMode_ == InteractionMode::Zoom && zooming_ && leftButtonDown && !dragging_) {
        const QRect oldDirty = zoomRubberBandDirtyRect(zoomRubberBand_);
        zoomRubberBand_ = QRectF(zoomStart_, event->position()).normalized().intersected(pr);
        const QRect newDirty = zoomRubberBandDirtyRect(zoomRubberBand_);
        const QRect dirty = oldDirty.united(newDirty);
        if (dirty.isValid() && !dirty.isEmpty()) {
            update(dirty);
        }
    }
    bool needsUpdate = (interactionMode_ == InteractionMode::Pan || interactionMode_ == InteractionMode::Zoom) && dragging_;
    if (interactionMode_ == InteractionMode::Point) {
        if (!pointTrackingActive_) {
            event->accept();
            return;
        }
        if (hoverSeriesLocked_) {
            schedulePointHoverUpdate(event->position());
        }
        needsUpdate = false;
    }
    if (needsUpdate) {
        scheduleUpdate();
    }
}

void PlotWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton) {
        event->ignore();
        return;
    }
    if (event->button() == Qt::LeftButton && interactionMode_ == InteractionMode::Zoom && zooming_) {
        const QRectF pr = plotRect();
        const QRect oldDirty = zoomRubberBandDirtyRect(zoomRubberBand_);
        const QRectF band = QRectF(zoomStart_, event->position()).normalized().intersected(pr);
        bool viewChanged = false;
        if (band.width() > 8 && band.height() > 8) {
            const QRectF view = effectiveView();
            const QPointF p1 = pixelToData(band.bottomLeft(), view, pr);
            const QPointF p2 = pixelToData(band.topRight(), view, pr);
            view_ = QRectF(QPointF(p1.x(), p1.y()), QPointF(p2.x(), p2.y())).normalized();
            expandFlatRange(view_);
            hasView_ = true;
            invalidatePlotCache();
            viewChanged = true;
        }
        zoomRubberBand_ = {};
        zooming_ = false;
        if (viewChanged) {
            update();
        } else if (oldDirty.isValid() && !oldDirty.isEmpty()) {
            update(oldDirty);
        }
        dragging_ = false;
        unsetCursor();
        event->accept();
        return;
    }
    dragging_ = false;
    unsetCursor();
    if (interactionMode_ == InteractionMode::Pan || interactionMode_ == InteractionMode::Zoom) {
        update();
    }
}

void PlotWidget::wheelEvent(QWheelEvent* event)
{
    const QRectF pr = plotRect();
    if (!pr.contains(event->position())) {
        return;
    }
    QRectF view = effectiveView();
    const QPointF center = pixelToData(event->position(), view, pr);
    const double factor = event->angleDelta().y() > 0 ? 0.82 : 1.22;
    const double left = center.x() - (center.x() - view.left()) * factor;
    const double right = center.x() + (view.right() - center.x()) * factor;
    const double bottom = center.y() - (center.y() - view.top()) * factor;
    const double top = center.y() + (view.bottom() - center.y()) * factor;
    view = QRectF(QPointF(left, bottom), QPointF(right, top));
    expandFlatRange(view);
    view_ = view;
    hasView_ = true;
    invalidatePlotCache();
    if (interactionMode_ == InteractionMode::Point && pointTrackingActive_ && hoverSeriesLocked_) {
        schedulePointHoverUpdate(event->position());
    } else if (updateHover(event->position())) {
        updatePointOverlay();
    }
    update();
}

void PlotWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    invalidatePlotCache();
    if (pointOverlay_) {
        pointOverlay_->setGeometry(rect());
        updatePointOverlay();
    }
}

void PlotWidget::leaveEvent(QEvent*)
{
    if (interactionMode_ == InteractionMode::Point) {
        pointTrackingActive_ = false;
        pointHoverQueued_ = false;
        emit pointTrackingStopped();
    }
}

int PlotWidget::legendSeriesAt(const QPointF& pixelPos) const
{
    const QRectF pr = plotRect();
    if (!pr.contains(pixelPos)) {
        return -1;
    }

    const FontSettings& fonts = fontSettings();
    QFont legendFont(fonts.family, fonts.legendSize + (largeDisplayMode_ ? 4 : 0));
    const QFontMetrics legendFm(legendFont);
    int legendWidth = 0;
    for (const QString& label : legendLabels_) {
        legendWidth = std::max(legendWidth, legendFm.horizontalAdvance(label));
    }
    if (legendLabels_.isEmpty()) {
        return -1;
    }

    legendWidth += 20;
    const int legendLineHeight = std::max(10, legendFm.height());
    const int legendHeight = legendLabels_.size() * legendLineHeight + 4;
    const QRectF legendArea = pr.adjusted(2, 2, -2, -2);
    const double legendW = std::min<double>(legendWidth, std::max(1.0, legendArea.width()));
    const double legendH = std::min<double>(legendHeight, std::max(1.0, legendArea.height()));
    const QRectF legendRect(legendArea.right() - legendW - 1,
                            legendArea.top() + 1,
                            legendW,
                            legendH);
    if (!legendRect.adjusted(-2, -2, 2, 2).contains(pixelPos)) {
        return -1;
    }

    for (int i = 0; i < legendLabels_.size(); ++i) {
        const QRectF rowRect(legendRect.left(),
                             legendRect.top() + 2 + i * legendLineHeight,
                             legendRect.width(),
                             legendLineHeight);
        if (rowRect.adjusted(-2, -1, 2, 1).contains(pixelPos)) {
            return legendSeriesIndexes_.value(i, i);
        }
    }
    return -1;
}

bool PlotWidget::nearestPointForSeries(int seriesIndex,
                                       double dataX,
                                       const QPointF* pixelPos,
                                       QPointF* point,
                                       QPointF* pixel,
                                       double* pixelDistance) const
{
    if (seriesIndex < 0 || seriesIndex >= series_.size()) {
        return false;
    }
    if (seriesIndex < spec_.signalSpecs.size() && spec_.signalSpecs[seriesIndex].hidden) {
        return false;
    }
    const SignalSeries& s = series_[seriesIndex];
    if (!s.hasData() || !std::isfinite(dataX)) {
        return false;
    }

    const QRectF pr = plotRect();
    const QRectF view = effectiveView();
    QPointF bestPoint;
    QPointF bestPixel;
    double bestDistance = std::numeric_limits<double>::infinity();
    double bestDx = std::numeric_limits<double>::infinity();

    auto acceptPointByX = [&](const QPointF& p) {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y())) {
            return false;
        }
        bestPoint = p;
        bestPixel = dataToPixel(p, view, pr);
        bestDistance = std::abs(p.x() - dataX);
        bestDx = bestDistance;
        return true;
    };

    auto considerPoint = [&](const QPointF& p) {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y())) {
            return;
        }
        const QPointF pp = dataToPixel(p, view, pr);
        double distance = std::abs(p.x() - dataX);
        if (pixelPos) {
            distance = std::hypot(pp.x() - pixelPos->x(), pp.y() - pixelPos->y());
        }
        const double dx = std::abs(p.x() - dataX);
        if (distance < bestDistance || (distance == bestDistance && dx < bestDx)) {
            bestPoint = p;
            bestPixel = pp;
            bestDistance = distance;
            bestDx = dx;
        }
    };

    if (s.hasUniformData() && s.uniformStep != 0.0) {
        int index = static_cast<int>(std::llround((dataX - s.uniformStart) / s.uniformStep));
        index = std::clamp(index, 0, static_cast<int>(s.uniformY.size()) - 1);
        if (!pixelPos) {
            acceptPointByX(s.pointAt(index));
        } else {
            const int begin = std::max(0, index - 4);
            const int end = std::min(static_cast<int>(s.uniformY.size()) - 1, index + 4);
            for (int i = begin; i <= end; ++i) {
                considerPoint(s.pointAt(i));
            }
        }
    } else if (!s.points.isEmpty()) {
        int lo = 0;
        int hi = s.points.size();
        while (lo < hi) {
            const int mid = lo + (hi - lo) / 2;
            if (s.points[mid].x() < dataX) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (!pixelPos) {
            int index = std::clamp(lo, 0, static_cast<int>(s.points.size()) - 1);
            if (index > 0 && std::abs(s.points[index - 1].x() - dataX) <= std::abs(s.points[index].x() - dataX)) {
                --index;
            }
            acceptPointByX(s.points[index]);
        } else {
            const int begin = std::max(0, lo - 8);
            const int end = std::min(static_cast<int>(s.points.size()) - 1, lo + 8);
            for (int i = begin; i <= end; ++i) {
                considerPoint(s.points[i]);
            }
        }
    }

    if (!std::isfinite(bestDistance)) {
        return false;
    }
    if (point) {
        *point = bestPoint;
    }
    if (pixel) {
        *pixel = bestPixel;
    }
    if (pixelDistance) {
        *pixelDistance = pixelPos ? bestDistance : std::abs(bestPoint.x() - dataX);
    }
    return true;
}

int PlotWidget::nearestSeriesAtPixel(const QPointF& pixelPos, double maxDistance, QPointF* point, QPointF* pixel) const
{
    const QRectF pr = plotRect();
    if (!pr.contains(pixelPos)) {
        return -1;
    }
    const double dataX = pixelToData(pixelPos, effectiveView(), pr).x();
    int bestSeries = -1;
    QPointF bestPoint;
    QPointF bestPixel;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (int i = 0; i < series_.size(); ++i) {
        QPointF candidatePoint;
        QPointF candidatePixel;
        double candidateDistance = std::numeric_limits<double>::infinity();
        if (!nearestPointForSeries(i, dataX, &pixelPos, &candidatePoint, &candidatePixel, &candidateDistance)) {
            continue;
        }
        if (candidateDistance < bestDistance) {
            bestSeries = i;
            bestPoint = candidatePoint;
            bestPixel = candidatePixel;
            bestDistance = candidateDistance;
        }
    }
    if (bestSeries < 0 || bestDistance > maxDistance) {
        return -1;
    }
    if (point) {
        *point = bestPoint;
    }
    if (pixel) {
        *pixel = bestPixel;
    }
    return bestSeries;
}

bool PlotWidget::updateHoverForSeriesX(int seriesIndex, double dataX, bool lockSeries)
{
    if (seriesIndex < 0 || seriesIndex >= series_.size() || !signalVisible(spec_, seriesIndex) || !series_[seriesIndex].hasData()) {
        for (int i = 0; i < series_.size(); ++i) {
            if (!signalVisible(spec_, i)) {
                continue;
            }
            if (series_[i].hasData()) {
                seriesIndex = i;
                break;
            }
        }
    }
    QPointF point;
    QPointF pixel;
    if (seriesIndex < 0 || !nearestPointForSeries(seriesIndex, dataX, nullptr, &point, &pixel, nullptr)) {
        const bool changed = !hoverText_.isEmpty();
        hoverText_.clear();
        return changed;
    }

    const QString valueText = QString("%1, %2").arg(point.x(), 0, 'g', 6).arg(point.y(), 0, 'g', 6);
    const QString nextText = valueText;
    const bool nextLocked = lockSeries ? true : hoverSeriesLocked_;
    const bool changed = hoverSeriesIndex_ != seriesIndex
                         || hoverSeriesLocked_ != nextLocked
                         || hoverPixel_ != pixel
                         || hoverData_ != point
                         || hoverText_ != nextText;
    hoverSeriesIndex_ = seriesIndex;
    hoverSeriesLocked_ = nextLocked;
    hoverPixel_ = pixel;
    hoverData_ = point;
    hoverText_ = nextText;
    return changed;
}

bool PlotWidget::stepActivePoint(int delta)
{
    if (interactionMode_ != InteractionMode::Point || delta == 0 || hoverText_.isEmpty()) {
        return false;
    }
    int seriesIndex = hoverSeriesIndex_;
    if (seriesIndex < 0 || seriesIndex >= series_.size() || !series_[seriesIndex].hasData()) {
        return false;
    }

    const SignalSeries& s = series_[seriesIndex];
    double nextX = qQNaN();
    if (s.hasUniformData() && s.uniformStep != 0.0) {
        int index = static_cast<int>(std::llround((hoverData_.x() - s.uniformStart) / s.uniformStep));
        index = std::clamp(index + delta, 0, static_cast<int>(s.uniformY.size()) - 1);
        nextX = s.uniformStart + static_cast<double>(index) * s.uniformStep;
    } else if (!s.points.isEmpty()) {
        int lo = 0;
        int hi = s.points.size();
        while (lo < hi) {
            const int mid = lo + (hi - lo) / 2;
            if (s.points[mid].x() < hoverData_.x()) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        int index = std::clamp(lo, 0, static_cast<int>(s.points.size()) - 1);
        if (index > 0 && std::abs(s.points[index - 1].x() - hoverData_.x()) <= std::abs(s.points[index].x() - hoverData_.x())) {
            --index;
        }
        index = std::clamp(index + delta, 0, static_cast<int>(s.points.size()) - 1);
        nextX = s.points[index].x();
    }

    if (!std::isfinite(nextX)) {
        return false;
    }
    const bool changed = updateHoverForSeriesX(seriesIndex, nextX, true);
    setSyncedPointX(hoverData_.x(), seriesIndex);
    if (changed && !hoverText_.isEmpty()) {
        emit pointXChanged(hoverData_.x());
    }
    return changed;
}

void PlotWidget::setPointX(double x)
{
    if (interactionMode_ != InteractionMode::Point) {
        return;
    }
    if (!std::isfinite(x)) {
        hoverText_.clear();
        hoverSeriesIndex_ = -1;
        hoverSeriesLocked_ = false;
        clearPointOverlay();
        clearSyncedPoint();
        return;
    }
    setSyncedPointX(x, 0);
    if (updateHoverForSeriesX(0, x, false)) {
        updatePointOverlay();
    }
}

void PlotWidget::setSyncedPointX(double x, int seriesIndex)
{
    if (interactionMode_ != InteractionMode::Point || !std::isfinite(x)) {
        clearSyncedPoint();
        return;
    }
    if (seriesIndex < 0 || seriesIndex >= series_.size() || !signalVisible(spec_, seriesIndex) || !series_[seriesIndex].hasData()) {
        seriesIndex = -1;
        for (int i = 0; i < series_.size(); ++i) {
            if (!signalVisible(spec_, i)) {
                continue;
            }
            if (series_[i].hasData()) {
                seriesIndex = i;
                break;
            }
        }
    }

    PointReadout next;
    QPointF point;
    QPointF pixel;
    if (nearestPointForSeries(seriesIndex, x, nullptr, &point, &pixel, nullptr)) {
        next.plotRect = plotRect();
        next.pixel = pixel;
        next.data = point;
        next.visible = next.plotRect.contains(next.pixel);
        next.showText = true;
        next.color = seriesColor(seriesIndex);
    }

    const bool samePoint = syncedPoint_.visible == next.visible
                           && syncedPoint_.showText == next.showText
                           && syncedPoint_.color == next.color
                           && qRound(syncedPoint_.pixel.x()) == qRound(next.pixel.x())
                           && qRound(syncedPoint_.pixel.y()) == qRound(next.pixel.y())
                           && syncedPoint_.data == next.data
                           && syncedPoint_.plotRect.toAlignedRect() == next.plotRect.toAlignedRect();
    if (samePoint) {
        return;
    }
    if (next.visible) {
        next.text = QString("%1, %2").arg(point.x(), 0, 'g', 6).arg(point.y(), 0, 'g', 6);
    }

    QRect dirty = syncedPointDirtyRect(syncedPoint_).united(syncedPointDirtyRect(next));
    syncedPoint_ = std::move(next);
    if (dirty.isValid() && !dirty.isEmpty()) {
        update(dirty);
    } else {
        update();
    }
}

void PlotWidget::clearSyncedPoint()
{
    if (!syncedPoint_.visible && syncedPoint_.text.isEmpty()) {
        return;
    }
    const QRect dirty = syncedPointDirtyRect(syncedPoint_);
    syncedPoint_ = {};
    if (dirty.isValid() && !dirty.isEmpty()) {
        update(dirty);
    } else {
        update();
    }
}

PointReadout PlotWidget::pointReadoutForX(double x, int seriesIndex, QWidget* target, bool includeText) const
{
    PointReadout readout;
    if (!target || interactionMode_ != InteractionMode::Point || !std::isfinite(x)) {
        return readout;
    }
    if (seriesIndex < 0 || seriesIndex >= series_.size() || !series_[seriesIndex].hasData()) {
        seriesIndex = 0;
    }
    QPointF point;
    QPointF pixel;
    if (!nearestPointForSeries(seriesIndex, x, nullptr, &point, &pixel, nullptr)) {
        return readout;
    }

    const QRectF pr = plotRect();
    const QPoint topLeft = mapTo(target, pr.topLeft().toPoint());
    const QPoint bottomRight = mapTo(target, pr.bottomRight().toPoint());
    readout.plotRect = QRectF(topLeft, bottomRight).normalized();
    readout.pixel = QPointF(mapTo(target, pixel.toPoint()));
    readout.data = point;
    readout.color = seriesColor(seriesIndex);
    if (includeText) {
        readout.text = QString("%1, %2").arg(point.x(), 0, 'g', 6).arg(point.y(), 0, 'g', 6);
    }
    readout.visible = readout.plotRect.contains(readout.pixel);
    return readout;
}

bool PlotWidget::updatePointFromGlobalPosition(const QPointF& globalPos, double* dataX)
{
    if (interactionMode_ != InteractionMode::Point) {
        return false;
    }
    const QRectF pr = plotRect();
    if (!pr.isValid() || pr.width() <= 0.0) {
        return false;
    }
    const QPointF local = mapFromGlobal(globalPos.toPoint());
    const double xPixel = std::clamp(local.x(), pr.left(), pr.right());
    const double yPixel = std::clamp(local.y(), pr.top(), pr.bottom());
    const QPointF clamped(xPixel, yPixel);
    const double x = pixelToData(clamped, effectiveView(), pr).x();
    bool changed = false;
    if (hoverSeriesLocked_ && hoverSeriesIndex_ >= 0 && hoverSeriesIndex_ < series_.size()) {
        changed = updateHoverForSeriesX(hoverSeriesIndex_, x, false);
    } else {
        changed = updateHover(clamped, false);
    }
    if (dataX) {
        *dataX = hoverText_.isEmpty() ? x : hoverData_.x();
    }
    if (changed) {
        updatePointOverlay();
    }
    return changed;
}

bool PlotWidget::updateHover(const QPointF& pixelPos, bool lockSeries)
{
    const QRectF pr = plotRect();
    if (!pr.contains(pixelPos)) {
        const bool changed = !hoverText_.isEmpty();
        hoverText_.clear();
        if (!hoverSeriesLocked_) {
            hoverSeriesIndex_ = -1;
        }
        return changed;
    }
    const QRectF view = effectiveView();
    const QPointF data = pixelToData(pixelPos, view, pr);

    if (hoverSeriesLocked_ && !lockSeries && hoverSeriesIndex_ >= 0 && hoverSeriesIndex_ < series_.size()) {
        if (updateHoverForSeriesX(hoverSeriesIndex_, data.x(), false)) {
            return true;
        }
        return false;
    }

    QPointF point;
    QPointF pixel;
    constexpr double kCurvePickPixels = 16.0;
    const int pickedSeries = nearestSeriesAtPixel(pixelPos, kCurvePickPixels, &point, &pixel);
    if (pickedSeries >= 0) {
        return updateHoverForSeriesX(pickedSeries, point.x(), lockSeries);
    } else if (hoverSeriesLocked_ && hoverSeriesIndex_ >= 0 && hoverSeriesIndex_ < series_.size()) {
        return updateHoverForSeriesX(hoverSeriesIndex_, data.x(), false);
    } else {
        const QString nextText = QString("%1, %2").arg(data.x(), 0, 'g', 6).arg(data.y(), 0, 'g', 6);
        const bool changed = hoverPixel_ != pixelPos || hoverData_ != data || hoverText_ != nextText;
        hoverPixel_ = pixelPos;
        hoverData_ = data;
        hoverText_ = nextText;
        return changed;
    }
}
