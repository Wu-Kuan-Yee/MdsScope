// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "about_dialog.hpp"
#include "shared.hpp"


void MainWindow::applyShot()
{
    if (!shotEdit_) {
        return;
    }
    const QString shot = shotEdit_->text().trimmed();
    if (shot.isEmpty()) {
        return;
    }
    if (shotEdit_->text() != shot) {
        shotEdit_->setText(shot);
    }
    rememberShotExpression(shot);
    ++latestShotGeneration_;
    setAllPlotShots(shot);
    refreshData();
}

void MainWindow::stepShot(int delta)
{
    if (!shotEdit_) {
        return;
    }
    bool ok = false;
    const int shot = shotEdit_->text().trimmed().toInt(&ok);
    if (!ok) {
        return;
    }
    int next = std::max(0, shot + delta);
    if (delta > 0) {
        bool latestOk = false;
        const int latest = latestShot_.trimmed().toInt(&latestOk);
        if (latestOk && next > latest) {
            if (shot != latest && shotEdit_->text() != latestShot_) {
                shotEdit_->setText(latestShot_);
                applyShot();
            } else {
                setStatus(QString("Already at latest shot %1").arg(latestShot_));
            }
            return;
        }
    }
    const QString nextShot = QString::number(next);
    if (shotEdit_->text() != nextShot) {
        shotEdit_->setText(nextShot);
    }
    applyShot();
}

void MainWindow::latestShot()
{
    // Latest is an active refresh, not a jump to a possibly stale cached value.
    fetchLatestShotAsync(true);
}

void MainWindow::openLoginDialog()
{
    QString apiUrl;
    if (!prepareSshUrl(readApiUrl(rootPath_), &apiUrl)) {
        return;
    }
    LoginDialog dialog(rootPath_, this, apiUrl);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    applyLoginSuccessStatus("Login token saved");
}

void MainWindow::openAboutDialog()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::applyLoginSuccessStatus(const QString& statusText)
{
    updateLoginActionIcon();
    latestShot_.clear();
    pendingTopSummaryShot_.clear();
    topSummaryShot_.clear();
    topSummaryIp_.clear();
    topSummaryPulse_.clear();
    topSummaryIt_.clear();
    topSummaryTime_.clear();
    ++topSummaryGeneration_;
    updateTopInfoLabels();
    setStatus(statusText);
}

void MainWindow::updateLoginActionIcon()
{
    if (!loginAction_) {
        return;
    }

    CachedAuth auth;
    const bool loggedIn = loadCachedAuth(&auth)
        && !auth.token.trimmed().isEmpty()
        && !tokenExpiresSoon(auth.token);
    loginAction_->setIcon(loginIcon(loggedIn));
}

void MainWindow::fetchLatestShotAsync(bool applyLatest)
{
    if (applyLatest) {
        latestShotApplyPending_ = true;
        setStatus("Fetching latest shot...");
    }
    if (latestShotFetchRunning_) {
        return;
    }
    latestShotFetchRunning_ = true;
    const int generation = ++latestShotGeneration_;
    QString apiUrl;
    if (!prepareSshUrl(readApiUrl(rootPath_), &apiUrl)) {
        latestShotFetchRunning_ = false;
        const bool shouldApply = latestShotApplyPending_;
        latestShotApplyPending_ = false;
        if (shouldApply) {
            setStatus("Latest shot unavailable through SSH");
        }
        return;
    }
    QThreadPool::globalInstance()->start([this, generation, apiUrl] {
        const QString latest = latestShotFromApi(apiUrl);
        QMetaObject::invokeMethod(this, [this, latest, generation] {
            if (generation != latestShotGeneration_) {
                latestShotFetchRunning_ = false;
                latestShotApplyPending_ = false;
                return;
            }
            latestShotFetchRunning_ = false;
            const bool shouldApply = latestShotApplyPending_;
            latestShotApplyPending_ = false;
            if (latest.isEmpty()) {
                cachedApiSourceUrl_.clear();
                cachedPreparedApiUrl_.clear();
                if (shouldApply) {
                    pendingPrewarmRefresh_ = false;
                    setStatus("Latest shot unavailable");
                }
                return;
            }
            latestShot_ = latest;
            if (!shouldApply) {
                return;
            }
            if (shotEdit_ && shotEdit_->text() != latestShot_) {
                shotEdit_->setText(latestShot_);
            }
            if (pendingPrewarmRefresh_) {
                rememberShotExpression(latestShot_);
                setAllPlotShots(latestShot_);
                setStatus(QString("Preparing MDS connections for shot %1...").arg(latestShot_));
                if (prewarmConnections()) {
                    return;
                }
                pendingPrewarmRefresh_ = false;
            }
            applyShot();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::updateShotControlsFromConfig(const QString& preferredShot)
{
    QString shot = preferredShot.trimmed();
    if (shot.isEmpty()) {
        shot = maxShotInConfig();
    }
    if (!shot.isEmpty()) {
        if (shotEdit_) {
            shotEdit_->setText(shot);
        }
        ++latestShotGeneration_;
        setAllPlotShots(shot);
    }
}

void MainWindow::setAllPlotShots(const QString& shot)
{
    bool changed = false;
    for (auto& col : config_.columns) {
        for (PlotSpec& plot : col) {
            if (plot.shot != shot) {
                plot.shot = shot;
                changed = true;
            }
        }
    }
    if (changed) {
        syncDisplayConfig();
    }
    updateTopInfoLabels();
    setStatus(QString("ShotNo:%1").arg(shot));
}

QString MainWindow::maxShotInConfig() const
{
    int best = -1;
    QString bestText;
    for (const auto& col : config_.columns) {
        for (const PlotSpec& plot : col) {
            bool ok = false;
            const int shot = plot.shot.trimmed().toInt(&ok);
            if (ok && shot > best) {
                best = shot;
                bestText = plot.shot.trimmed();
            }
        }
    }
    return bestText;
}

QString MainWindow::latestShotFromApi(const QString& apiOverride) const
{
    const auto properties = readApiSettings(rootPath_);
    const QString api = apiOverride.trimmed().isEmpty() ? properties.value("ApiUrl") : apiOverride.trimmed();
    const QString token = properties.value("Token");
    const QString prefix = properties.value("Authorization_Prefix", properties.value("Init_Prefix", "Bearer"));
    const QString charset = properties.value("Charset", "UTF-8");
    if (api.isEmpty() || token.isEmpty()) {
        return {};
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(api + "/treeShot"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=" + charset);
    request.setRawHeader("Authorization", (prefix + " " + token).toUtf8());
    request.setRawHeader("User-Agent", "MdsScope/0.1");
    request.setTransferTimeout(4000);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply = manager.post(request, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(4000);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        return {};
    }

    const QByteArray body = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error != QNetworkReply::NoError || body.isEmpty()) {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        QString shot = firstShotFromJsonValue(obj.value("data"));
        if (!shot.isEmpty()) {
            return shot;
        }
        shot = firstShotFromJsonValue(obj);
        if (!shot.isEmpty()) {
            return shot;
        }
    } else if (doc.isArray()) {
        const QString shot = firstShotFromJsonValue(doc.array());
        if (!shot.isEmpty()) {
            return shot;
        }
    }
    return firstShotLikeText(QString::fromUtf8(body));
}

bool MainWindow::loadShotSummaryFromApi(const QString& shot,
                                        QString* ip,
                                        QString* pulse,
                                        QString* it,
                                        QString* time,
                                        const QString& apiOverride) const
{
    const auto properties = readApiSettings(rootPath_);
    const QString api = apiOverride.trimmed().isEmpty() ? properties.value("ApiUrl") : apiOverride.trimmed();
    const QString token = properties.value("Token");
    const QString prefix = properties.value("Authorization_Prefix", properties.value("Init_Prefix", "Bearer"));
    const QString charset = properties.value("Charset", "UTF-8");
    if (api.isEmpty() || token.isEmpty() || shot.trimmed().isEmpty()) {
        return false;
    }

    bool shotOk = false;
    const int shotNumber = shot.trimmed().toInt(&shotOk);
    if (!shotOk) {
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(api + "/pcsEastTree"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=" + charset);
    request.setRawHeader("Authorization", (prefix + " " + token).toUtf8());
    request.setRawHeader("User-Agent", "MdsScope/0.1");
    request.setTransferTimeout(2500);

    QJsonObject payload;
    payload.insert("treeshot", shotNumber);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(2500);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        return false;
    }

    const QByteArray body = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error != QNetworkReply::NoError || body.isEmpty()) {
        return false;
    }

    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QString code = root.value("code").isString()
        ? root.value("code").toString()
        : QString::number(root.value("code").toInt());
    if (code != "20000") {
        return false;
    }
    const QJsonObject data = root.value("data").toObject();
    if (data.isEmpty()) {
        return false;
    }

    auto scalarText = [](const QJsonValue& value) {
        if (value.isString()) {
            return value.toString().trimmed();
        }
        if (value.isDouble()) {
            return QString::number(value.toDouble(), 'g', 8);
        }
        if (value.isBool()) {
            return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        }
        return QString();
    };

    if (ip) {
        *ip = scalarText(data.value("pcrl01"));
    }
    if (pulse) {
        *pulse = scalarText(data.value("shot_len"));
    }
    if (it) {
        *it = scalarText(data.value("iv"));
    }
    if (time) {
        *time = scalarText(data.value("curr_time"));
    }
    return true;
}
