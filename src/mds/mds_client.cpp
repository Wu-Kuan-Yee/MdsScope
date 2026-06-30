#include "mdsscope_internal.h"
#include "mds_client.h"

namespace {
class MdsIpClient {
    struct NativeRequest {
        int loadedIndex = -1;
        int column = -1;
        int row = -1;
        int signal = -1;
        QString shot;
        PlotSpec plot;
        SignalSpec sig;
        int maxPoints = 2000;
    };

    struct SignalFetchResult {
        int loadedIndex = -1;
        SignalSeries series;
    };

    struct ThinSampling {
        int sourceCount = 0;
        int step = 1;
        int sampledCount = 0;
    };

    struct UniformTimebase {
        bool valid = false;
        double start = 0.0;
        double step = 1.0;
    };

    struct EastThinPlan {
        bool valid = false;
        ThinSampling sampling;
        UniformTimebase timebase;
    };

    struct ScaledSignalExpr {
        bool valid = false;
        QString baseExpr;
        double scale = 1.0;
    };

    struct CachedMdsConnection {
        QString key;
        QString currentTree;
        QString currentShot;
        std::unique_ptr<QTcpSocket> socket;
    };

public:
    using ResultCallback = std::function<void(const LoadedSignal&)>;

    explicit MdsIpClient(DataReadMode readMode = DataReadMode::Thin, ResultCallback callback = {})
        : readMode_(readMode), callback_(std::move(callback))
    {
    }

    static void clearCurrentThreadConnections()
    {
        auto& connections = threadLocalConnections();
        for (const auto& connection : connections) {
            if (connection && connection->socket) {
                connection->socket->disconnectFromHost();
                if (connection->socket->state() != QAbstractSocket::UnconnectedState) {
                    connection->socket->waitForDisconnected(200);
                }
                connection->socket->abort();
            }
        }
        connections.clear();
    }

    QVector<LoadedSignal> fetchAll(const LayoutConfig& snapshot) const
    {
        QVector<LoadedSignal> loaded;
        QHash<QString, QVector<NativeRequest>> groups;
        QHash<int, NativeRequest> requestsByLoadedIndex;

        for (int c = 0; c < snapshot.columns.size(); ++c) {
            for (int r = 0; r < snapshot.columns[c].size(); ++r) {
                const PlotSpec& plot = snapshot.columns[c][r];
                for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                    const SignalSpec& sig = plot.signalSpecs[s];
                    if (sig.hidden) {
                        continue;
                    }
                    const QString shot = effectiveSignalShot(plot, sig);
                    LoadedSignal item;
                    item.column = c;
                    item.row = r;
                    item.signal = s;
                    item.shot = shot;
                    item.series.name = normalizedMdsSignal(sig.yExpr);

                    if (sig.serverIp.isEmpty() || sig.experiment.isEmpty() || shot.isEmpty() || sig.yExpr.isEmpty()) {
                        item.series.error = "missing server/tree/shot/signal";
                        loaded.push_back(std::move(item));
                        continue;
                    }

                    const int loadedIndex = loaded.size();
                    loaded.push_back(std::move(item));
                    NativeRequest request;
                    request.loadedIndex = loadedIndex;
                    request.column = c;
                    request.row = r;
                    request.signal = s;
                    request.shot = shot;
                    request.plot = plot;
                    request.plot.shot = shot;
                    request.sig = sig;
                    request.maxPoints = maxPointsForSignal(plot, sig);
                    requestsByLoadedIndex.insert(loadedIndex, request);
                    groups[groupKey(request.plot, sig)].push_back(std::move(request));
                }
            }
        }

        QVector<QVector<NativeRequest>> chunks;
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            appendConnectionChunks(it.value(), &chunks);
        }
        prioritizeConnectionChunks(&chunks);

        constexpr int kMaxGlobalSockets = 16;
        for (int start = 0; start < chunks.size(); start += kMaxGlobalSockets) {
            const int count = std::min(kMaxGlobalSockets, static_cast<int>(chunks.size()) - start);
            QVector<QFuture<QVector<SignalFetchResult>>> futures;
            futures.reserve(count);
            for (int i = 0; i < count; ++i) {
                QVector<NativeRequest> chunk = std::move(chunks[start + i]);
                futures.push_back(QtConcurrent::run([this, chunk = std::move(chunk)] {
                    return fetchGroupResults(chunk);
                }));
            }
            for (auto& future : futures) {
                applyFetchResults(future.result(), &loaded);
            }
        }
        retryTransientFailures(requestsByLoadedIndex, &loaded);
        return loaded;
    }

    void warmConnections(const LayoutConfig& snapshot) const
    {
        QHash<QString, QVector<NativeRequest>> groups;
        for (int c = 0; c < snapshot.columns.size(); ++c) {
            for (int r = 0; r < snapshot.columns[c].size(); ++r) {
                const PlotSpec& plot = snapshot.columns[c][r];
                for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                    const SignalSpec& sig = plot.signalSpecs[s];
                    if (sig.hidden) {
                        continue;
                    }
                    const QString shot = effectiveSignalShot(plot, sig);
                    if (sig.serverIp.isEmpty() || sig.experiment.isEmpty() || shot.isEmpty() || sig.yExpr.isEmpty()) {
                        continue;
                    }
                    NativeRequest request;
                    request.loadedIndex = 0;
                    request.column = c;
                    request.row = r;
                    request.signal = s;
                    request.shot = shot;
                    request.plot = plot;
                    request.plot.shot = shot;
                    request.sig = sig;
                    request.maxPoints = maxPointsForSignal(plot, sig);
                    groups[groupKey(request.plot, sig)].push_back(std::move(request));
                }
            }
        }

        QVector<QVector<NativeRequest>> chunks;
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            appendConnectionChunks(it.value(), &chunks);
        }
        prioritizeConnectionChunks(&chunks);

        constexpr int kMaxGlobalSockets = 16;
        for (int start = 0; start < chunks.size(); start += kMaxGlobalSockets) {
            const int count = std::min(kMaxGlobalSockets, static_cast<int>(chunks.size()) - start);
            const int warmCount = std::max(count, kMaxGlobalSockets);
            QVector<QFuture<void>> futures;
            futures.reserve(warmCount);
            for (int i = 0; i < warmCount; ++i) {
                QVector<NativeRequest> warmChunk;
                warmChunk.push_back(chunks[start + (i % count)].front());
                futures.push_back(QtConcurrent::run([this, warmChunk = std::move(warmChunk)] {
                    fetchGroupResults(warmChunk);
                }));
            }
            for (auto& future : futures) {
                future.waitForFinished();
            }
        }
    }

    SignalSeries fetch(const PlotSpec& plot, const SignalSpec& sig) const
    {
        SignalSeries result;
        result.name = normalizedMdsSignal(sig.yExpr);
        const QString shot = effectiveSignalShot(plot, sig);
        if (sig.serverIp.isEmpty() || sig.experiment.isEmpty() || shot.isEmpty() || sig.yExpr.isEmpty()) {
            result.error = "missing server/tree/shot/signal";
            return result;
        }
        NativeRequest request;
        request.loadedIndex = 0;
        request.plot = plot;
        request.plot.shot = shot;
        request.shot = shot;
        request.sig = sig;
        QVector<NativeRequest> requests = {request};
        QVector<LoadedSignal> loaded(1);
        loaded[0].series = result;
        fetchGroup(requests, &loaded);
        return loaded[0].series;
    }

private:
    DataReadMode readMode_ = DataReadMode::Thin;
    ResultCallback callback_;
    static constexpr bool kEnableMultiSignalBatch = false;
    static constexpr bool kEnableCombinedSignalFetch = false;
    static constexpr bool kUseServerSideThin = true;
    static constexpr bool kEnableEastTimeContextBatch = false;
    static constexpr bool kEnableEastTimebasePrefetch = false;
    static constexpr bool kEnablePipelinedDirectFetch = false;
    static constexpr bool kEnableEastThinPipeline = false;
    static constexpr bool kSetServerResampleMode = false;
    static constexpr bool kSetTreeDefault = false;
    static constexpr int kMaxConnectionsPerGroup = 8;
    static constexpr int kDefaultFullLargeSignalPoints = 8'000'000;

    class SemaphoreGuard {
    public:
        explicit SemaphoreGuard(QSemaphore* semaphore)
            : semaphore_(semaphore)
        {
            if (semaphore_) {
                semaphore_->acquire();
            }
        }

        ~SemaphoreGuard()
        {
            if (semaphore_) {
                semaphore_->release();
            }
        }

        SemaphoreGuard(const SemaphoreGuard&) = delete;
        SemaphoreGuard& operator=(const SemaphoreGuard&) = delete;

    private:
        QSemaphore* semaphore_ = nullptr;
    };

    static QString groupKey(const PlotSpec& plot, const SignalSpec& sig)
    {
        QString server = sig.serverIp.trimmed();
        if (!server.contains(':')) {
            server += ":8000";
        }
        return server + "|" + sig.experiment.trimmed() + "|" + plot.shot.trimmed();
    }

    static int maxPointsForSignal(const PlotSpec& plot, const SignalSpec& sig)
    {
        const int configured = plot.extractionPoints > 0 ? plot.extractionPoints : 2000;
        return configured;
    }

    static QString serverHost(QString server)
    {
        server = server.trimmed();
        const int colon = server.lastIndexOf(':');
        if (colon > 0) {
            return server.left(colon);
        }
        return server;
    }

    static int serverPort(const QString& server)
    {
        const int colon = server.lastIndexOf(':');
        if (colon > 0) {
            bool ok = false;
            const int port = server.mid(colon + 1).toInt(&ok);
            if (ok && port > 0) {
                return port;
            }
        }
        return kMdsPort;
    }

    static QString serverKey(const SignalSpec& sig)
    {
        return serverHost(sig.serverIp) + ":" + QString::number(serverPort(sig.serverIp));
    }

    static int fullLargeDownloadLimit()
    {
        bool ok = false;
        int configured = qEnvironmentVariableIntValue("MDSSCOPE_FULL_LARGE_LIMIT", &ok);
        if (!ok) {
            configured = qEnvironmentVariableIntValue("WEBSCOPE_FULL_LARGE_LIMIT", &ok);
        }
        if (ok) {
            return std::clamp(configured, 1, 8);
        }
        return 2;
    }

    static int fullLargeSignalPointThreshold()
    {
        bool ok = false;
        int configured = qEnvironmentVariableIntValue("MDSSCOPE_FULL_LARGE_POINTS", &ok);
        if (!ok) {
            configured = qEnvironmentVariableIntValue("WEBSCOPE_FULL_LARGE_POINTS", &ok);
        }
        if (ok) {
            return std::clamp(configured, 100'000, 100'000'000);
        }
        return kDefaultFullLargeSignalPoints;
    }

    static QSemaphore* fullLargeDownloadSemaphore(const SignalSpec& sig)
    {
        static QMutex mutex;
        static QHash<QString, QSemaphore*> semaphores;
        const QString key = serverKey(sig);
        QMutexLocker locker(&mutex);
        QSemaphore* semaphore = semaphores.value(key, nullptr);
        if (!semaphore) {
            semaphore = new QSemaphore(fullLargeDownloadLimit());
            semaphores.insert(key, semaphore);
        }
        return semaphore;
    }

    static int fullPointCountBestEffort(QTcpSocket& socket, const QString& yExpr)
    {
        QString localError;
        const int count = intValue(socket, QString("size(%1)").arg(yExpr), &localError);
        return count > 0 ? count : 0;
    }

    static std::unique_ptr<SemaphoreGuard> fullLargeDownloadGuard(const SignalSpec& sig, int pointCount)
    {
        if (pointCount < fullLargeSignalPointThreshold()) {
            return {};
        }
        return std::make_unique<SemaphoreGuard>(fullLargeDownloadSemaphore(sig));
    }

    static CachedMdsConnection* threadLocalConnection(const SignalSpec& sig)
    {
        auto& connections = threadLocalConnections();
        const QString key = serverKey(sig);
        for (const auto& connection : connections) {
            if (connection->key == key) {
                return connection.get();
            }
        }
        auto connection = std::make_unique<CachedMdsConnection>();
        connection->key = key;
        connections.push_back(std::move(connection));
        return connections.back().get();
    }

    static std::vector<std::unique_ptr<CachedMdsConnection>>& threadLocalConnections()
    {
        thread_local std::vector<std::unique_ptr<CachedMdsConnection>> connections;
        return connections;
    }

    static void resetConnection(CachedMdsConnection* connection)
    {
        if (!connection) {
            return;
        }
        if (connection->socket) {
            connection->socket->abort();
            connection->socket.reset();
        }
        connection->currentTree.clear();
        connection->currentShot.clear();
    }

    static int connectionCountForGroup(int requestCount)
    {
        if (requestCount >= 16) {
            return kMaxConnectionsPerGroup;
        }
        if (requestCount >= 8) {
            return std::min(8, kMaxConnectionsPerGroup);
        }
        if (requestCount >= 4) {
            return std::min(4, kMaxConnectionsPerGroup);
        }
        if (requestCount >= 2) {
            return 2;
        }
        return 1;
    }

    static bool isLikelyHeavySignal(const SignalSpec& sig)
    {
        const QString experiment = sig.experiment.trimmed().toLower();
        const QString y = normalizedMdsSignal(sig.yExpr).toLower();
        return experiment == "east" && (y.startsWith("\\hrs") || y.startsWith("hrs"));
    }

    static bool prefersEastTimeContext(const QString& baseExpr)
    {
        const QString y = baseExpr.trimmed().toLower();
        return y.startsWith("\\hrs")
               || y.startsWith("\\ssnpa");
    }

    static bool chunkHasLikelyHeavySignal(const QVector<NativeRequest>& chunk)
    {
        return std::any_of(chunk.begin(), chunk.end(), [](const NativeRequest& request) {
            return isLikelyHeavySignal(request.sig);
        });
    }

    static void prioritizeConnectionChunks(QVector<QVector<NativeRequest>>* chunks)
    {
        std::stable_sort(chunks->begin(), chunks->end(), [](const QVector<NativeRequest>& a, const QVector<NativeRequest>& b) {
            const bool aHeavy = chunkHasLikelyHeavySignal(a);
            const bool bHeavy = chunkHasLikelyHeavySignal(b);
            if (aHeavy != bHeavy) {
                return aHeavy;
            }
            return a.size() > b.size();
        });
    }

    static void appendConnectionChunks(const QVector<NativeRequest>& requests, QVector<QVector<NativeRequest>>* chunks)
    {
        if (requests.isEmpty()) {
            return;
        }

        int heavyCount = 0;
        for (const NativeRequest& request : requests) {
            if (isLikelyHeavySignal(request.sig)) {
                ++heavyCount;
            }
        }

        QVector<NativeRequest> normal;
        normal.reserve(requests.size());
        const bool splitHeavySignals = heavyCount > 0 && heavyCount <= 8;
        for (const NativeRequest& request : requests) {
            if (splitHeavySignals && isLikelyHeavySignal(request.sig)) {
                chunks->push_back(QVector<NativeRequest>{request});
            } else {
                normal.push_back(request);
            }
        }
        if (normal.isEmpty()) {
            return;
        }

        const QString experiment = normal.front().sig.experiment.trimmed().toLower();
        if (experiment == "east" && normal.size() > 16) {
            int bucketCount = 2;
            if (!splitHeavySignals && normal.size() > 48) {
                bucketCount = 6;
            } else if (!splitHeavySignals && normal.size() > 32) {
                bucketCount = 4;
            }
            QVector<QVector<NativeRequest>> buckets(bucketCount);
            for (int i = 0; i < normal.size(); ++i) {
                buckets[i % bucketCount].push_back(normal[i]);
            }
            for (QVector<NativeRequest>& bucket : buckets) {
                if (!bucket.isEmpty()) {
                    chunks->push_back(std::move(bucket));
                }
            }
            return;
        }

        chunks->push_back(std::move(normal));
    }

    void fetchGroup(const QVector<NativeRequest>& requests, QVector<LoadedSignal>* loaded) const
    {
        applyFetchResults(fetchGroupResults(requests), loaded);
    }

    QVector<SignalFetchResult> fetchGroupResults(const QVector<NativeRequest>& requests) const
    {
        if (requests.isEmpty()) {
            return {};
        }

        QVector<SignalFetchResult> results;
        results.reserve(requests.size());
        const PlotSpec& firstPlot = requests.front().plot;
        const SignalSpec& firstSig = requests.front().sig;
        QElapsedTimer groupTimer;
        groupTimer.start();
        QElapsedTimer stageTimer;
        CachedMdsConnection* cached = threadLocalConnection(firstSig);
        QTcpSocket* socketPtr = nullptr;
        QString error;
        qint64 connectMs = 0;
        qint64 handshakeMs = 0;
        qint64 openMs = 0;
        bool usedCachedConnection = false;
        bool reusedOpenTree = false;
        bool usedJavaOpenFallback = false;

        for (int attempt = 0; attempt < 2; ++attempt) {
            const bool canReuseSocket = cached && cached->socket && cached->socket->state() == QAbstractSocket::ConnectedState;
            usedCachedConnection = canReuseSocket;
            if (!canReuseSocket) {
                resetConnection(cached);
                cached->socket = std::make_unique<QTcpSocket>();
                stageTimer.restart();
                cached->socket->connectToHost(serverHost(firstSig.serverIp), serverPort(firstSig.serverIp));
                if (!cached->socket->waitForConnected(kNetworkTimeoutMs)) {
                    error = cached->socket->errorString();
                    resetConnection(cached);
                    break;
                }
                connectMs = stageTimer.elapsed();

                stageTimer.restart();
                if (!handshake(*cached->socket, &error)) {
                    error = error.isEmpty() ? "MDS handshake failed" : error;
                    resetConnection(cached);
                    break;
                }
                handshakeMs = stageTimer.elapsed();
            }

            socketPtr = cached->socket.get();
            reusedOpenTree = cached->currentTree.compare(firstSig.experiment.trimmed(), Qt::CaseInsensitive) == 0
                             && cached->currentShot == firstPlot.shot.trimmed();
            if (reusedOpenTree) {
                error.clear();
                openMs = 0;
                break;
            }

            stageTimer.restart();
            const QString treeOpenExpr = QString("TreeOpen(\"%1\",%2)")
                                             .arg(escapedMdsExpr(firstSig.experiment), firstPlot.shot);
            value(*socketPtr, treeOpenExpr, &error);
            usedJavaOpenFallback = false;
            if (!error.isEmpty()) {
                const QString treeOpenError = error;
                error.clear();
                const QString javaOpenExpr = QString("JavaOpen(\"%1\",%2)")
                                                 .arg(escapedMdsExpr(firstSig.experiment), firstPlot.shot);
                value(*socketPtr, javaOpenExpr, &error);
                usedJavaOpenFallback = error.isEmpty();
                if (!error.isEmpty()) {
                    error = treeOpenError + "; fallback JavaOpen: " + error;
                }
            }
            openMs = stageTimer.elapsed();
            if (error.isEmpty()) {
                cached->currentTree = firstSig.experiment.trimmed();
                cached->currentShot = firstPlot.shot.trimmed();
                break;
            }

            if (usedCachedConnection && attempt == 0) {
                resetConnection(cached);
                error.clear();
                connectMs = 0;
                handshakeMs = 0;
                openMs = 0;
                usedCachedConnection = false;
                reusedOpenTree = false;
                usedJavaOpenFallback = false;
                continue;
            }
            resetConnection(cached);
            break;
        }

        if (!socketPtr || !error.isEmpty()) {
            QVector<SignalFetchResult> errorResults = groupErrorResults(requests, error);
            emitResults(requests, errorResults);
            return errorResults;
        }
        QTcpSocket& socket = *socketPtr;
        if (kSetServerResampleMode) {
            value(socket, "setenv('MDSPLUS_DEFAULT_RESAMPLE_MODE=MinMax')", &error);
            if (!error.isEmpty()) {
                resetConnection(cached);
                QVector<SignalFetchResult> errorResults = groupErrorResults(requests, error);
                emitResults(requests, errorResults);
                return errorResults;
            }
        }
        if (kSetTreeDefault) {
            value(socket, QString("TreeSetDefault(\"\\\\%1::TOP\")").arg(escapedMdsExpr(firstSig.experiment)), &error);
            if (!error.isEmpty()) {
                resetConnection(cached);
                QVector<SignalFetchResult> errorResults = groupErrorResults(requests, error);
                emitResults(requests, errorResults);
                return errorResults;
            }
        }
        traceMdsLine(QString("group_open_ms=%1 tree=%2 shot=%3 count=%4")
                         .arg(groupTimer.elapsed())
                         .arg(firstSig.experiment)
                         .arg(firstPlot.shot)
                         .arg(requests.size()));
        traceMdsLine(QString("group_open_parts connect_ms=%1 handshake_ms=%2 open_ms=%3 fallback=%4 tree=%5 shot=%6 count=%7")
                         .arg(connectMs)
                         .arg(handshakeMs)
                         .arg(openMs)
                         .arg(usedJavaOpenFallback ? 1 : 0)
                         .arg(firstSig.experiment)
                         .arg(firstPlot.shot)
                         .arg(requests.size()));
        traceMdsLine(QString("group_open_cache socket_reused=%1 tree_reused=%2 tree=%3 shot=%4 count=%5")
                         .arg(usedCachedConnection ? 1 : 0)
                         .arg(reusedOpenTree ? 1 : 0)
                         .arg(firstSig.experiment)
                         .arg(firstPlot.shot)
                         .arg(requests.size()));

        const QHash<QString, UniformTimebase> timebaseCache = kEnableEastTimebasePrefetch
                                                                  ? prefetchEastTimebases(socket, requests)
                                                                  : QHash<QString, UniformTimebase>{};

        QSet<int> fetchedIndexes;
        if (kEnableEastThinPipeline && readMode_ == DataReadMode::Thin && kUseServerSideThin) {
            QVector<SignalFetchResult> pipelinedEastResults = fetchEastThinPipelinedOnOpenSocket(socket, requests, &error);
            if (!pipelinedEastResults.isEmpty()) {
                for (const SignalFetchResult& result : pipelinedEastResults) {
                    fetchedIndexes.insert(result.loadedIndex);
                }
                emitResults(requests, pipelinedEastResults);
                results += pipelinedEastResults;
            }
        }

        if (kEnablePipelinedDirectFetch && readMode_ == DataReadMode::Thin && !kUseServerSideThin && !kEnableMultiSignalBatch) {
            QVector<SignalFetchResult> pipelinedResults = fetchDirectPipelinedOnOpenSocket(socket, requests, timebaseCache, &error);
            if (pipelinedResults.size() == requests.size()) {
                emitResults(requests, pipelinedResults);
                return pipelinedResults;
            }
        }

        if (kEnableMultiSignalBatch && readMode_ == DataReadMode::Thin && requests.size() > 1) {
            QVector<SignalFetchResult> batchResults = fetchThinBatchOnOpenSocket(socket, requests, &error);
            if (!batchResults.isEmpty()) {
                for (const SignalFetchResult& result : batchResults) {
                    fetchedIndexes.insert(result.loadedIndex);
                }
                emitResults(requests, batchResults);
                results += batchResults;
            }
        }

        if (kEnableEastTimeContextBatch && readMode_ == DataReadMode::Thin && kUseServerSideThin) {
            QVector<SignalFetchResult> contextResults = fetchEastTimeContextBatchOnOpenSocket(socket, requests, fetchedIndexes, &error);
            if (!contextResults.isEmpty()) {
                for (const SignalFetchResult& result : contextResults) {
                    fetchedIndexes.insert(result.loadedIndex);
                }
                emitResults(requests, contextResults);
                results += contextResults;
            }
        }

        for (int i = 0; i < requests.size(); ++i) {
            const NativeRequest& request = requests[i];
            if (fetchedIndexes.contains(request.loadedIndex)) {
                continue;
            }
            QString signalError;
            SignalFetchResult result;
            result.loadedIndex = request.loadedIndex;
            result.series = fetchSignalOnOpenSocket(socket, request.plot, request.sig, request.maxPoints, &timebaseCache, &signalError);
            emitResult(request, result);
            results.push_back(std::move(result));
        }
        return results;
    }

    QVector<SignalFetchResult> fetchEastThinPipelinedOnOpenSocket(QTcpSocket& socket,
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
            results.push_back(std::move(result));
        }

        traceMdsLine(QString("east_pipeline_chunk_ms=%1 count=%2").arg(chunkTimer.elapsed()).arg(results.size()));
        return results;
    }

    QVector<SignalFetchResult> fetchDirectPipelinedOnOpenSocket(QTcpSocket& socket,
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

    QVector<SignalFetchResult> fetchEastTimeContextBatchOnOpenSocket(QTcpSocket& socket,
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
        const UniformTimebase timebase = eastUniformTimebase(socket, pending.front().request.plot.shot, firstSig, &localError);
        if (!timebase.valid) {
            if (error) {
                error->clear();
            }
            return {};
        }

        const int maxPoints = pending.front().request.maxPoints > 0 ? pending.front().request.maxPoints : 2000;
        const double start = timebase.start;
        const double end = start + 4.0;
        const double delta = (end - start) / static_cast<double>(maxPoints);
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

    static QVector<SignalFetchResult> groupErrorResults(const QVector<NativeRequest>& requests, const QString& error)
    {
        QVector<SignalFetchResult> results;
        results.reserve(requests.size());
        for (const NativeRequest& request : requests) {
            SignalFetchResult result;
            result.loadedIndex = request.loadedIndex;
            result.series.error = error;
            results.push_back(std::move(result));
        }
        return results;
    }

    static void applyFetchResults(const QVector<SignalFetchResult>& results, QVector<LoadedSignal>* loaded)
    {
        for (const SignalFetchResult& result : results) {
            if (result.loadedIndex >= 0 && result.loadedIndex < loaded->size()) {
                (*loaded)[result.loadedIndex].series = result.series;
            }
        }
    }

    static bool isPermanentMdsError(const QString& error)
    {
        const QString e = error.toLower();
        return e.contains("node not found")
               || e.contains("no data available")
               || e.contains("missing server/tree/shot/signal")
               || e.contains("%tree-w-nnf")
               || e.contains("%tree-e-nodata");
    }

    static bool shouldRetrySignal(const LoadedSignal& item)
    {
        if (item.series.hasData()) {
            return false;
        }
        if (item.series.error.isEmpty()) {
            return true;
        }
        return !isPermanentMdsError(item.series.error);
    }

    void retryTransientFailures(const QHash<int, NativeRequest>& requestsByLoadedIndex, QVector<LoadedSignal>* loaded) const
    {
        QVector<NativeRequest> retryRequests;
        retryRequests.reserve(loaded->size());
        for (int i = 0; i < loaded->size(); ++i) {
            if (shouldRetrySignal((*loaded)[i]) && requestsByLoadedIndex.contains(i)) {
                retryRequests.push_back(requestsByLoadedIndex.value(i));
            }
        }
        if (retryRequests.isEmpty()) {
            return;
        }

        constexpr int kMaxRetrySockets = 8;
        for (int start = 0; start < retryRequests.size(); start += kMaxRetrySockets) {
            const int count = std::min(kMaxRetrySockets, static_cast<int>(retryRequests.size()) - start);
            QVector<QFuture<QVector<SignalFetchResult>>> futures;
            futures.reserve(count);
            for (int i = 0; i < count; ++i) {
                QVector<NativeRequest> single;
                single.push_back(retryRequests[start + i]);
                futures.push_back(QtConcurrent::run([this, single = std::move(single)] {
                    return fetchGroupResults(single);
                }));
            }
            for (auto& future : futures) {
                const QVector<SignalFetchResult> results = future.result();
                for (const SignalFetchResult& result : results) {
                    if (result.loadedIndex < 0 || result.loadedIndex >= loaded->size()) {
                        continue;
                    }
                    LoadedSignal& item = (*loaded)[result.loadedIndex];
                    if (result.series.hasData() || !item.series.hasData()) {
                        item.series = result.series;
                    }
                }
            }
        }
    }

    LoadedSignal loadedSignalFromResult(const NativeRequest& request, const SignalFetchResult& result) const
    {
        LoadedSignal loaded;
        loaded.column = request.column;
        loaded.row = request.row;
        loaded.signal = request.signal;
        loaded.shot = request.shot;
        loaded.series = result.series;
        return loaded;
    }

    void emitResult(const NativeRequest& request, const SignalFetchResult& result) const
    {
        if (callback_) {
            callback_(loadedSignalFromResult(request, result));
        }
    }

    void emitResults(const QVector<NativeRequest>& requests, const QVector<SignalFetchResult>& results) const
    {
        if (!callback_) {
            return;
        }
        QHash<int, const NativeRequest*> byIndex;
        byIndex.reserve(requests.size());
        for (const NativeRequest& request : requests) {
            byIndex.insert(request.loadedIndex, &request);
        }
        for (const SignalFetchResult& result : results) {
            if (const NativeRequest* request = byIndex.value(result.loadedIndex, nullptr)) {
                callback_(loadedSignalFromResult(*request, result));
            }
        }
    }

    QVector<SignalFetchResult> fetchThinBatchOnOpenSocket(QTcpSocket& socket, const QVector<NativeRequest>& requests, QString* error) const
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

    SignalSeries fetchSignalOnOpenSocket(QTcpSocket& socket,
                                         const PlotSpec& plot,
                                         const SignalSpec& sig,
                                         int maxPoints,
                                         const QHash<QString, UniformTimebase>* timebaseCache,
                                         QString* error) const
    {
        QElapsedTimer timer;
        timer.start();
        SignalSeries result = fetchSignalOnOpenSocketImpl(socket, plot, sig, maxPoints, timebaseCache, error);
        traceMdsLine(QString("signal_ms=%1 shot=%2 tree=%3 y=%4 points=%5 error=%6")
                         .arg(timer.elapsed())
                         .arg(plot.shot)
                         .arg(sig.experiment)
                         .arg(sig.yExpr)
                         .arg(result.pointCount())
                         .arg(result.error.simplified()));
        return result;
    }

    SignalSeries fetchSignalOnOpenSocketImpl(QTcpSocket& socket,
                                             const PlotSpec& plot,
                                             const SignalSpec& sig,
                                             int maxPoints,
                                             const QHash<QString, UniformTimebase>* timebaseCache,
                                             QString* error) const
    {
        SignalSeries result;
        result.name = normalizedMdsSignal(sig.yExpr);

        ThinSampling sampling;
        UniformTimebase fastTimebase;
        const QString xExpr = sig.xExpr.trimmed();
        const bool serverSideThin = readMode_ == DataReadMode::Thin && kUseServerSideThin;
        SignalSpec fastSig = sig;
        const ScaledSignalExpr scaledExpr = scaledSimpleSignalExpr(sig.yExpr);
        if (scaledExpr.valid) {
            fastSig.yExpr = scaledExpr.baseExpr;
        }
        int fullExpectedPoints = 0;
        if (readMode_ == DataReadMode::Full) {
            fullExpectedPoints = fullPointCountBestEffort(socket, fastSig.yExpr);
        }
        if (readMode_ == DataReadMode::Full && xExpr.isEmpty() && scaledExpr.valid && isEastTimebaseCandidate(plot.shot, fastSig)) {
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
            if (!prefersEastTimeContext(fastSig.yExpr)) {
                QString savedError;
                SignalSeries saved = fetchSavedEastSignalOnOpenSocket(socket, plot, fastSig, maxPoints, &savedError);
                if (saved.hasData()) {
                    applySeriesScale(&saved, sig.yExpr, scaledExpr.scale);
                    if (error) {
                        error->clear();
                    }
                    return saved;
                }
            }
            SignalSeries contextual = fetchEastTimeContextSignalOnOpenSocket(socket, plot, fastSig, maxPoints, timebaseCache, error);
            if (contextual.hasData()) {
                applySeriesScale(&contextual, sig.yExpr, scaledExpr.scale);
                return contextual;
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
        const int displayMaxPoints = readMode_ == DataReadMode::Thin ? maxPoints : 0;
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

        if (readMode_ == DataReadMode::Thin && xExpr.isEmpty()) {
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
        if (readMode_ == DataReadMode::Full) {
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

    SignalSeries fetchEastTimeContextSignalOnOpenSocket(QTcpSocket& socket,
                                                        const PlotSpec& plot,
                                                        const SignalSpec& sig,
                                                        int maxPoints,
                                                        const QHash<QString, UniformTimebase>* timebaseCache,
                                                        QString* error) const
    {
        SignalSeries result;
        result.name = normalizedMdsSignal(sig.yExpr);
        if (maxPoints <= 0) {
            return result;
        }

        QString localError;
        UniformTimebase timebase;
        if (timebaseCache) {
            timebase = timebaseCache->value(eastTimebaseKey(plot.shot, sig));
        }
        if (!timebase.valid) {
            timebase = eastUniformTimebase(socket, plot.shot, sig, &localError);
        }
        if (!timebase.valid) {
            if (error) {
                error->clear();
            }
            return result;
        }

        const double start = timebase.start;
        const double end = start + 4.0;
        const double delta = (end - start) / static_cast<double>(maxPoints);
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

    SignalSeries fetchSavedEastSignalOnOpenSocket(QTcpSocket& socket,
                                                  const PlotSpec& plot,
                                                  const SignalSpec& sig,
                                                  int maxPoints,
                                                  QString* error) const
    {
        SignalSeries result;
        result.name = normalizedMdsSignal(sig.yExpr);
        const QString savedExpr = sig.yExpr.trimmed() + "_s";
        QString localError;
        const QVector<double> y = numericValue(socket,
                                               QString("( _jscope_0 = (%1), fs_float(_jscope_0))").arg(savedExpr),
                                               &localError);
        if (y.isEmpty()) {
            if (error) {
                error->clear();
            }
            return result;
        }

        const QVector<double> x = numericValue(socket,
                                               QString("( _jscope_1 = (dim_of(%1)), ft_float(_jscope_1))").arg(savedExpr),
                                               &localError);
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

    struct Message {
        qint32 status = 0;
        qint16 length = 0;
        qint8 dtype = 0;
        QByteArray body;
    };

    static QByteArray message(qint8 dtype, qint8 nargs, qint8 descrIdx, quint8 messageId, const QByteArray& body)
    {
        QByteArray packet;
        QDataStream out(&packet, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::BigEndian);
        out << qint32(48 + body.size());
        out << qint32(0);
        out << qint16(body.size());
        out << nargs;
        out << descrIdx;
        out << qint8(messageId);
        out << dtype;
        out << qint8(0xC3);
        out << qint8(0);
        for (int i = 0; i < 8; ++i) {
            out << qint32(0);
        }
        packet.append(body);
        return packet;
    }

    static bool writeMessage(QTcpSocket& socket, const QByteArray& packet, QString* error)
    {
        socket.write(packet);
        if (!socket.waitForBytesWritten(kNetworkTimeoutMs)) {
            *error = socket.errorString();
            return false;
        }
        return true;
    }

    static bool readFully(QTcpSocket& socket, QByteArray* out, qsizetype size, QString* error)
    {
        while (out->size() < size) {
            if (socket.bytesAvailable() <= 0 && !socket.waitForReadyRead(kNetworkTimeoutMs)) {
                *error = socket.errorString();
                return false;
            }
            out->append(socket.read(size - out->size()));
        }
        return true;
    }

    static Message readMessage(QTcpSocket& socket, QString* error)
    {
        error->clear();
        QByteArray header;
        Message msg;
        if (!readFully(socket, &header, 48, error)) {
            return msg;
        }
        QDataStream in(header);
        in.setByteOrder(QDataStream::BigEndian);
        qint32 msgLen = 0;
        qint8 nargs = 0;
        qint8 descrIdx = 0;
        qint8 messageId = 0;
        qint8 clientType = 0;
        qint8 ndims = 0;
        in >> msgLen >> msg.status >> msg.length >> nargs >> descrIdx >> messageId >> msg.dtype >> clientType >> ndims;
        if (msgLen < 48 || msgLen > 128 * 1024 * 1024) {
            *error = "invalid MDSIP message length";
            return msg;
        }
        if (!readFully(socket, &msg.body, msgLen - 48, error)) {
            msg.body.clear();
        }
        return msg;
    }

    static bool handshake(QTcpSocket& socket, QString* error)
    {
        const QByteArray user("JAVA_USER");
        if (!writeMessage(socket, message(14, 1, 0, 1, user), error)) {
            return false;
        }
        readMessage(socket, error);
        return error->isEmpty();
    }

    static quint8 nextMessageId()
    {
        thread_local quint8 messageId = 2;
        const quint8 currentMessageId = messageId;
        if (++messageId == 0) {
            messageId = 1;
        }
        return currentMessageId;
    }

    static bool sendValue(QTcpSocket& socket, const QString& expr, QString* error)
    {
        error->clear();
        const QByteArray body = expr.toUtf8();
        return writeMessage(socket, message(14, 1, 0, nextMessageId(), body), error);
    }

    static bool queueValue(QTcpSocket& socket, const QString& expr, QString* error)
    {
        error->clear();
        const QByteArray body = expr.toUtf8();
        const QByteArray packet = message(14, 1, 0, nextMessageId(), body);
        if (socket.write(packet) != packet.size()) {
            *error = socket.errorString();
            return false;
        }
        return true;
    }

    static bool flushQueuedValues(QTcpSocket& socket, QString* error)
    {
        while (socket.bytesToWrite() > 0) {
            if (!socket.waitForBytesWritten(kNetworkTimeoutMs)) {
                *error = socket.errorString();
                return false;
            }
        }
        return true;
    }

    static Message value(QTcpSocket& socket, const QString& expr, QString* error)
    {
        error->clear();
        const QByteArray body = expr.toUtf8();
        const quint8 currentMessageId = nextMessageId();
        if (!writeMessage(socket, message(14, 1, 0, currentMessageId, body), error)) {
            return {};
        }
        return readMessage(socket, error);
    }

    static QVector<double> numericFromMessage(const Message& msg, QString* error)
    {
        if (!error->isEmpty() || msg.body.isEmpty()) {
            return {};
        }
        if (msg.dtype == 14) {
            *error = QString::fromUtf8(msg.body).trimmed();
            return {};
        }
        QVector<double> values;
        QDataStream in(msg.body);
        in.setByteOrder(QDataStream::BigEndian);

        if (msg.dtype == 11 || msg.dtype == 53) {
            in.setFloatingPointPrecision(QDataStream::DoublePrecision);
            values.reserve(msg.body.size() / 8);
            while (!in.atEnd()) {
                double v = 0.0;
                in >> v;
                if (std::isfinite(v)) {
                    values.push_back(v);
                }
            }
            return values;
        }

        if (msg.dtype == 10 || msg.dtype == 52 || msg.body.size() % 4 == 0) {
            in.setFloatingPointPrecision(QDataStream::SinglePrecision);
            values.reserve(msg.body.size() / 4);
            while (!in.atEnd()) {
                float v = 0.0f;
                in >> v;
                if (std::isfinite(v)) {
                    values.push_back(v);
                }
            }
        }
        return values;
    }

    static QVector<double> numericValue(QTcpSocket& socket, const QString& expr, QString* error)
    {
        return numericFromMessage(value(socket, expr, error), error);
    }

    static QVector<double> cachedNumericValue(QTcpSocket& socket,
                                              const QString& expr,
                                              QHash<QString, QVector<double>>* cache,
                                              QString* error)
    {
        if (!cache) {
            return numericValue(socket, expr, error);
        }
        if (cache->contains(expr)) {
            if (error) {
                error->clear();
            }
            return cache->value(expr);
        }
        QVector<double> values = numericValue(socket, expr, error);
        if (!values.isEmpty()) {
            cache->insert(expr, values);
        }
        return values;
    }

    static int intValue(QTcpSocket& socket, const QString& expr, QString* error)
    {
        const Message msg = value(socket, expr, error);
        if (!error->isEmpty() || msg.body.isEmpty()) {
            return 0;
        }
        if (msg.dtype == 14) {
            *error = QString::fromUtf8(msg.body).trimmed();
            return 0;
        }

        QDataStream in(msg.body);
        in.setByteOrder(QDataStream::BigEndian);
        if ((msg.dtype == 11 || msg.dtype == 53) && msg.body.size() >= 8) {
            in.setFloatingPointPrecision(QDataStream::DoublePrecision);
            double v = 0.0;
            in >> v;
            return std::isfinite(v) && v > 0.0 && v <= std::numeric_limits<int>::max()
                       ? static_cast<int>(std::llround(v))
                       : 0;
        }
        if ((msg.dtype == 10 || msg.dtype == 52) && msg.body.size() >= 4) {
            in.setFloatingPointPrecision(QDataStream::SinglePrecision);
            float v = 0.0f;
            in >> v;
            return std::isfinite(v) && v > 0.0f && v <= static_cast<float>(std::numeric_limits<int>::max())
                       ? static_cast<int>(std::lround(v))
                       : 0;
        }
        if (msg.body.size() == 8 && msg.dtype != 11 && msg.dtype != 53) {
            qint64 v = 0;
            in >> v;
            return v > 0 && v <= std::numeric_limits<int>::max() ? static_cast<int>(v) : 0;
        }
        if (msg.body.size() >= 4) {
            qint32 v = 0;
            in >> v;
            return v > 0 ? v : 0;
        }
        if (msg.body.size() >= 2) {
            qint16 v = 0;
            in >> v;
            return v > 0 ? v : 0;
        }
        if (msg.body.size() == 1) {
            return static_cast<quint8>(msg.body[0]);
        }
        return 0;
    }

    static QVector<int> intVectorValue(QTcpSocket& socket, const QString& expr, QString* error)
    {
        const Message msg = value(socket, expr, error);
        if (!error->isEmpty() || msg.body.isEmpty()) {
            return {};
        }
        if (msg.dtype == 14) {
            *error = QString::fromUtf8(msg.body).trimmed();
            return {};
        }

        QVector<int> values;
        QDataStream in(msg.body);
        in.setByteOrder(QDataStream::BigEndian);
        if (msg.body.size() % 4 == 0) {
            values.reserve(msg.body.size() / 4);
            while (!in.atEnd()) {
                qint32 v = 0;
                in >> v;
                values.push_back(v > 0 ? v : 0);
            }
            return values;
        }
        if (msg.body.size() % 2 == 0) {
            values.reserve(msg.body.size() / 2);
            while (!in.atEnd()) {
                qint16 v = 0;
                in >> v;
                values.push_back(v > 0 ? v : 0);
            }
            return values;
        }
        values.reserve(msg.body.size());
        for (char byte : msg.body) {
            values.push_back(static_cast<quint8>(byte));
        }
        return values;
    }

    static bool isSimpleMdsNode(const QString& expr)
    {
        const QString node = expr.trimmed();
        return node.startsWith('\\')
               && !node.contains('(')
               && !node.contains(')')
               && !node.contains('[')
               && !node.contains(']')
               && !node.contains(',')
               && !node.contains(' ');
    }

    static ScaledSignalExpr scaledSimpleSignalExpr(QString expr)
    {
        expr = expr.trimmed();
        expr.remove(' ');
        ScaledSignalExpr out;
        if (expr.isEmpty()) {
            return out;
        }

        double sign = 1.0;
        if (expr.startsWith('-')) {
            sign = -1.0;
            expr.remove(0, 1);
        } else if (expr.startsWith('+')) {
            expr.remove(0, 1);
        }

        static const QRegularExpression re(
            R"(^\(?(\\[A-Za-z][A-Za-z0-9_]*)\)?(?:([*/])([0-9]+(?:\.[0-9]+)?))?$)");
        const QRegularExpressionMatch match = re.match(expr);
        if (!match.hasMatch()) {
            return out;
        }

        double scale = sign;
        if (!match.captured(2).isEmpty()) {
            bool ok = false;
            const double value = match.captured(3).toDouble(&ok);
            if (!ok || value == 0.0) {
                return {};
            }
            scale *= match.captured(2) == "*" ? value : (1.0 / value);
        }

        out.valid = true;
        out.baseExpr = match.captured(1);
        out.scale = scale;
        return out;
    }

    static void applySeriesScale(SignalSeries* series, const QString& name, double scale)
    {
        series->name = normalizedMdsSignal(name);
        if (scale == 1.0) {
            return;
        }
        for (QPointF& point : series->points) {
            point.setY(point.y() * scale);
        }
        for (float& value : series->uniformY) {
            value = static_cast<float>(static_cast<double>(value) * scale);
        }
        if (std::isfinite(series->uniformMinY) && std::isfinite(series->uniformMaxY)) {
            const double a = series->uniformMinY * scale;
            const double b = series->uniformMaxY * scale;
            series->uniformMinY = std::min(a, b);
            series->uniformMaxY = std::max(a, b);
        }
    }

    static bool isEastTimebaseCandidate(const QString& shotText, const SignalSpec& sig)
    {
        const QString experiment = sig.experiment.trimmed().toLower();
        bool shotOk = false;
        const int shot = shotText.trimmed().toInt(&shotOk);
        return (((experiment == "east" && shotOk && shot > 44326) || experiment == "eastpower")
                && isSimpleMdsNode(sig.yExpr));
    }

    static QString eastTimebaseKey(const QString& shotText, const SignalSpec& sig)
    {
        return shotText.trimmed() + "|" + sig.experiment.trimmed().toLower() + "|" + sig.yExpr.trimmed().toLower();
    }

    static QHash<QString, UniformTimebase> prefetchEastTimebases(QTcpSocket& socket, const QVector<NativeRequest>& requests)
    {
        struct Candidate {
            QString key;
            QString shot;
            QString tree;
            QString yExpr;
        };

        QVector<Candidate> candidates;
        QSet<QString> seen;
        for (const NativeRequest& request : requests) {
            if (!isEastTimebaseCandidate(request.plot.shot, request.sig)) {
                continue;
            }
            const QString key = eastTimebaseKey(request.plot.shot, request.sig);
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            candidates.push_back({key, request.plot.shot, request.sig.experiment, request.sig.yExpr.trimmed()});
        }
        if (candidates.isEmpty()) {
            return {};
        }

        QHash<QString, UniformTimebase> out;

        std::function<void(int, int)> fetchRange = [&](int start, int count) {
            if (count <= 0) {
                return;
            }

            QString error;
            QVector<int> freqs;
            QVector<double> trigs;
            if (count == 1) {
                freqs.push_back(intValue(socket, candidates[start].yExpr + ":freq", &error));
                trigs = numericValue(socket, candidates[start].yExpr + ":trigtime", &error);
            } else {
                QStringList freqParts;
                QStringList trigParts;
                freqParts.reserve(count);
                trigParts.reserve(count);
                for (int i = 0; i < count; ++i) {
                    const Candidate& candidate = candidates[start + i];
                    freqParts.push_back(candidate.yExpr + ":freq");
                    trigParts.push_back(candidate.yExpr + ":trigtime");
                }
                freqs = intVectorValue(socket, QString("[%1]").arg(freqParts.join(",")), &error);
                if (freqs.size() == count) {
                    trigs = numericValue(socket, QString("[%1]").arg(trigParts.join(",")), &error);
                }
            }

            if (freqs.size() != count || trigs.size() != count) {
                if (count == 1) {
                    traceMdsLine(QString("east_timebase_prefetch_skip y=%1 error=%2")
                                     .arg(candidates[start].yExpr, error.simplified()));
                    return;
                }
                const int leftCount = count / 2;
                fetchRange(start, leftCount);
                fetchRange(start + leftCount, count - leftCount);
                return;
            }

            for (int i = 0; i < count; ++i) {
                if (freqs[i] <= 0 || !std::isfinite(trigs[i])) {
                    continue;
                }
                UniformTimebase timebase;
                timebase.valid = true;
                timebase.start = trigs[i];
                timebase.step = 1.0 / static_cast<double>(freqs[i]);
                out.insert(candidates[start + i].key, timebase);
            }
        };

        fetchRange(0, candidates.size());
        traceMdsLine(QString("east_timebase_prefetch count=%1 cached=%2")
                         .arg(candidates.size())
                         .arg(out.size()));
        return out;
    }

    static UniformTimebase eastUniformTimebase(QTcpSocket& socket,
                                               const QString& shotText,
                                               const SignalSpec& sig,
                                               QString* error)
    {
        const QString yExpr = sig.yExpr.trimmed();
        if (!isEastTimebaseCandidate(shotText, sig)) {
            return {};
        }

        QString localError;
        const int freq = intValue(socket, yExpr + ":freq", &localError);
        if (freq <= 0) {
            if (error) {
                error->clear();
            }
            return {};
        }

        const QVector<double> trigtime = numericValue(socket, yExpr + ":trigtime", &localError);
        if (trigtime.isEmpty() || !std::isfinite(trigtime.front())) {
            if (error) {
                error->clear();
            }
            return {};
        }

        if (error) {
            error->clear();
        }
        UniformTimebase timebase;
        timebase.valid = true;
        timebase.start = trigtime.front();
        timebase.step = 1.0 / static_cast<double>(freq);
        traceMdsLine(QString("east_timebase shot=%1 tree=%2 y=%3 freq=%4 trigtime=%5")
                         .arg(shotText, sig.experiment, yExpr)
                         .arg(freq)
                         .arg(timebase.start, 0, 'g', 12));
        return timebase;
    }

    static EastThinPlan eastThinPlan(QTcpSocket& socket,
                                     const QString& shotText,
                                     const SignalSpec& sig,
                                     int maxPoints,
                                     QString* error)
    {
        EastThinPlan plan;
        if (maxPoints <= 0 || !isEastTimebaseCandidate(shotText, sig)) {
            return plan;
        }

        const QString yExpr = sig.yExpr.trimmed();
        QString localError;
        const QVector<double> meta = numericValue(socket,
                                                  QString("[size(%1),%1:freq,%1:trigtime]").arg(yExpr),
                                                  &localError);
        if (meta.size() < 3 || !std::isfinite(meta[0]) || !std::isfinite(meta[1]) || !std::isfinite(meta[2])) {
            if (error) {
                error->clear();
            }
            return plan;
        }

        const int pointCount = static_cast<int>(std::llround(meta[0]));
        const int freq = static_cast<int>(std::llround(meta[1]));
        if (pointCount <= 0 || freq <= 0) {
            if (error) {
                error->clear();
            }
            return plan;
        }

        plan.sampling = samplingFromPointCount(pointCount, maxPoints);
        if (plan.sampling.sampledCount <= 0) {
            if (error) {
                error->clear();
            }
            return {};
        }

        plan.timebase.valid = true;
        plan.timebase.start = meta[2];
        plan.timebase.step = 1.0 / static_cast<double>(freq);
        plan.valid = true;
        if (error) {
            error->clear();
        }
        traceMdsLine(QString("east_thin_plan y=%1 size=%2 step=%3 freq=%4 trigtime=%5")
                         .arg(yExpr)
                         .arg(pointCount)
                         .arg(plan.sampling.step)
                         .arg(freq)
                         .arg(plan.timebase.start, 0, 'g', 12));
        return plan;
    }

    static ThinSampling samplingFromPointCount(int pointCount, int maxPoints)
    {
        ThinSampling sampling;
        if (maxPoints <= 0 || pointCount <= 0) {
            return sampling;
        }
        sampling.sourceCount = pointCount;
        if (pointCount <= maxPoints) {
            sampling.step = 1;
            sampling.sampledCount = pointCount;
            return sampling;
        }
        sampling.step = std::max(1, (pointCount / maxPoints) + 1);
        sampling.sampledCount = pointCount > 1 ? ((pointCount - 2) / sampling.step) + 1 : pointCount;
        return sampling;
    }

    static QVector<ThinSampling> thinSamplings(QTcpSocket& socket, const QVector<NativeRequest>& requests, QString* error)
    {
        QStringList sizeParts;
        sizeParts.reserve(requests.size());
        for (const NativeRequest& request : requests) {
            sizeParts.push_back(QString("size(%1)").arg(request.sig.yExpr));
        }

        const QVector<int> sizes = intVectorValue(socket, QString("[%1]").arg(sizeParts.join(",")), error);
        if (sizes.size() != requests.size()) {
            return {};
        }

        QVector<ThinSampling> samplings;
        samplings.reserve(requests.size());
        for (int i = 0; i < requests.size(); ++i) {
            ThinSampling sampling = samplingFromPointCount(sizes[i], requests[i].maxPoints);
            if (sampling.sampledCount <= 0) {
                return {};
            }
            samplings.push_back(sampling);
        }
        if (error) {
            error->clear();
        }
        return samplings;
    }

    static QVector<ThinSampling> thinSamplingsBestEffort(QTcpSocket& socket, const QVector<NativeRequest>& requests, QString* error)
    {
        QVector<ThinSampling> samplings(requests.size());
        fillThinSamplings(socket, requests, 0, static_cast<int>(requests.size()), &samplings, error);
        if (error) {
            error->clear();
        }
        return samplings;
    }

    static void fillThinSamplings(QTcpSocket& socket,
                                  const QVector<NativeRequest>& requests,
                                  int start,
                                  int count,
                                  QVector<ThinSampling>* out,
                                  QString* error)
    {
        if (count <= 0) {
            return;
        }

        QStringList sizeParts;
        sizeParts.reserve(count);
        for (int i = 0; i < count; ++i) {
            sizeParts.push_back(QString("size(%1)").arg(requests[start + i].sig.yExpr));
        }

        QString localError;
        const QVector<int> sizes = intVectorValue(socket, QString("[%1]").arg(sizeParts.join(",")), &localError);
        if (sizes.size() == count) {
            for (int i = 0; i < count; ++i) {
                (*out)[start + i] = samplingFromPointCount(sizes[i], requests[start + i].maxPoints);
            }
            if (error) {
                error->clear();
            }
            return;
        }

        if (count == 1) {
            QString singleError;
            const int pointCount = intValue(socket, QString("size(%1)").arg(requests[start].sig.yExpr), &singleError);
            (*out)[start] = samplingFromPointCount(pointCount, requests[start].maxPoints);
            if (error) {
                *error = singleError;
            }
            return;
        }

        const int leftCount = count / 2;
        fillThinSamplings(socket, requests, start, leftCount, out, error);
        fillThinSamplings(socket, requests, start + leftCount, count - leftCount, out, error);
    }

    static ThinSampling thinSampling(QTcpSocket& socket, const QString& yExpr, int maxPoints, QString* error)
    {
        if (maxPoints <= 0) {
            return {};
        }

        QString localError;
        const int pointCount = intValue(socket, QString("size(%1)").arg(yExpr), &localError);
        ThinSampling sampling = samplingFromPointCount(pointCount, maxPoints);
        if (sampling.sampledCount <= 0) {
            if (error) {
                *error = localError.isEmpty() ? "empty signal" : localError;
            }
            return sampling;
        }
        if (sampling.step <= 1) {
            if (error) {
                error->clear();
            }
            return sampling;
        }

        if (error) {
            error->clear();
        }
        return sampling;
    }

    static void minMaxDownsample(SignalSeries* series, int maxPoints)
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

    static void buildUniformOverview(SignalSeries* series, int maxPoints = 4000)
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

    static SignalSeries makeSeries(QString name, const QVector<double>& y, const QVector<double>& x, int maxPoints)
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

    static SignalSeries makeSeriesUniformX(QString name,
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

    static int numericElementSize(const Message& msg)
    {
        if (msg.dtype == 11 || msg.dtype == 53) {
            return 8;
        }
        if (msg.dtype == 10 || msg.dtype == 52 || msg.body.size() % 4 == 0) {
            return 4;
        }
        return 0;
    }

    static double numericAt(const QByteArray& body, int elementSize, int index)
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

    static SignalSeries makeSeriesUniformXFromMessage(QString name,
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

    static SignalSeries makeSeriesFromAdjacentSegments(QString name, const QVector<double>& values, int offset, int count, int maxPoints)
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

    static SignalSeries makeSeriesFromCombined(QString name, const QVector<double>& xy, int maxPoints)
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
};


}

QVector<LoadedSignal> fetchMdsSignals(const LayoutConfig& snapshot,
                                      DataReadMode readMode,
                                      LoadedSignalCallback callback)
{
    MdsIpClient client(readMode, std::move(callback));
    return client.fetchAll(snapshot);
}

void warmMdsConnections(const LayoutConfig& snapshot)
{
    MdsIpClient client(DataReadMode::Thin);
    client.warmConnections(snapshot);
}

SignalSeries fetchMdsSignal(const PlotSpec& plot, const SignalSpec& sig, DataReadMode readMode)
{
    MdsIpClient client(readMode);
    return client.fetch(plot, sig);
}

void clearMdsCurrentThreadConnections()
{
    MdsIpClient::clearCurrentThreadConnections();
}
