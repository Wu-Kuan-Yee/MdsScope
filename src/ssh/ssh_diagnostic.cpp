// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ssh_diagnostic.hpp"
#include "ssh_tunnel_manager.hpp"
#include "mds_client.hpp"

#include <QTextStream>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
QJsonObject postJson(const QString& url,
                     const QByteArray& authorization,
                     const QJsonObject& payload,
                     QString* error)
{
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
    request.setRawHeader("Authorization", authorization);
    request.setRawHeader("User-Agent", "MdsScope/ssh-diagnostic");
    request.setTransferTimeout(5000);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(5000);
    loop.exec();
    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        *error = QStringLiteral("request timed out");
        return {};
    }
    const QByteArray body = reply->readAll();
    const auto replyError = reply->error();
    const QString replyErrorText = reply->errorString();
    reply->deleteLater();
    if (replyError != QNetworkReply::NoError) {
        *error = replyErrorText;
        return {};
    }
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    if (object.isEmpty()) {
        *error = QStringLiteral("empty or invalid JSON response");
    }
    return object;
}

QString scalarText(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 8);
    }
    return {};
}
}

int runSshTunnelBenchmark(const QString& configPath, const QString& shotOverride)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    LayoutConfig source = parseEnvironment(configPath);
    if (!shotOverride.trimmed().isEmpty()) {
        for (auto& column : source.columns) {
            for (PlotSpec& plot : column) {
                plot.shot = shotOverride.trimmed();
                for (SignalSpec& signal : plot.signalSpecs) {
                    signal.shot.clear();
                }
            }
        }
    }
    source = expandedShotLayout(source);

    SshTunnelManager manager;
    if (manager.settings().mode == SshMode::Disabled || manager.settings().host.trimmed().isEmpty()) {
        err << "SSH is disabled or not configured in the encrypted user cache." << Qt::endl;
        return 2;
    }

    LayoutConfig prepared;
    QString error;
    if (!manager.prepareLayout(source, &prepared, &error)) {
        err << "SSH tunnel setup failed: " << error << Qt::endl;
        return 3;
    }

    int ok = 0;
    int empty = 0;
    int failed = 0;
    qint64 points = 0;
    const QVector<LoadedSignal> loaded = fetchMdsSignals(prepared, DataReadMode::Thin);
    for (const LoadedSignal& item : loaded) {
        points += item.series.pointCount();
        if (!item.series.error.isEmpty()) {
            ++failed;
        } else if (!item.series.hasData()) {
            ++empty;
        } else {
            ++ok;
        }
    }

    out << "ssh-tunnel"
        << "\tok=" << ok
        << "\tempty=" << empty
        << "\tfailed=" << failed
        << "\tsignals=" << loaded.size()
        << "\tpoints=" << points << Qt::endl;
    return ok > 0 ? 0 : 1;
}

int runSshApiTest(const QString& rootPath)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    const auto properties = readApiSettings(rootPath);
    const QString originalApi = properties.value(QStringLiteral("ApiUrl")).trimmed();
    const QString token = properties.value(QStringLiteral("Token")).trimmed();
    if (originalApi.isEmpty() || token.isEmpty()) {
        err << "API URL or cached token is missing." << Qt::endl;
        return 2;
    }

    SshTunnelManager manager;
    QString api;
    QString error;
    if (!manager.prepareUrl(originalApi, &api, &error)) {
        err << "SSH API tunnel setup failed: " << error << Qt::endl;
        return 3;
    }
    const QString prefix = properties.value(QStringLiteral("Authorization_Prefix"), QStringLiteral("Bearer"));
    const QByteArray authorization = (prefix + ' ' + token).toUtf8();
    const QJsonObject latestRoot = postJson(api + QStringLiteral("/treeShot"), authorization, {}, &error);
    const QString latest = firstShotFromJsonValue(latestRoot.value(QStringLiteral("data")));
    if (latest.isEmpty()) {
        err << "Latest shot request failed: " << error << Qt::endl;
        return 4;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("treeshot"), latest.toInt());
    const QJsonObject summaryRoot = postJson(api + QStringLiteral("/pcsEastTree"), authorization, payload, &error);
    const QJsonObject data = summaryRoot.value(QStringLiteral("data")).toObject();
    if (data.isEmpty()) {
        err << "Shot summary request failed: " << error << Qt::endl;
        return 5;
    }
    out << "ssh-api"
        << "\tlatest=" << latest
        << "\tip=" << scalarText(data.value(QStringLiteral("pcrl01")))
        << "\tpulse=" << scalarText(data.value(QStringLiteral("shot_len")))
        << "\tit=" << scalarText(data.value(QStringLiteral("iv")))
        << "\ttime=" << scalarText(data.value(QStringLiteral("curr_time"))) << Qt::endl;
    return 0;
}
