// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mds_ip_client.h"

namespace mds_client_internal {

using CachedMdsConnection = MdsIpClient::CachedMdsConnection;
using NativeRequest = MdsIpClient::NativeRequest;
using SignalFetchResult = MdsIpClient::SignalFetchResult;

constexpr int kGroupFetchThreadLimit = 16;

QThreadPool* groupFetchPool()
{
    static QThreadPool* pool = [] {
        auto* created = new QThreadPool();
        created->setMaxThreadCount(kGroupFetchThreadLimit);
        return created;
    }();
    return pool;
}


MdsIpClient::MdsIpClient(DataReadMode readMode,
                         ResultCallback callback,
                         std::shared_ptr<std::atomic_bool> cancel)
    : readMode_(readMode), callback_(std::move(callback)), cancel_(std::move(cancel))
{
}

void MdsIpClient::clearCurrentThreadConnections()
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

QVector<LoadedSignal> MdsIpClient::fetchAll(const LayoutConfig& snapshot) const
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
                    request.readMode = effectiveSignalReadMode(readMode_, sig);
                    request.maxPoints = maxPointsForSignal(plot, sig);
                    requestsByLoadedIndex.insert(loadedIndex, request);
                    groups[groupKey(request)].push_back(std::move(request));
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
            if (isCanceled()) {
                clearCurrentThreadConnections();
                break;
            }
            const int count = std::min(kMaxGlobalSockets, static_cast<int>(chunks.size()) - start);
            QVector<QFuture<QVector<SignalFetchResult>>> futures;
            futures.reserve(count);
            for (int i = 0; i < count; ++i) {
                QVector<NativeRequest> chunk = std::move(chunks[start + i]);
                futures.push_back(QtConcurrent::run(groupFetchPool(), [this, chunk = std::move(chunk)] {
                    return fetchGroupResults(chunk);
                }));
            }
            for (auto& future : futures) {
                applyFetchResults(future.result(), &loaded);
            }
        }
        if (!isCanceled()) {
            retryTransientFailures(requestsByLoadedIndex, &loaded);
        }
        return loaded;
    }

void MdsIpClient::warmConnections(const LayoutConfig& snapshot) const
{
        QHash<QString, NativeRequest> representatives;
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
                    request.readMode = effectiveSignalReadMode(readMode_, sig);
                    request.maxPoints = maxPointsForSignal(plot, sig);
                    representatives.insert(groupKey(request), std::move(request));
                }
            }
        }

        QVector<NativeRequest> warmRequests;
        warmRequests.reserve(representatives.size());
        for (auto it = representatives.begin(); it != representatives.end(); ++it) {
            warmRequests.push_back(std::move(it.value()));
        }

        constexpr int kMaxGlobalSockets = 16;
        for (int start = 0; start < warmRequests.size(); start += kMaxGlobalSockets) {
            if (isCanceled()) {
                clearCurrentThreadConnections();
                break;
            }
            const int count = std::min(kMaxGlobalSockets, static_cast<int>(warmRequests.size()) - start);
            const int warmCount = std::max(count, kMaxGlobalSockets);
            QVector<QFuture<void>> futures;
            futures.reserve(warmCount);
            for (int i = 0; i < warmCount; ++i) {
                QVector<NativeRequest> warmChunk;
                warmChunk.push_back(warmRequests[start + (i % count)]);
                futures.push_back(QtConcurrent::run(groupFetchPool(), [this, warmChunk = std::move(warmChunk)] {
                    fetchGroupResults(warmChunk, true);
                }));
            }
            for (auto& future : futures) {
                future.waitForFinished();
            }
        }
    }

SignalSeries MdsIpClient::fetch(const PlotSpec& plot, const SignalSpec& sig) const
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
        request.readMode = effectiveSignalReadMode(readMode_, sig);
        request.maxPoints = maxPointsForSignal(plot, sig);
        QVector<NativeRequest> requests = {request};
        QVector<LoadedSignal> loaded(1);
        loaded[0].series = result;
        fetchGroup(requests, &loaded);
        return loaded[0].series;
    }

bool MdsIpClient::isCanceled() const
{
        return cancel_ && cancel_->load(std::memory_order_relaxed);
    }

const std::atomic_bool*& MdsIpClient::currentCancel()
{
        thread_local const std::atomic_bool* cancel = nullptr;
        return cancel;
    }

bool MdsIpClient::currentCanceled()
{
        const std::atomic_bool* cancel = currentCancel();
        return cancel && cancel->load(std::memory_order_relaxed);
    }

QString MdsIpClient::groupKey(const NativeRequest& request)
{
        QString server = request.sig.serverIp.trimmed();
        if (!server.contains(':')) {
            server += ":8000";
        }
        return server + "|"
               + request.sig.experiment.trimmed() + "|"
               + request.plot.shot.trimmed();
    }

int MdsIpClient::maxPointsForSignal(const PlotSpec& plot, const SignalSpec& sig)
{
        const int configured = plot.extractionPoints > 0 ? plot.extractionPoints : 2000;
        return configured;
    }

QString MdsIpClient::serverHost(QString server)
{
        server = server.trimmed();
        const int colon = server.lastIndexOf(':');
        if (colon > 0) {
            return server.left(colon);
        }
        return server;
    }

int MdsIpClient::serverPort(const QString& server)
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

QString MdsIpClient::serverKey(const SignalSpec& sig)
{
        return serverHost(sig.serverIp) + ":" + QString::number(serverPort(sig.serverIp));
    }

int MdsIpClient::fullLargeDownloadLimit()
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

int MdsIpClient::fullLargeSignalPointThreshold()
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

QSemaphore* MdsIpClient::fullLargeDownloadSemaphore(const SignalSpec& sig)
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

int MdsIpClient::fullPointCountBestEffort(QTcpSocket& socket, const QString& yExpr)
{
        QString localError;
        const int count = intValue(socket, QString("size(%1)").arg(yExpr), &localError);
        return count > 0 ? count : 0;
    }

std::unique_ptr<MdsIpClient::SemaphoreGuard> MdsIpClient::fullLargeDownloadGuard(const SignalSpec& sig, int pointCount)
{
        if (pointCount < fullLargeSignalPointThreshold()) {
            return {};
        }
        return std::make_unique<SemaphoreGuard>(fullLargeDownloadSemaphore(sig));
    }

CachedMdsConnection* MdsIpClient::threadLocalConnection(const SignalSpec& sig)
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

std::vector<std::unique_ptr<CachedMdsConnection>>& MdsIpClient::threadLocalConnections()
{
        thread_local std::vector<std::unique_ptr<CachedMdsConnection>> connections;
        return connections;
    }

void MdsIpClient::resetConnection(CachedMdsConnection* connection)
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

int MdsIpClient::connectionCountForGroup(int requestCount)
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

int MdsIpClient::heavyThinConnectionLimit()
{
        bool ok = false;
        const int configured = qEnvironmentVariableIntValue("MDSSCOPE_HEAVY_THIN_CONNECTIONS", &ok);
        if (ok) {
            return std::clamp(configured, 1, kMaxConnectionsPerGroup);
        }
        return std::min(8, kMaxConnectionsPerGroup);
    }

bool MdsIpClient::isLikelyHeavySignal(const SignalSpec& sig)
{
        const QString experiment = sig.experiment.trimmed().toLower();
        const QString y = normalizedMdsSignal(sig.yExpr).toLower();
        return experiment == "east" && (y.startsWith("\\hrs") || y.startsWith("hrs"));
    }

bool MdsIpClient::prefersEastTimeContext(const QString& baseExpr)
{
        const QString y = baseExpr.trimmed().toLower();
        return y.startsWith("\\hrs")
               || y.startsWith("\\ssnpa");
    }

bool MdsIpClient::chunkHasLikelyHeavySignal(const QVector<NativeRequest>& chunk)
{
        return std::any_of(chunk.begin(), chunk.end(), [](const NativeRequest& request) {
            return isLikelyHeavySignal(request.sig);
        });
    }

void MdsIpClient::prioritizeConnectionChunks(QVector<QVector<NativeRequest>>* chunks)
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

void MdsIpClient::appendConnectionChunks(const QVector<NativeRequest>& requests, QVector<QVector<NativeRequest>>* chunks)
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

        QVector<NativeRequest> heavy;
        QVector<NativeRequest> normal;
        heavy.reserve(heavyCount);
        normal.reserve(requests.size());
        for (const NativeRequest& request : requests) {
            if (isLikelyHeavySignal(request.sig)) {
                heavy.push_back(request);
            } else {
                normal.push_back(request);
            }
        }

        if (!heavy.isEmpty()) {
            const int bucketCount = std::min(static_cast<int>(heavy.size()), heavyThinConnectionLimit());
            QVector<QVector<NativeRequest>> heavyBuckets(bucketCount);
            for (int i = 0; i < heavy.size(); ++i) {
                heavyBuckets[i % bucketCount].push_back(heavy[i]);
            }
            for (QVector<NativeRequest>& bucket : heavyBuckets) {
                if (!bucket.isEmpty()) {
                    chunks->push_back(std::move(bucket));
                }
            }
        }

        if (normal.isEmpty()) {
            return;
        }

        const QString experiment = normal.front().sig.experiment.trimmed().toLower();
        if (experiment == "east" && normal.size() > 16) {
            int bucketCount = 2;
            if (normal.size() > 48) {
                bucketCount = 6;
            } else if (normal.size() > 32) {
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

void MdsIpClient::fetchGroup(const QVector<NativeRequest>& requests, QVector<LoadedSignal>* loaded) const
{
        applyFetchResults(fetchGroupResults(requests), loaded);
    }

QVector<SignalFetchResult> MdsIpClient::groupErrorResults(const QVector<NativeRequest>& requests, const QString& error)
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

void MdsIpClient::applyFetchResults(const QVector<SignalFetchResult>& results, QVector<LoadedSignal>* loaded)
{
        for (const SignalFetchResult& result : results) {
            if (result.loadedIndex >= 0 && result.loadedIndex < loaded->size()) {
                (*loaded)[result.loadedIndex].series = result.series;
            }
        }
    }

bool MdsIpClient::isPermanentMdsError(const QString& error)
{
        const QString e = error.toLower();
        return e.contains("node not found")
               || e.contains("no data available")
               || e.contains("missing server/tree/shot/signal")
               || e.contains("%tree-w-nnf")
               || e.contains("%tree-e-nodata");
    }

bool MdsIpClient::shouldRetrySignal(const LoadedSignal& item)
{
        if (item.series.hasData()) {
            return false;
        }
        if (item.series.error.isEmpty()) {
            return true;
        }
        return !isPermanentMdsError(item.series.error);
    }

bool MdsIpClient::shouldStreamResult(const SignalFetchResult& result)
{
        if (result.series.hasData()) {
            return true;
        }
        return !result.series.error.isEmpty() && isPermanentMdsError(result.series.error);
    }

void MdsIpClient::retryTransientFailures(const QHash<int, NativeRequest>& requestsByLoadedIndex, QVector<LoadedSignal>* loaded) const
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
                futures.push_back(QtConcurrent::run(groupFetchPool(), [this, single = std::move(single)] {
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

LoadedSignal MdsIpClient::loadedSignalFromResult(const NativeRequest& request, const SignalFetchResult& result) const
{
        LoadedSignal loaded;
        loaded.column = request.column;
        loaded.row = request.row;
        loaded.signal = request.signal;
        loaded.shot = request.shot;
        loaded.series = result.series;
        return loaded;
    }

void MdsIpClient::emitResult(const NativeRequest& request, const SignalFetchResult& result) const
{
        if (callback_ && !isCanceled() && shouldStreamResult(result)) {
            callback_(loadedSignalFromResult(request, result));
        }
    }

void MdsIpClient::emitResults(const QVector<NativeRequest>& requests, const QVector<SignalFetchResult>& results) const
{
        if (!callback_ || isCanceled()) {
            return;
        }
        QHash<int, const NativeRequest*> byIndex;
        byIndex.reserve(requests.size());
        for (const NativeRequest& request : requests) {
            byIndex.insert(request.loadedIndex, &request);
        }
        for (const SignalFetchResult& result : results) {
            if (isCanceled()) {
                return;
            }
            if (!shouldStreamResult(result)) {
                continue;
            }
            if (const NativeRequest* request = byIndex.value(result.loadedIndex, nullptr)) {
                callback_(loadedSignalFromResult(*request, result));
            }
        }
    }


} // namespace mds_client_internal
