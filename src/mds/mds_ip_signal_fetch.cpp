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

SignalSeries MdsIpClient::fetchSignalOnOpenSocket(QTcpSocket& socket,
                                     const PlotSpec& plot,
                                     const SignalSpec& sig,
                                     DataReadMode readMode,
                                     int maxPoints,
                                     const QHash<QString, UniformTimebase>* timebaseCache,
                                     QString* error) const
{
        QElapsedTimer timer;
        timer.start();
        SignalSeries result = fetchSignalOnOpenSocketImpl(socket, plot, sig, readMode, maxPoints, timebaseCache, error);
        traceMdsLine(QString("signal_ms=%1 shot=%2 tree=%3 y=%4 points=%5 error=%6")
                         .arg(timer.elapsed())
                         .arg(plot.shot)
                         .arg(sig.experiment)
                         .arg(sig.yExpr)
                         .arg(result.pointCount())
                         .arg(result.error.simplified()));
        return result;
    }

SignalSeries MdsIpClient::fetchSignalOnOpenSocketImpl(QTcpSocket& socket,
                                         const PlotSpec& plot,
                                         const SignalSpec& sig,
                                         DataReadMode readMode,
                                         int maxPoints,
                                         const QHash<QString, UniformTimebase>* timebaseCache,
                                         QString* error) const
{
        SignalSeries result;
        result.name = normalizedMdsSignal(sig.yExpr);

        ThinSampling sampling;
        UniformTimebase fastTimebase;
        const QString xExpr = sig.xExpr.trimmed();
        const bool serverSideThin = (readMode == DataReadMode::Thin || readMode == DataReadMode::Medium) && kUseServerSideThin;
        SignalSpec fastSig = sig;
        const ScaledSignalExpr scaledExpr = scaledSimpleSignalExpr(sig.yExpr);
        if (scaledExpr.valid) {
            fastSig.yExpr = scaledExpr.baseExpr;
        }
        int fullExpectedPoints = 0;
        if (readMode == DataReadMode::Full) {
            fullExpectedPoints = fullPointCountBestEffort(socket, fastSig.yExpr);
        }
        if (readMode == DataReadMode::Full && xExpr.isEmpty() && scaledExpr.valid && isEastTimebaseCandidate(plot.shot, fastSig)) {
            QString localError;
            const UniformTimebase timebase = eastUniformTimebase(socket, plot.shot, fastSig, &localError);
            if (timebase.valid) {
                QElapsedTimer waitTimer;
                waitTimer.start();
                auto largeGuard = fullLargeDownloadGuard(sig, fullExpectedPoints);
                if (largeGuard) {
                    traceMdsLine(QString("full_large_wait_ms=%1 shot=%2 tree=%3 y=%4 points=%5 limit=%6")
                                     .arg(waitTimer.elapsed())
                                     .arg(plot.shot, sig.experiment, sig.yExpr)
                                     .arg(fullExpectedPoints)
                                     .arg(fullLargeDownloadLimit()));
                }
                const Message yMessage = value(socket,
                                               QString("( _jscope_0 = (%1), fs_float(_jscope_0))").arg(fastSig.yExpr),
                                               &localError);
                result = makeSeriesUniformXFromMessage(result.name,
                                                       yMessage,
                                                       timebase.start,
                                                       timebase.step,
                                                       0,
                                                       &localError);
                if (result.hasData()) {
                    applySeriesScale(&result, sig.yExpr, scaledExpr.scale);
                    if (error) {
                        error->clear();
                    }
                    return result;
                }
                if (error) {
                    error->clear();
                }
            }
        }
        if (serverSideThin && xExpr.isEmpty() && scaledExpr.valid && isEastTimebaseCandidate(plot.shot, fastSig)) {
            QString savedError;
            SignalSeries saved = fetchSavedEastSignalOnOpenSocket(socket, plot, fastSig, maxPoints, &savedError);
            if (saved.hasData()) {
                applySeriesScale(&saved, sig.yExpr, scaledExpr.scale);
                if (error) {
                    error->clear();
                }
                return saved;
            }
            // For thin/preview: full-read envelope (min/max per bucket) preserves
            // spikes perfectly (0% loss) at ~1.8s/signal vs stride-sampling's ~1.3s
            // but 91% bucket miss rate. With 8-way parallel the wall-clock is ~6-7s
            // for 34 signals (vs Java's ~7s), faster than Java and spike-preserving.
            SignalSeries envelope = fetchEastFullEnvelopeSignalOnOpenSocket(socket, plot, fastSig, maxPoints, readMode, error);
            if (envelope.hasData()) {
                applySeriesScale(&envelope, sig.yExpr, scaledExpr.scale);
                return envelope;
            }
            if (error) {
                error->clear();
            }
            // SetTimeContext-based server resample is ~5x faster than length
            // sampling on segmented EAST nodes: it lets the server resample to the
            // requested time resolution instead of reading the full record and
            // striding ([1:*:step] forces a full 11M-point read, ~1.3s; the windowed
            // resample is ~0.25s). This is what Java's freq-mode path uses. Fall
            // back to length sampling only if the time-context read yields no data.
            SignalSeries contextual = fetchEastTimeContextSignalOnOpenSocket(socket, plot, fastSig, maxPoints, timebaseCache, error);
            if (contextual.hasData()) {
                applySeriesScale(&contextual, sig.yExpr, scaledExpr.scale);
                return contextual;
            }
            if (error) {
                error->clear();
            }
            SignalSeries sampled = fetchEastLengthSampledSignalOnOpenSocket(socket, plot, fastSig, maxPoints, error);
            if (sampled.hasData()) {
                applySeriesScale(&sampled, sig.yExpr, scaledExpr.scale);
                return sampled;
            }
            if (error) {
                error->clear();
            }
        }
        if (serverSideThin) {
            if (xExpr.isEmpty()) {
                const EastThinPlan plan = eastThinPlan(socket, plot.shot, sig, maxPoints, error);
                if (plan.valid) {
                    sampling = plan.sampling;
                    fastTimebase = plan.timebase;
                }
            }
            if (sampling.sampledCount <= 0) {
                sampling = thinSampling(socket, sig.yExpr, maxPoints, error);
            }
            if (sampling.sampledCount <= 0 && sig.yExpr.trimmed().startsWith('\\')) {
                result.error = error && !error->isEmpty() ? *error : "empty signal";
                return result;
            }
        }
        const int sampleStep = serverSideThin ? sampling.step : 1;
        const int displayMaxPoints = readMode == DataReadMode::Thin ? maxPoints : 0;
        if (kEnableCombinedSignalFetch && sampleStep > 1) {
            const QString xSource = sig.xExpr.trimmed().isEmpty()
                                        ? QString("dim_of(%1)").arg(sig.yExpr)
                                        : sig.xExpr.trimmed();
            const QString xyExpr = QString("( _jscope_0 = (data(%1)[1:*:%2]), _jscope_1 = (data(%3)[1:*:%2]), fs_float([_jscope_1, _jscope_0]))")
                                       .arg(sig.yExpr)
                                       .arg(sampleStep)
                                       .arg(xSource);
            const QVector<double> xy = numericValue(socket, xyExpr, error);
            if (xy.size() >= 2) {
                result = makeSeriesFromCombined(result.name, xy, displayMaxPoints);
                if (result.hasData()) {
                    return result;
                }
            }
        }

        const QString yExpr = sampleStep > 1
                                  ? QString("( _jscope_0 = (data(%1)[1:*:%2]), fs_float(_jscope_0))").arg(sig.yExpr).arg(sampleStep)
                                  : QString("( _jscope_0 = (%1), fs_float(_jscope_0))").arg(sig.yExpr);

        if (readMode == DataReadMode::Thin && xExpr.isEmpty()) {
            UniformTimebase timebase = fastTimebase;
            if (timebaseCache) {
                const UniformTimebase cached = timebaseCache->value(eastTimebaseKey(plot.shot, sig));
                if (cached.valid) {
                    timebase = cached;
                }
            }
            if (!timebase.valid) {
                timebase = eastUniformTimebase(socket, plot.shot, sig, error);
            }
            if (timebase.valid) {
                const double sampledStart = sampleStep > 1 ? timebase.start + timebase.step : timebase.start;
                const double sampledStep = timebase.step * static_cast<double>(sampleStep);
                const Message yMessage = value(socket, yExpr, error);
                result = makeSeriesUniformXFromMessage(result.name,
                                                       yMessage,
                                                       sampledStart,
                                                       sampledStep,
                                                       displayMaxPoints,
                                                       error);
                if (result.hasData() || (error && !error->isEmpty())) {
                    if (!result.hasData()) {
                        result.error = error->isEmpty() ? "empty signal" : *error;
                    }
                    return result;
                }
            }
        }

        std::unique_ptr<SemaphoreGuard> fullFallbackGuard;
        if (readMode == DataReadMode::Full) {
            QElapsedTimer waitTimer;
            waitTimer.start();
            fullFallbackGuard = fullLargeDownloadGuard(sig, fullExpectedPoints);
            if (fullFallbackGuard) {
                traceMdsLine(QString("full_large_wait_ms=%1 shot=%2 tree=%3 y=%4 points=%5 limit=%6")
                                 .arg(waitTimer.elapsed())
                                 .arg(plot.shot, sig.experiment, sig.yExpr)
                                 .arg(fullExpectedPoints)
                                 .arg(fullLargeDownloadLimit()));
            }
        }
        const QVector<double> y = numericValue(socket, yExpr, error);
        if (y.isEmpty()) {
            const QVector<double> alt = numericValue(socket, QString("data(%1)").arg(sig.yExpr), error);
            if (alt.isEmpty()) {
                result.error = error->isEmpty() ? "empty signal" : *error;
                return result;
            }
            return makeSeries(result.name, alt, QVector<double>{}, displayMaxPoints);
        }

        QVector<double> x;
        if (!xExpr.isEmpty()) {
            const QString sampledXExpr = sampleStep > 1
                                             ? QString("( _jscope_1 = (data(%1)[1:*:%2]), ft_float(_jscope_1))").arg(xExpr).arg(sampleStep)
                                             : QString("( _jscope_1 = (%1), ft_float(_jscope_1))").arg(xExpr);
            x = numericValue(socket, sampledXExpr, error);
        }
        if (x.isEmpty()) {
            if (xExpr.isEmpty()) {
                UniformTimebase timebase;
                if (timebaseCache) {
                    timebase = timebaseCache->value(eastTimebaseKey(plot.shot, sig));
                }
                if (!timebase.valid) {
                    timebase = eastUniformTimebase(socket, plot.shot, sig, error);
                }
                if (timebase.valid) {
                    const double sampledStart = sampleStep > 1 ? timebase.start + timebase.step : timebase.start;
                    const double sampledStep = timebase.step * static_cast<double>(sampleStep);
                    result = makeSeriesUniformX(result.name, y, sampledStart, sampledStep, displayMaxPoints);
                    if (result.hasData()) {
                        return result;
                    }
                }
            }
            const QString dimExpr = sampleStep > 1
                                        ? QString("( _jscope_1 = (data(dim_of(%1))[1:*:%2]), ft_float(_jscope_1))").arg(sig.yExpr).arg(sampleStep)
                                        : QString("( _jscope_1 = (dim_of(%1)), ft_float(_jscope_1))").arg(sig.yExpr);
            x = numericValue(socket, dimExpr, error);
        }
        result = makeSeries(result.name, y, x, displayMaxPoints);
        if (!result.hasData()) {
            result.error = "no numeric points";
        }
        return result;
    }

SignalSeries MdsIpClient::fetchEastFullEnvelopeSignalOnOpenSocket(QTcpSocket& socket,
                                                     const PlotSpec& plot,
                                                     const SignalSpec& sig,
                                                     int maxPoints,
                                                     DataReadMode readMode,
                                                     QString* error) const
{
        SignalSeries result;
        result.name = normalizedMdsSignal(sig.yExpr);
        if (maxPoints <= 0) {
            return result;
        }

        QString localError;
        const EastThinPlan plan = eastThinPlan(socket, plot.shot, sig, maxPoints, &localError);
        if (!plan.valid || plan.sampling.sourceCount <= 0 || !plan.timebase.valid) {
            return result;
        }

        // Only use envelope when sampling ratio is high enough.
        static constexpr int kOversample = 10;
        if (plan.sampling.step <= 4) {
            return result;
        }

        const int oversampledPoints = maxPoints * kOversample;
        const double start = plan.timebase.start;
        const double end = start + static_cast<double>(plan.sampling.sourceCount - 1) * plan.timebase.step;

        if (readMode == DataReadMode::Medium) {
            // Medium mode: stride sampling at high resolution (real measured values)
            const int fineStep = std::max(1, plan.sampling.sourceCount / oversampledPoints);
            const QString sampledExpr = QString("( _jscope_0 = (data(%1)[1:*:%2]), fs_float(_jscope_0))")
                                            .arg(sig.yExpr.trimmed()).arg(fineStep);
            const double sampledStart = start + plan.timebase.step; // [1:*:step] skips index 0
            const double sampledStep = plan.timebase.step * static_cast<double>(fineStep);

            const Message yMessage = value(socket, sampledExpr, &localError);
            result = makeSeriesUniformXFromMessage(result.name, yMessage, sampledStart, sampledStep, oversampledPoints, &localError);
            if (!result.hasData()) {
                if (error) error->clear();
                return result;
            }
            traceMdsLine(QString("east_envelope_signal shot=%1 tree=%2 y=%3 method=stride points=%4")
                             .arg(plot.shot, sig.experiment, sig.yExpr)
                             .arg(result.pointCount()));
        } else {
            // Thin mode: SetTimeContext averaging (fast but spike amplitude slightly attenuated)
            const double delta = (end - start) / static_cast<double>(oversampledPoints - 1);
            value(socket,
                  QString("SetTimeContext(%1,%2,%3)")
                      .arg(start, 0, 'g', 12)
                      .arg(end,   0, 'g', 12)
                      .arg(delta, 0, 'g', 12),
                  &localError);
            if (!localError.isEmpty()) {
                value(socket, "SetTimeContext()", &localError);
                return result;
            }

            const Message yMessage = value(socket,
                                           QString("( _jscope_0 = (%1), fs_float(_jscope_0))").arg(sig.yExpr.trimmed()),
                                           &localError);
            value(socket, "SetTimeContext()", &localError);

            result = makeSeriesUniformXFromMessage(result.name, yMessage, start, delta, oversampledPoints, &localError);
            if (!result.hasData()) {
                if (error) error->clear();
                return result;
            }
            traceMdsLine(QString("east_envelope_signal shot=%1 tree=%2 y=%3 method=STC points=%4")
                             .arg(plot.shot, sig.experiment, sig.yExpr)
                             .arg(result.pointCount()));
        }
        if (error) error->clear();
        return result;
    }

SignalSeries MdsIpClient::fetchEastLengthSampledSignalOnOpenSocket(QTcpSocket& socket,
                                                      const PlotSpec& plot,
                                                      const SignalSpec& sig,
                                                      int maxPoints,
                                                      QString* error) const
{
        SignalSeries result;
        result.name = normalizedMdsSignal(sig.yExpr);
        if (maxPoints <= 0) {
            return result;
        }

        QString localError;
        const EastThinPlan plan = eastThinPlan(socket, plot.shot, sig, maxPoints, &localError);
        if (!plan.valid || plan.sampling.sampledCount <= 0 || !plan.timebase.valid) {
            if (error) {
                error->clear();
            }
            return result;
        }

        const int sampleStep = plan.sampling.step;
        const QString yExpr = sampleStep > 1
                                  ? QString("( _jscope_0 = (data(%1)[1:*:%2]), fs_float(_jscope_0))").arg(sig.yExpr).arg(sampleStep)
                                  : QString("( _jscope_0 = (%1), fs_float(_jscope_0))").arg(sig.yExpr);
        const double sampledStart = sampleStep > 1 ? plan.timebase.start + plan.timebase.step : plan.timebase.start;
        const double sampledStep = plan.timebase.step * static_cast<double>(sampleStep);
        const Message yMessage = value(socket, yExpr, &localError);
        result = makeSeriesUniformXFromMessage(result.name,
                                               yMessage,
                                               sampledStart,
                                               sampledStep,
                                               maxPoints,
                                               &localError);
        if (!result.hasData()) {
            if (error) {
                error->clear();
            }
            return {};
        }

        traceMdsLine(QString("east_length_sampled_signal shot=%1 tree=%2 y=%3 points=%4 step=%5")
                         .arg(plot.shot, sig.experiment, sig.yExpr)
                         .arg(result.pointCount())
                         .arg(sampleStep));
        if (error) {
            error->clear();
        }
        return result;
    }

SignalSeries MdsIpClient::fetchEastTimeContextSignalOnOpenSocket(QTcpSocket& socket,
                                                    const PlotSpec& plot,
                                                    const SignalSpec& sig,
                                                    int maxPoints,
                                                    const QHash<QString, UniformTimebase>*,
                                                    QString* error) const
{
        SignalSeries result;
        result.name = normalizedMdsSignal(sig.yExpr);
        if (maxPoints <= 0) {
            return result;
        }

        QString localError;
        const EastThinPlan plan = eastThinPlan(socket, plot.shot, sig, maxPoints, &localError);
        if (!plan.valid || plan.sampling.sourceCount <= 0 || !plan.timebase.valid) {
            if (error) {
                error->clear();
            }
            return result;
        }

        double start = plan.timebase.start;
        double end = start + static_cast<double>(plan.sampling.sourceCount - 1) * plan.timebase.step;
        double delta = plan.timebase.step * static_cast<double>(plan.sampling.step);
        if (plot.customXRange
            && std::isfinite(plot.xmin)
            && std::isfinite(plot.xmax)
            && plot.xmax > plot.xmin) {
            start = plot.xmin;
            end = plot.xmax;
            delta = (end - start) / static_cast<double>(maxPoints);
        }
        if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(delta) || delta <= 0.0 || end <= start) {
            if (error) {
                error->clear();
            }
            return result;
        }
        value(socket,
              QString("SetTimeContext(%1,%2,%3)")
                  .arg(start, 0, 'g', 12)
                  .arg(end, 0, 'g', 12)
                  .arg(delta, 0, 'g', 12),
              &localError);
        if (!localError.isEmpty()) {
            if (error) {
                error->clear();
            }
            return result;
        }

        const Message yMessage = value(socket,
                                       QString("( _jscope_0 = (%1), fs_float(_jscope_0))").arg(sig.yExpr),
                                       &localError);
        value(socket, "SetTimeContext()", &localError);

        result = makeSeriesUniformXFromMessage(result.name, yMessage, start, delta, maxPoints, error);
        if (!result.hasData()) {
            if (error) {
                error->clear();
            }
            return {};
        }
        traceMdsLine(QString("east_time_context_signal shot=%1 tree=%2 y=%3 points=%4 start=%5 delta=%6")
                         .arg(plot.shot, sig.experiment, sig.yExpr)
                         .arg(result.pointCount())
                         .arg(start, 0, 'g', 12)
                         .arg(delta, 0, 'g', 12));
        if (error) {
            error->clear();
        }
        return result;
    }

SignalSeries MdsIpClient::fetchSavedEastSignalOnOpenSocket(QTcpSocket& socket,
                                              const PlotSpec& plot,
                                              const SignalSpec& sig,
                                              int maxPoints,
                                              QString* error) const
{
        SignalSeries result;
        result.name = normalizedMdsSignal(sig.yExpr);
        const QString savedExpr = sig.yExpr.trimmed() + "_s";
        QString localError;
        const QString yExpr = QString("( _jscope_0 = (%1), fs_float(_jscope_0))").arg(savedExpr);
        const QVector<double> y = numericValue(socket, yExpr, &localError);
        if (y.isEmpty()) {
            if (error) {
                error->clear();
            }
            return result;
        }

        const QString xExpr = QString("( _jscope_1 = (dim_of(%1)), ft_float(_jscope_1))").arg(savedExpr);
        const QVector<double> x = numericValue(socket, xExpr, &localError);
        result = makeSeries(result.name, y, x, maxPoints);
        if (!result.hasData()) {
            if (error) {
                error->clear();
            }
            return {};
        }

        traceMdsLine(QString("east_saved_signal shot=%1 tree=%2 y=%3 saved=%4 points=%5")
                         .arg(plot.shot, sig.experiment, sig.yExpr, savedExpr)
                         .arg(result.pointCount()));
        if (error) {
            error->clear();
        }
        return result;
    }
} // namespace mds_client_internal
