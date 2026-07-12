// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "helpers.hpp"

void PlotWidget::resetScale(bool repaint)
{
    hasView_ = false;
    view_ = {};
    invalidatePlotCache();
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

void PlotWidget::applyYRangeKeepX(double ymin, double ymax)
{
    if (!std::isfinite(ymin) || !std::isfinite(ymax) || ymin == ymax) {
        return;
    }
    if (ymax < ymin) {
        std::swap(ymin, ymax);
    }

    QRectF next = effectiveView();
    if (!next.isValid() || next.width() <= 0.0) {
        next = dataBounds();
    }
    if (!next.isValid() || next.width() <= 0.0) {
        return;
    }
    next.setTop(ymin);
    next.setBottom(ymax);
    expandFlatRange(next);
    view_ = next;
    hasView_ = true;
    invalidatePlotCache();
    update();
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
