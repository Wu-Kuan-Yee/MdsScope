// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mdsscope_internal.hpp"

inline constexpr int kMinMaxBlockSize = 256;

inline bool signalVisible(const PlotSpec& spec, int index)
{
    return index < 0 || index >= spec.signalSpecs.size() || !spec.signalSpecs[index].hidden;
}

inline void rebuildMinMaxIndex(SignalSeries& series)
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

inline QVector<QPointF> displayUniformPointsUsingMinMaxIndex(const SignalSeries& series,
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

inline QVector<QPointF> displayPointSeriesUsingMinMaxIndex(const SignalSeries& series,
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
