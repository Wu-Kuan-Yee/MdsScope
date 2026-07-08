// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mds_ip_client.hpp"

namespace mds_client_internal {

using EastThinPlan = MdsIpClient::EastThinPlan;
using NativeRequest = MdsIpClient::NativeRequest;
using ScaledSignalExpr = MdsIpClient::ScaledSignalExpr;
using SignalFetchResult = MdsIpClient::SignalFetchResult;
using ThinSampling = MdsIpClient::ThinSampling;
using UniformTimebase = MdsIpClient::UniformTimebase;

QVector<SignalFetchResult> MdsIpClient::fetchEastThinPipelinedOnOpenSocket(QTcpSocket& socket,
                                                              const QVector<NativeRequest>& requests,
                                                              QString* error) const
{
        struct Pending {
            NativeRequest request;
            EastThinPlan plan;
            QElapsedTimer timer;
        };

        QVector<Pending> pending;
        pending.reserve(requests.size());
        for (const NativeRequest& request : requests) {
            if (request.sig.xExpr.trimmed().isEmpty()
                && isEastTimebaseCandidate(request.plot.shot, request.sig)
                && request.maxPoints > 0) {
                Pending item;
                item.request = request;
                pending.push_back(std::move(item));
            }
        }
        if (pending.size() <= 1) {
            return {};
        }

        QElapsedTimer chunkTimer;
        chunkTimer.start();
        for (Pending& item : pending) {
            item.timer.start();
            const QString yExpr = item.request.sig.yExpr.trimmed();
            if (!queueValue(socket, QString("[size(%1),%1:freq,%1:trigtime]").arg(yExpr), error)) {
                return {};
            }
        }
        if (!flushQueuedValues(socket, error)) {
            return {};
        }

        QVector<Pending> valid;
        valid.reserve(pending.size());
        QVector<SignalFetchResult> results;
        results.reserve(pending.size());
        for (Pending& item : pending) {
            QString localError;
            const Message metaMessage = readMessage(socket, &localError);
            const QVector<double> meta = numericFromMessage(metaMessage, &localError);
            SignalFetchResult result;
            result.loadedIndex = item.request.loadedIndex;
            result.series.name = normalizedMdsSignal(item.request.sig.yExpr);
            if (meta.size() < 3 || !std::isfinite(meta[0]) || !std::isfinite(meta[1]) || !std::isfinite(meta[2])) {
                if (!localError.isEmpty()) {
                    result.series.error = localError;
                    traceMdsLine(QString("signal_ms=%1 shot=%2 tree=%3 y=%4 points=%5 error=%6")
                                     .arg(item.timer.elapsed())
                                     .arg(item.request.plot.shot)
                                     .arg(item.request.sig.experiment)
                                     .arg(item.request.sig.yExpr)
                                     .arg(result.series.pointCount())
                                     .arg(result.series.error.simplified()));
                    results.push_back(std::move(result));
                }
                continue;
            }

            const int pointCount = static_cast<int>(std::llround(meta[0]));
            const int freq = static_cast<int>(std::llround(meta[1]));
            if (pointCount <= 0 || freq <= 0) {
                result.series.error = "invalid EAST signal metadata";
                results.push_back(std::move(result));
                continue;
            }

            item.plan.sampling = samplingFromPointCount(pointCount, item.request.maxPoints);
            if (item.plan.sampling.sampledCount <= 0) {
                result.series.error = "empty signal";
                results.push_back(std::move(result));
                continue;
            }
            item.plan.timebase.valid = true;
            item.plan.timebase.start = meta[2];
            item.plan.timebase.step = 1.0 / static_cast<double>(freq);
            item.plan.valid = true;
            traceMdsLine(QString("east_thin_plan y=%1 size=%2 step=%3 freq=%4 trigtime=%5")
                             .arg(item.request.sig.yExpr.trimmed())
                             .arg(pointCount)
                             .arg(item.plan.sampling.step)
                             .arg(freq)
                             .arg(item.plan.timebase.start, 0, 'g', 12));
            valid.push_back(std::move(item));
        }
        if (valid.isEmpty()) {
            return results;
        }

        for (const Pending& item : valid) {
            const QString yExpr = item.request.sig.yExpr.trimmed();
            const int sampleStep = item.plan.sampling.step;
            const QString sampledYExpr = sampleStep > 1
                                             ? QString("( _jscope_0 = (data(%1)[1:*:%2]), fs_float(_jscope_0))").arg(yExpr).arg(sampleStep)
                                             : QString("( _jscope_0 = (%1), fs_float(_jscope_0))").arg(yExpr);
            if (!queueValue(socket, sampledYExpr, error)) {
                return results;
            }
        }
        if (!flushQueuedValues(socket, error)) {
            return results;
        }

        for (const Pending& item : valid) {
            QString localError;
            SignalFetchResult result;
            result.loadedIndex = item.request.loadedIndex;
            result.series.name = normalizedMdsSignal(item.request.sig.yExpr);
            const double sampledStart = item.plan.sampling.step > 1
                                            ? item.plan.timebase.start + item.plan.timebase.step
                                            : item.plan.timebase.start;
            const double sampledStep = item.plan.timebase.step * static_cast<double>(item.plan.sampling.step);
            result.series = makeSeriesUniformXFromMessage(result.series.name,
                                                          readMessage(socket, &localError),
                                                          sampledStart,
                                                          sampledStep,
                                                          item.request.maxPoints,
                                                          &localError);
            if (!result.series.hasData()) {
                result.series.error = localError.isEmpty() ? "empty signal" : localError;
            }
            traceMdsLine(QString("signal_ms=%1 shot=%2 tree=%3 y=%4 points=%5 error=%6")
                             .arg(item.timer.elapsed())
                             .arg(item.request.plot.shot)
                             .arg(item.request.sig.experiment)
                             .arg(item.request.sig.yExpr)
                             .arg(result.series.pointCount())
                             .arg(result.series.error.simplified()));
            emitResult(item.request, result);
            results.push_back(std::move(result));
        }

        traceMdsLine(QString("east_pipeline_chunk_ms=%1 count=%2").arg(chunkTimer.elapsed()).arg(results.size()));
        return results;
    }

QVector<SignalFetchResult> MdsIpClient::fetchDirectPipelinedOnOpenSocket(QTcpSocket& socket,
                                                            const QVector<NativeRequest>& requests,
                                                            const QHash<QString, UniformTimebase>& timebaseCache,
                                                            QString* error) const
{
        QElapsedTimer timer;
        timer.start();
        struct Pending {
            NativeRequest request;
            QString yExpr;
            QString xExpr;
            UniformTimebase timebase;
            bool needsX = false;
        };

        QVector<Pending> pending;
        pending.reserve(requests.size());
        for (const NativeRequest& request : requests) {
            Pending item;
            item.request = request;
            item.yExpr = QString("( _jscope_0 = (%1), fs_float(_jscope_0))").arg(request.sig.yExpr);
            const QString xExpr = request.sig.xExpr.trimmed();
            if (!xExpr.isEmpty()) {
                item.xExpr = QString("( _jscope_1 = (%1), ft_float(_jscope_1))").arg(xExpr);
                item.needsX = true;
            } else {
                item.timebase = timebaseCache.value(eastTimebaseKey(request.plot.shot, request.sig));
                if (!item.timebase.valid) {
                    item.xExpr = QString("( _jscope_1 = (dim_of(%1)), ft_float(_jscope_1))").arg(request.sig.yExpr);
                    item.needsX = true;
                }
            }
            pending.push_back(std::move(item));
        }

        for (const Pending& item : pending) {
            if (!queueValue(socket, item.yExpr, error)) {
                return {};
            }
            if (item.needsX && !queueValue(socket, item.xExpr, error)) {
                return {};
            }
        }
        if (!flushQueuedValues(socket, error)) {
            return {};
        }

        QVector<SignalFetchResult> results;
        results.reserve(pending.size());
        for (const Pending& item : pending) {
            SignalFetchResult result;
            result.loadedIndex = item.request.loadedIndex;
            const QVector<double> y = numericFromMessage(readMessage(socket, error), error);
            if (y.isEmpty()) {
                result.series.name = normalizedMdsSignal(item.request.sig.yExpr);
                result.series.error = error && !error->isEmpty() ? *error : "empty signal";
                if (item.needsX) {
                    readMessage(socket, error);
                }
                results.push_back(std::move(result));
                continue;
            }

            if (item.needsX) {
                const QVector<double> x = numericFromMessage(readMessage(socket, error), error);
                result.series = makeSeries(normalizedMdsSignal(item.request.sig.yExpr), y, x, item.request.maxPoints);
            } else {
                result.series = makeSeriesUniformX(normalizedMdsSignal(item.request.sig.yExpr),
                                                  y,
                                                  item.timebase.start,
                                                  item.timebase.step,
                                                  item.request.maxPoints);
            }
            if (!result.series.hasData()) {
                result.series.error = error && !error->isEmpty() ? *error : "no numeric points";
            }
            results.push_back(std::move(result));
        }
        traceMdsLine(QString("pipeline_chunk_ms=%1 count=%2").arg(timer.elapsed()).arg(results.size()));
        return results;
    }

QVector<SignalFetchResult> MdsIpClient::fetchEastTimeContextBatchOnOpenSocket(QTcpSocket& socket,
                                                                 const QVector<NativeRequest>& requests,
                                                                 const QSet<int>& fetchedIndexes,
                                                                 QString* error) const
{
        struct Pending {
            NativeRequest request;
            ScaledSignalExpr scaled;
            QElapsedTimer timer;
        };

        QVector<Pending> pending;
        pending.reserve(requests.size());
        for (const NativeRequest& request : requests) {
            if (fetchedIndexes.contains(request.loadedIndex) || !request.sig.xExpr.trimmed().isEmpty()) {
                continue;
            }
            ScaledSignalExpr scaled = scaledSimpleSignalExpr(request.sig.yExpr);
            if (!scaled.valid || !prefersEastTimeContext(scaled.baseExpr)) {
                continue;
            }
            SignalSpec baseSig = request.sig;
            baseSig.yExpr = scaled.baseExpr;
            if (!isEastTimebaseCandidate(request.plot.shot, baseSig)) {
                continue;
            }
            Pending item;
            item.request = request;
            item.scaled = std::move(scaled);
            pending.push_back(std::move(item));
        }
        if (pending.size() < 2) {
            return {};
        }

        SignalSpec firstSig = pending.front().request.sig;
        firstSig.yExpr = pending.front().scaled.baseExpr;
        QString localError;
        const int maxPoints = pending.front().request.maxPoints > 0 ? pending.front().request.maxPoints : 2000;
        const EastThinPlan plan = eastThinPlan(socket, pending.front().request.plot.shot, firstSig, maxPoints, &localError);
        if (!plan.valid || plan.sampling.sourceCount <= 0 || !plan.timebase.valid) {
            if (error) {
                error->clear();
            }
            return {};
        }

        const PlotSpec& firstPlot = pending.front().request.plot;
        double start = plan.timebase.start;
        double end = start + static_cast<double>(plan.sampling.sourceCount - 1) * plan.timebase.step;
        double delta = plan.timebase.step * static_cast<double>(plan.sampling.step);
        if (firstPlot.customXRange
            && std::isfinite(firstPlot.xmin)
            && std::isfinite(firstPlot.xmax)
            && firstPlot.xmax > firstPlot.xmin) {
            start = firstPlot.xmin;
            end = firstPlot.xmax;
            delta = (end - start) / static_cast<double>(maxPoints);
        }
        if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(delta) || delta <= 0.0 || end <= start) {
            if (error) {
                error->clear();
            }
            return {};
        }
        value(socket,
              QString("SetTimeContext(%1,%2,%3)")
                  .arg(start, 0, 'g', 12)
                  .arg(end, 0, 'g', 12)
                  .arg(delta, 0, 'g', 12),
              &localError);
        if (!localError.isEmpty()) {
            value(socket, "SetTimeContext()", &localError);
            if (error) {
                error->clear();
            }
            return {};
        }

        QVector<SignalFetchResult> results;
        results.reserve(pending.size());
        for (Pending& item : pending) {
            item.timer.start();
            SignalFetchResult result;
            result.loadedIndex = item.request.loadedIndex;
            const Message yMessage = value(socket,
                                           QString("( _jscope_0 = (%1), fs_float(_jscope_0))").arg(item.scaled.baseExpr),
                                           &localError);
            result.series = makeSeriesUniformXFromMessage(normalizedMdsSignal(item.request.sig.yExpr),
                                                          yMessage,
                                                          start,
                                                          delta,
                                                          item.request.maxPoints,
                                                          &localError);
            if (!result.series.hasData()) {
                result.series.error = localError.isEmpty() ? "empty signal" : localError;
            } else {
                applySeriesScale(&result.series, item.request.sig.yExpr, item.scaled.scale);
            }
            traceMdsLine(QString("signal_ms=%1 shot=%2 tree=%3 y=%4 points=%5 error=%6")
                             .arg(item.timer.elapsed())
                             .arg(item.request.plot.shot)
                             .arg(item.request.sig.experiment)
                             .arg(item.request.sig.yExpr)
                             .arg(result.series.pointCount())
                             .arg(result.series.error.simplified()));
            results.push_back(std::move(result));
        }

        value(socket, "SetTimeContext()", &localError);
        traceMdsLine(QString("east_time_context_batch count=%1 start=%2 delta=%3")
                         .arg(results.size())
                         .arg(start, 0, 'g', 12)
                         .arg(delta, 0, 'g', 12));
        if (error) {
            error->clear();
        }
        return results;
    }

QVector<SignalFetchResult> MdsIpClient::fetchThinBatchOnOpenSocket(QTcpSocket& socket, const QVector<NativeRequest>& requests, QString* error) const
{
        const QVector<ThinSampling> samplings = thinSamplings(socket, requests, error);
        if (samplings.size() != requests.size()) {
            if (requests.size() > 1) {
                const int middle = static_cast<int>(requests.size()) / 2;
                QVector<NativeRequest> left;
                QVector<NativeRequest> right;
                left.reserve(middle);
                right.reserve(requests.size() - middle);
                for (int i = 0; i < requests.size(); ++i) {
                    (i < middle ? left : right).push_back(requests[i]);
                }
                QVector<SignalFetchResult> splitResults = fetchThinBatchOnOpenSocket(socket, left, error);
                splitResults += fetchThinBatchOnOpenSocket(socket, right, error);
                return splitResults;
            }
            return {};
        }

        QStringList parts;
        parts.reserve(requests.size() * 2);
        int expectedValues = 0;

        for (int i = 0; i < requests.size(); ++i) {
            const NativeRequest& request = requests[i];
            const ThinSampling& sampling = samplings[i];
            if (sampling.sampledCount <= 0) {
                return {};
            }

            const QString xSource = request.sig.xExpr.trimmed().isEmpty()
                                        ? QString("dim_of(%1)").arg(request.sig.yExpr)
                                        : request.sig.xExpr.trimmed();
            parts.push_back(QString("data(%1)[1:*:%2]").arg(xSource).arg(sampling.step));
            parts.push_back(QString("data(%1)[1:*:%2]").arg(request.sig.yExpr).arg(sampling.step));
            expectedValues += sampling.sampledCount * 2;
        }

        const QString expr = QString("( _jscope_0 = ([%1]), fs_float(_jscope_0))").arg(parts.join(","));
        const QVector<double> values = numericValue(socket, expr, error);
        if (values.size() < expectedValues) {
            if (requests.size() > 1) {
                const int middle = static_cast<int>(requests.size()) / 2;
                QVector<NativeRequest> left;
                QVector<NativeRequest> right;
                left.reserve(middle);
                right.reserve(requests.size() - middle);
                for (int i = 0; i < requests.size(); ++i) {
                    (i < middle ? left : right).push_back(requests[i]);
                }
                QVector<SignalFetchResult> splitResults = fetchThinBatchOnOpenSocket(socket, left, error);
                splitResults += fetchThinBatchOnOpenSocket(socket, right, error);
                return splitResults;
            }
            return {};
        }

        QVector<SignalFetchResult> results;
        results.reserve(requests.size());
        int offset = 0;
        for (int i = 0; i < requests.size(); ++i) {
            const int n = samplings[i].sampledCount;
            SignalFetchResult result;
            result.loadedIndex = requests[i].loadedIndex;
            result.series = makeSeriesFromAdjacentSegments(
                normalizedMdsSignal(requests[i].sig.yExpr),
                values,
                offset,
                n,
                requests[i].maxPoints);
            if (!result.series.hasData()) {
                return {};
            }
            results.push_back(std::move(result));
            offset += n * 2;
        }
        return results;
    }
} // namespace mds_client_internal
