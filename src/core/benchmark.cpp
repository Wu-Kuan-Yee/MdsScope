// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.h"
#include "mds_client.h"

int runMdsScopeBenchmark(const QString& configPath,
                         DataReadMode readMode,
                         const QString& shotOverride,
                         bool summaryOnly,
                         bool prewarm)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QString tracePath = QDir::temp().filePath("mdsscope_mds_trace.log");
    QFile::remove(tracePath);
    qputenv("MDSSCOPE_MDS_TRACE", "1");

    LayoutConfig config = parseEnvironment(configPath);
    if (!shotOverride.trimmed().isEmpty()) {
        for (auto& column : config.columns) {
            for (PlotSpec& plot : column) {
                plot.shot = shotOverride.trimmed();
                for (SignalSpec& sig : plot.signalSpecs) {
                    sig.shot.clear();
                }
            }
        }
    }
    config = expandedShotLayout(config);
    int plotCount = 0;
    int signalCount = 0;
    QSet<QString> groups;
    for (const auto& column : config.columns) {
        plotCount += column.size();
        for (const PlotSpec& plot : column) {
            for (const SignalSpec& sig : plot.signalSpecs) {
                if (sig.hidden) {
                    continue;
                }
                ++signalCount;
                const QString shot = effectiveSignalShot(plot, sig);
                if (!shot.isEmpty() && !sig.serverIp.isEmpty() && !sig.experiment.isEmpty()) {
                    groups.insert(sig.serverIp.trimmed() + "|" + sig.experiment.trimmed() + "|" + shot);
                }
            }
        }
    }

    if (plotCount == 0 || signalCount == 0) {
        err << "Benchmark config has no plots/signals: " << configPath << Qt::endl;
        return 2;
    }

    struct Arrival {
        qint64 ms = 0;
        int column = -1;
        int row = -1;
        int signal = -1;
        QString shot;
        QString name;
        int points = 0;
        QString error;
    };

    QVector<Arrival> arrivals;
    arrivals.reserve(signalCount);
    QMutex arrivalMutex;
    QElapsedTimer timer;

    if (prewarm) {
        warmMdsConnections(config);
    }

    timer.start();
    auto loaded = fetchMdsSignals(config, readMode, [&](const LoadedSignal& item) {
        QMutexLocker locker(&arrivalMutex);
        Arrival arrival;
        arrival.ms = timer.elapsed();
        arrival.column = item.column;
        arrival.row = item.row;
        arrival.signal = item.signal;
        arrival.shot = item.shot;
        arrival.name = item.series.name;
        arrival.points = item.series.pointCount();
        arrival.error = item.series.error;
        arrivals.push_back(std::move(arrival));
    });

    if (!summaryOnly) {
        out << "Benchmark config: " << configPath << Qt::endl;
        if (!shotOverride.trimmed().isEmpty()) {
            out << "Shot override: " << shotOverride.trimmed() << Qt::endl;
        }
        out << "Mode: " << (readMode == DataReadMode::Full ? "full" : "thin")
            << ", plots=" << plotCount
            << ", signals=" << signalCount
            << ", groups=" << groups.size() << Qt::endl;
    }

    const qint64 totalMs = timer.elapsed();

    int okCount = 0;
    int emptyCount = 0;
    int failedCount = 0;
    qint64 totalPoints = 0;
    for (const LoadedSignal& item : loaded) {
        totalPoints += item.series.pointCount();
        if (!item.series.error.isEmpty()) {
            ++failedCount;
        } else if (!item.series.hasData()) {
            ++emptyCount;
        } else {
            ++okCount;
        }
    }

    qint64 lastArrivalMs = 0;
    {
        QMutexLocker locker(&arrivalMutex);
        for (const Arrival& arrival : arrivals) {
            lastArrivalMs = std::max(lastArrivalMs, arrival.ms);
        }
    }

    if (summaryOnly) {
        out << QFileInfo(configPath).fileName()
            << "\tms=" << totalMs
            << "\tok=" << okCount
            << "\tempty=" << emptyCount
            << "\tfailed=" << failedCount
            << "\tplots=" << plotCount
            << "\tsignals=" << signalCount
            << "\tgroups=" << groups.size()
            << "\tpoints=" << totalPoints << Qt::endl;
        return failedCount == signalCount ? 1 : 0;
    }

    out << "Total fetch: " << totalMs << " ms"
        << ", last streamed signal: " << lastArrivalMs << " ms"
        << ", ok=" << okCount
        << ", empty=" << emptyCount
        << ", failed=" << failedCount
        << ", points=" << totalPoints << Qt::endl;

    QVector<Arrival> arrivalSnapshot;
    {
        QMutexLocker locker(&arrivalMutex);
        arrivalSnapshot = arrivals;
    }
    std::sort(arrivalSnapshot.begin(), arrivalSnapshot.end(), [](const Arrival& a, const Arrival& b) {
        return a.ms > b.ms;
    });

    out << "Last arrivals:" << Qt::endl;
    for (int i = 0; i < std::min(10, static_cast<int>(arrivalSnapshot.size())); ++i) {
        const Arrival& item = arrivalSnapshot[i];
        out << "  " << item.ms << " ms"
            << " [" << item.column + 1 << "," << item.row + 1 << "," << item.signal + 1 << "]"
            << " shot=" << item.shot
            << " points=" << item.points
            << " y=" << item.name;
        if (!item.error.isEmpty()) {
            out << " error=" << item.error.simplified();
        }
        out << Qt::endl;
    }

    struct TraceSignal {
        qint64 ms = 0;
        QString shot;
        QString tree;
        QString y;
        int points = 0;
        QString error;
    };
    QVector<TraceSignal> traceSignals;
    QFile trace(tracePath);
    if (trace.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&trace);
        const QRegularExpression signalRe(
            R"(^signal_ms=(\d+) shot=(\S+) tree=(\S+) y=(.*?) points=(\d+) error=(.*)$)");
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const QRegularExpressionMatch match = signalRe.match(line);
            if (!match.hasMatch()) {
                continue;
            }
            TraceSignal item;
            item.ms = match.captured(1).toLongLong();
            item.shot = match.captured(2);
            item.tree = match.captured(3);
            item.y = match.captured(4);
            item.points = match.captured(5).toInt();
            item.error = match.captured(6).simplified();
            traceSignals.push_back(std::move(item));
        }
    }
    std::sort(traceSignals.begin(), traceSignals.end(), [](const TraceSignal& a, const TraceSignal& b) {
        return a.ms > b.ms;
    });

    out << "Slowest signal fetches:" << Qt::endl;
    for (int i = 0; i < std::min(20, static_cast<int>(traceSignals.size())); ++i) {
        const TraceSignal& item = traceSignals[i];
        out << "  " << item.ms << " ms"
            << " shot=" << item.shot
            << " tree=" << item.tree
            << " points=" << item.points
            << " y=" << item.y;
        if (!item.error.isEmpty()) {
            out << " error=" << item.error;
        }
        out << Qt::endl;
    }
    out << "Trace: " << tracePath << Qt::endl;
    return failedCount == 0 ? 0 : 1;
}

void shutdownMdsScopeWorkers()
{
    QVector<QFuture<void>> futures;
    futures.reserve(16);
    for (int i = 0; i < 16; ++i) {
        futures.push_back(QtConcurrent::run([] {
            clearMdsCurrentThreadConnections();
        }));
    }
    for (auto& future : futures) {
        future.waitForFinished();
    }
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone(3000);
}
