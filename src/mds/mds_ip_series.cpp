// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mds_ip_client.hpp"

namespace mds_client_internal {

using Message = MdsIpClient::Message;

void MdsIpClient::minMaxDownsample(SignalSeries* series, int maxPoints)
{
        if (maxPoints <= 0 || series->points.size() <= maxPoints) {
            return;
        }
        maxPoints = std::clamp(maxPoints, 500, 50000);
        const QVector<QPointF> original = series->points;
        const int buckets = std::max(1, maxPoints / 2);
        QVector<QPointF> reduced;
        reduced.reserve(std::min(static_cast<int>(original.size()), buckets * 2));
        for (int b = 0; b < buckets; ++b) {
            const int start = static_cast<int>((static_cast<qint64>(b) * original.size()) / buckets);
            const int end = static_cast<int>((static_cast<qint64>(b + 1) * original.size()) / buckets);
            if (end <= start) {
                continue;
            }
            int minIndex = start;
            int maxIndex = start;
            for (int i = start + 1; i < end; ++i) {
                if (original[i].y() < original[minIndex].y()) {
                    minIndex = i;
                }
                if (original[i].y() > original[maxIndex].y()) {
                    maxIndex = i;
                }
            }
            if (minIndex == maxIndex) {
                reduced.push_back(original[minIndex]);
            } else if (minIndex < maxIndex) {
                reduced.push_back(original[minIndex]);
                reduced.push_back(original[maxIndex]);
            } else {
                reduced.push_back(original[maxIndex]);
                reduced.push_back(original[minIndex]);
            }
        }
        series->points = std::move(reduced);
    }

void MdsIpClient::buildUniformOverview(SignalSeries* series, int maxPoints)
{
        if (!series || series->uniformY.isEmpty() || maxPoints <= 0) {
            return;
        }
        const int n = series->uniformY.size();
        if (n <= maxPoints) {
            series->points.resize(n);
            for (int i = 0; i < n; ++i) {
                series->points[i] = series->pointAt(i);
            }
            return;
        }

        const int buckets = std::max(1, maxPoints / 2);
        series->points.clear();
        series->points.reserve(std::min(n, buckets * 2));
        for (int b = 0; b < buckets; ++b) {
            const int bucketStart = static_cast<int>((static_cast<qint64>(b) * n) / buckets);
            const int bucketEnd = static_cast<int>((static_cast<qint64>(b + 1) * n) / buckets);
            if (bucketEnd <= bucketStart) {
                continue;
            }
            int minIndex = bucketStart;
            int maxIndex = bucketStart;
            for (int i = bucketStart + 1; i < bucketEnd; ++i) {
                if (series->uniformY[i] < series->uniformY[minIndex]) {
                    minIndex = i;
                }
                if (series->uniformY[i] > series->uniformY[maxIndex]) {
                    maxIndex = i;
                }
            }
            if (minIndex == maxIndex) {
                series->points.push_back(series->pointAt(minIndex));
            } else if (minIndex < maxIndex) {
                series->points.push_back(series->pointAt(minIndex));
                series->points.push_back(series->pointAt(maxIndex));
            } else {
                series->points.push_back(series->pointAt(maxIndex));
                series->points.push_back(series->pointAt(minIndex));
            }
        }
    }

SignalSeries MdsIpClient::makeSeries(QString name, const QVector<double>& y, const QVector<double>& x, int maxPoints)
{
        SignalSeries out;
        out.name = std::move(name);
        const int n = x.isEmpty() ? y.size() : std::min(y.size(), x.size());
        if (maxPoints <= 0) {
            out.points.resize(n);
            int used = 0;
            for (int i = 0; i < n; ++i) {
                const double px = x.isEmpty() ? static_cast<double>(i) : x[i];
                const double py = y[i];
                if (std::isfinite(px) && std::isfinite(py)) {
                    out.points[used++] = QPointF(px, py);
                }
            }
            if (used != n) {
                out.points.resize(used);
            }
            return out;
        }

        out.points.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double px = x.isEmpty() ? static_cast<double>(i) : x[i];
            const double py = y[i];
            if (std::isfinite(px) && std::isfinite(py)) {
                out.points.push_back(QPointF(px, py));
            }
        }
        minMaxDownsample(&out, maxPoints);
        return out;
    }

SignalSeries MdsIpClient::makeSeriesUniformX(QString name,
                                       const QVector<double>& y,
                                       double start,
                                       double step,
                                       int maxPoints)
{
        SignalSeries out;
        out.name = std::move(name);
        const int n = y.size();
        if (n <= 0 || !std::isfinite(start) || !std::isfinite(step) || step == 0.0) {
            return out;
        }

        if (maxPoints <= 0) {
            out.uniformY.resize(n);
            int used = 0;
            double minY = std::numeric_limits<double>::infinity();
            double maxY = -std::numeric_limits<double>::infinity();
            for (int i = 0; i < n; ++i) {
                const double py = y[i];
                if (std::isfinite(py)) {
                    out.uniformY[used++] = static_cast<float>(py);
                    minY = std::min(minY, py);
                    maxY = std::max(maxY, py);
                }
            }
            out.uniformY.resize(used);
            if (used > 0) {
                out.uniformStart = start;
                out.uniformStep = step;
                out.uniformMinY = minY;
                out.uniformMaxY = maxY;
            }
            return out;
        }

        if (maxPoints > 0 && n > maxPoints) {
            maxPoints = std::clamp(maxPoints, 500, 50000);
            const int buckets = std::max(1, maxPoints / 2);
            out.points.reserve(std::min(n, buckets * 2));
            for (int b = 0; b < buckets; ++b) {
                const int bucketStart = static_cast<int>((static_cast<qint64>(b) * n) / buckets);
                const int bucketEnd = static_cast<int>((static_cast<qint64>(b + 1) * n) / buckets);
                if (bucketEnd <= bucketStart) {
                    continue;
                }
                int minIndex = -1;
                int maxIndex = -1;
                for (int i = bucketStart; i < bucketEnd; ++i) {
                    if (!std::isfinite(y[i])) {
                        continue;
                    }
                    if (minIndex < 0) {
                        minIndex = i;
                        maxIndex = i;
                        continue;
                    }
                    if (y[i] < y[minIndex]) {
                        minIndex = i;
                    }
                    if (y[i] > y[maxIndex]) {
                        maxIndex = i;
                    }
                }
                if (minIndex < 0) {
                    continue;
                }
                auto appendPoint = [&](int index) {
                    out.points.push_back(QPointF(start + static_cast<double>(index) * step, y[index]));
                };
                if (minIndex == maxIndex) {
                    appendPoint(minIndex);
                } else if (minIndex < maxIndex) {
                    appendPoint(minIndex);
                    appendPoint(maxIndex);
                } else {
                    appendPoint(maxIndex);
                    appendPoint(minIndex);
                }
            }
            return out;
        }

        out.points.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double py = y[i];
            if (std::isfinite(py)) {
                out.points.push_back(QPointF(start + static_cast<double>(i) * step, py));
            }
        }
        return out;
    }

int MdsIpClient::numericElementSize(const Message& msg)
{
        if (msg.dtype == 11 || msg.dtype == 53) {
            return 8;
        }
        if (msg.dtype == 10 || msg.dtype == 52 || msg.body.size() % 4 == 0) {
            return 4;
        }
        return 0;
    }

double MdsIpClient::numericAt(const QByteArray& body, int elementSize, int index)
{
        const auto* bytes = reinterpret_cast<const uchar*>(body.constData());
        if (elementSize == 8) {
            const quint64 bits = qFromBigEndian<quint64>(bytes + static_cast<qsizetype>(index) * 8);
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }
        const quint32 bits = qFromBigEndian<quint32>(bytes + static_cast<qsizetype>(index) * 4);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

SignalSeries MdsIpClient::makeSeriesUniformXFromMessage(QString name,
                                                  const Message& msg,
                                                  double start,
                                                  double step,
                                                  int maxPoints,
                                                  QString* error)
{
        SignalSeries out;
        out.name = std::move(name);
        if (!error->isEmpty()) {
            return out;
        }
        if (msg.dtype == 14) {
            *error = QString::fromUtf8(msg.body).trimmed();
            return out;
        }

        const int elementSize = numericElementSize(msg);
        if (elementSize <= 0 || msg.body.isEmpty() || msg.body.size() % elementSize != 0
            || !std::isfinite(start) || !std::isfinite(step) || step == 0.0) {
            return out;
        }

        const int n = static_cast<int>(msg.body.size() / elementSize);
        if (maxPoints <= 0) {
            out.uniformY.resize(n);
            int used = 0;
            double minY = std::numeric_limits<double>::infinity();
            double maxY = -std::numeric_limits<double>::infinity();
            for (int i = 0; i < n; ++i) {
                const double value = numericAt(msg.body, elementSize, i);
                if (std::isfinite(value)) {
                    out.uniformY[used++] = static_cast<float>(value);
                    minY = std::min(minY, value);
                    maxY = std::max(maxY, value);
                }
            }
            out.uniformY.resize(used);
            if (used > 0) {
                out.uniformStart = start;
                out.uniformStep = step;
                out.uniformMinY = minY;
                out.uniformMaxY = maxY;
            }
            return out;
        }

        if (maxPoints > 0 && n > maxPoints) {
            maxPoints = std::clamp(maxPoints, 500, 50000);
            const int buckets = std::max(1, maxPoints / 2);
            out.points.reserve(std::min(n, buckets * 2));
            for (int b = 0; b < buckets; ++b) {
                const int bucketStart = static_cast<int>((static_cast<qint64>(b) * n) / buckets);
                const int bucketEnd = static_cast<int>((static_cast<qint64>(b + 1) * n) / buckets);
                if (bucketEnd <= bucketStart) {
                    continue;
                }

                int minIndex = -1;
                int maxIndex = -1;
                double minValue = 0.0;
                double maxValue = 0.0;
                for (int i = bucketStart; i < bucketEnd; ++i) {
                    const double value = numericAt(msg.body, elementSize, i);
                    if (!std::isfinite(value)) {
                        continue;
                    }
                    if (minIndex < 0) {
                        minIndex = i;
                        maxIndex = i;
                        minValue = value;
                        maxValue = value;
                        continue;
                    }
                    if (value < minValue) {
                        minIndex = i;
                        minValue = value;
                    }
                    if (value > maxValue) {
                        maxIndex = i;
                        maxValue = value;
                    }
                }
                if (minIndex < 0) {
                    continue;
                }
                auto appendPoint = [&](int index, double value) {
                    out.points.push_back(QPointF(start + static_cast<double>(index) * step, value));
                };
                if (minIndex == maxIndex) {
                    appendPoint(minIndex, minValue);
                } else if (minIndex < maxIndex) {
                    appendPoint(minIndex, minValue);
                    appendPoint(maxIndex, maxValue);
                } else {
                    appendPoint(maxIndex, maxValue);
                    appendPoint(minIndex, minValue);
                }
            }
            return out;
        }

        out.points.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double value = numericAt(msg.body, elementSize, i);
            if (std::isfinite(value)) {
                out.points.push_back(QPointF(start + static_cast<double>(i) * step, value));
            }
        }
        return out;
    }

SignalSeries MdsIpClient::makeSeriesFromAdjacentSegments(QString name, const QVector<double>& values, int offset, int count, int maxPoints)
{
        SignalSeries out;
        out.name = std::move(name);
        const int n = std::min(count, static_cast<int>((values.size() - offset) / 2));
        out.points.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double px = values[offset + i];
            const double py = values[offset + n + i];
            if (std::isfinite(px) && std::isfinite(py)) {
                out.points.push_back(QPointF(px, py));
            }
        }
        minMaxDownsample(&out, maxPoints);
        return out;
    }

SignalSeries MdsIpClient::makeSeriesFromCombined(QString name, const QVector<double>& xy, int maxPoints)
{
        SignalSeries out;
        out.name = std::move(name);
        const int n = static_cast<int>(xy.size() / 2);
        out.points.reserve(n);
        for (int i = 0; i < n; ++i) {
            const double px = xy[i];
            const double py = xy[n + i];
            if (std::isfinite(px) && std::isfinite(py)) {
                out.points.push_back(QPointF(px, py));
            }
        }
        minMaxDownsample(&out, maxPoints);
        return out;
    }
} // namespace mds_client_internal
