// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mdsscope_internal.h"
#include "mds_client.h"

namespace mds_client_internal {

class MdsIpClient {
public:
    struct NativeRequest {
        int loadedIndex = -1;
        int column = -1;
        int row = -1;
        int signal = -1;
        QString shot;
        PlotSpec plot;
        SignalSpec sig;
        DataReadMode readMode = DataReadMode::Thin;
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

    using ResultCallback = std::function<void(const LoadedSignal&)>;

    explicit MdsIpClient(DataReadMode readMode = DataReadMode::Thin,
                         ResultCallback callback = {},
                         std::shared_ptr<std::atomic_bool> cancel = {});

    static void clearCurrentThreadConnections();

    QVector<LoadedSignal> fetchAll(const LayoutConfig& snapshot) const;

    void warmConnections(const LayoutConfig& snapshot) const;

    SignalSeries fetch(const PlotSpec& plot, const SignalSpec& sig) const;

private:
    DataReadMode readMode_ = DataReadMode::Thin;
    ResultCallback callback_;
    std::shared_ptr<std::atomic_bool> cancel_;
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

    bool isCanceled() const;

    class CurrentCancelGuard {
    public:
        explicit CurrentCancelGuard(const std::shared_ptr<std::atomic_bool>& cancel)
            : previous_(currentCancel())
        {
            currentCancel() = cancel.get();
        }

        ~CurrentCancelGuard()
        {
            currentCancel() = previous_;
        }

        CurrentCancelGuard(const CurrentCancelGuard&) = delete;
        CurrentCancelGuard& operator=(const CurrentCancelGuard&) = delete;

    private:
        const std::atomic_bool* previous_ = nullptr;
    };

    static const std::atomic_bool*& currentCancel();

    static bool currentCanceled();

    static QString groupKey(const NativeRequest& request);

    static int maxPointsForSignal(const PlotSpec& plot, const SignalSpec& sig);

    static QString serverHost(QString server);

    static int serverPort(const QString& server);

    static QString serverKey(const SignalSpec& sig);

    static int fullLargeDownloadLimit();

    static int fullLargeSignalPointThreshold();

    static QSemaphore* fullLargeDownloadSemaphore(const SignalSpec& sig);

    static int fullPointCountBestEffort(QTcpSocket& socket, const QString& yExpr);

    static std::unique_ptr<SemaphoreGuard> fullLargeDownloadGuard(const SignalSpec& sig, int pointCount);

    static CachedMdsConnection* threadLocalConnection(const SignalSpec& sig);

    static std::vector<std::unique_ptr<CachedMdsConnection>>& threadLocalConnections();

    static void resetConnection(CachedMdsConnection* connection);

    static int connectionCountForGroup(int requestCount);

    static int heavyThinConnectionLimit();

    static bool isLikelyHeavySignal(const SignalSpec& sig);

    static bool prefersEastTimeContext(const QString& baseExpr);

    static bool chunkHasLikelyHeavySignal(const QVector<NativeRequest>& chunk);

    static void prioritizeConnectionChunks(QVector<QVector<NativeRequest>>* chunks);

    static void appendConnectionChunks(const QVector<NativeRequest>& requests, QVector<QVector<NativeRequest>>* chunks);

    void fetchGroup(const QVector<NativeRequest>& requests, QVector<LoadedSignal>* loaded) const;

    QVector<SignalFetchResult> fetchGroupResults(const QVector<NativeRequest>& requests, bool openOnly = false) const;

    QVector<SignalFetchResult> fetchEastThinPipelinedOnOpenSocket(QTcpSocket& socket,
                                                                  const QVector<NativeRequest>& requests,
                                                                  QString* error) const;

    QVector<SignalFetchResult> fetchDirectPipelinedOnOpenSocket(QTcpSocket& socket,
                                                                const QVector<NativeRequest>& requests,
                                                                const QHash<QString, UniformTimebase>& timebaseCache,
                                                                QString* error) const;

    QVector<SignalFetchResult> fetchEastTimeContextBatchOnOpenSocket(QTcpSocket& socket,
                                                                     const QVector<NativeRequest>& requests,
                                                                     const QSet<int>& fetchedIndexes,
                                                                     QString* error) const;

    static QVector<SignalFetchResult> groupErrorResults(const QVector<NativeRequest>& requests, const QString& error);

    static void applyFetchResults(const QVector<SignalFetchResult>& results, QVector<LoadedSignal>* loaded);

    static bool isPermanentMdsError(const QString& error);

    static bool shouldRetrySignal(const LoadedSignal& item);

    static bool shouldStreamResult(const SignalFetchResult& result);

    void retryTransientFailures(const QHash<int, NativeRequest>& requestsByLoadedIndex, QVector<LoadedSignal>* loaded) const;

    LoadedSignal loadedSignalFromResult(const NativeRequest& request, const SignalFetchResult& result) const;

    void emitResult(const NativeRequest& request, const SignalFetchResult& result) const;

    void emitResults(const QVector<NativeRequest>& requests, const QVector<SignalFetchResult>& results) const;

    QVector<SignalFetchResult> fetchThinBatchOnOpenSocket(QTcpSocket& socket, const QVector<NativeRequest>& requests, QString* error) const;

    SignalSeries fetchSignalOnOpenSocket(QTcpSocket& socket,
                                         const PlotSpec& plot,
                                         const SignalSpec& sig,
                                         DataReadMode readMode,
                                         int maxPoints,
                                         const QHash<QString, UniformTimebase>* timebaseCache,
                                         QString* error) const;

    SignalSeries fetchSignalOnOpenSocketImpl(QTcpSocket& socket,
                                             const PlotSpec& plot,
                                             const SignalSpec& sig,
                                             DataReadMode readMode,
                                             int maxPoints,
                                             const QHash<QString, UniformTimebase>* timebaseCache,
                                             QString* error) const;

    // Oversampled envelope for thin/medium modes:
    // - Thin (DataReadMode::Thin): SetTimeContext 20k-pt averaging, ~4s, fast preview
    // - Medium (DataReadMode::Medium): stride 20k-pt sampling, ~8-11s, preserves spike amplitude
    SignalSeries fetchEastFullEnvelopeSignalOnOpenSocket(QTcpSocket& socket,
                                                         const PlotSpec& plot,
                                                         const SignalSpec& sig,
                                                         int maxPoints,
                                                         DataReadMode readMode,
                                                         QString* error) const;

    SignalSeries fetchEastLengthSampledSignalOnOpenSocket(QTcpSocket& socket,
                                                          const PlotSpec& plot,
                                                          const SignalSpec& sig,
                                                          int maxPoints,
                                                          QString* error) const;

    SignalSeries fetchEastTimeContextSignalOnOpenSocket(QTcpSocket& socket,
                                                        const PlotSpec& plot,
                                                        const SignalSpec& sig,
                                                        int maxPoints,
                                                        const QHash<QString, UniformTimebase>* timebaseCache,
                                                        QString* error) const;

    SignalSeries fetchSavedEastSignalOnOpenSocket(QTcpSocket& socket,
                                                  const PlotSpec& plot,
                                                  const SignalSpec& sig,
                                                  int maxPoints,
                                                  QString* error) const;

public:
    struct Message {
        qint32 status = 0;
        qint16 length = 0;
        qint8 dtype = 0;
        QByteArray body;
    };

private:
    static QByteArray message(qint8 dtype, qint8 nargs, qint8 descrIdx, quint8 messageId, const QByteArray& body);

    static bool writeMessage(QTcpSocket& socket, const QByteArray& packet, QString* error);

    static bool readFully(QTcpSocket& socket, QByteArray* out, qsizetype size, QString* error);

    static Message readMessage(QTcpSocket& socket, QString* error);

    static bool handshake(QTcpSocket& socket, QString* error);

    static quint8 nextMessageId();

    static bool sendValue(QTcpSocket& socket, const QString& expr, QString* error);

    static bool queueValue(QTcpSocket& socket, const QString& expr, QString* error);

    static bool flushQueuedValues(QTcpSocket& socket, QString* error);

    static Message value(QTcpSocket& socket, const QString& expr, QString* error);

    static QVector<double> numericFromMessage(const Message& msg, QString* error);

    static QVector<double> numericValue(QTcpSocket& socket, const QString& expr, QString* error);

    static QVector<double> cachedNumericValue(QTcpSocket& socket,
                                              const QString& expr,
                                              QHash<QString, QVector<double>>* cache,
                                              QString* error);

    static int intValue(QTcpSocket& socket, const QString& expr, QString* error);

    static QVector<int> intVectorValue(QTcpSocket& socket, const QString& expr, QString* error);

    static bool isSimpleMdsNode(const QString& expr);

    static ScaledSignalExpr scaledSimpleSignalExpr(QString expr);

    static void applySeriesScale(SignalSeries* series, const QString& name, double scale);

    static bool isEastTimebaseCandidate(const QString& shotText, const SignalSpec& sig);

    static QString eastTimebaseKey(const QString& shotText, const SignalSpec& sig);

    static QHash<QString, UniformTimebase> prefetchEastTimebases(QTcpSocket& socket, const QVector<NativeRequest>& requests);

    static UniformTimebase eastUniformTimebase(QTcpSocket& socket,
                                               const QString& shotText,
                                               const SignalSpec& sig,
                                               QString* error);

    static EastThinPlan eastThinPlan(QTcpSocket& socket,
                                     const QString& shotText,
                                     const SignalSpec& sig,
                                     int maxPoints,
                                     QString* error);

    static ThinSampling samplingFromPointCount(int pointCount, int maxPoints);

    static QVector<ThinSampling> thinSamplings(QTcpSocket& socket, const QVector<NativeRequest>& requests, QString* error);

    static QVector<ThinSampling> thinSamplingsBestEffort(QTcpSocket& socket, const QVector<NativeRequest>& requests, QString* error);

    static void fillThinSamplings(QTcpSocket& socket,
                                  const QVector<NativeRequest>& requests,
                                  int start,
                                  int count,
                                  QVector<ThinSampling>* out,
                                  QString* error);

    static ThinSampling thinSampling(QTcpSocket& socket, const QString& yExpr, int maxPoints, QString* error);

    static void minMaxDownsample(SignalSeries* series, int maxPoints);

    static void buildUniformOverview(SignalSeries* series, int maxPoints = 4000);

    static SignalSeries makeSeries(QString name, const QVector<double>& y, const QVector<double>& x, int maxPoints);

    static SignalSeries makeSeriesUniformX(QString name,
                                           const QVector<double>& y,
                                           double start,
                                           double step,
                                           int maxPoints);

    static int numericElementSize(const Message& msg);

    static double numericAt(const QByteArray& body, int elementSize, int index);

    static SignalSeries makeSeriesUniformXFromMessage(QString name,
                                                      const Message& msg,
                                                      double start,
                                                      double step,
                                                      int maxPoints,
                                                      QString* error);

    static SignalSeries makeSeriesFromAdjacentSegments(QString name, const QVector<double>& values, int offset, int count, int maxPoints);

    static SignalSeries makeSeriesFromCombined(QString name, const QVector<double>& xy, int maxPoints);
};

} // namespace mds_client_internal
