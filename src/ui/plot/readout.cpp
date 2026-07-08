// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "point_overlay.hpp"
#include "helpers.hpp"

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
