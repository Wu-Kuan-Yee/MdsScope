// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "ssh_tunnel_manager.hpp"

#include <QMessageBox>

namespace {
QString httpRedirectTarget(const QString& source, const QString& prepared)
{
    const QUrl sourceUrl(source);
    if (sourceUrl.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0) {
        return {};
    }

    QNetworkAccessManager manager;
    manager.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request{QUrl(prepared)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(3000);
    request.setRawHeader("Host", sourceUrl.authority(QUrl::FullyEncoded).toUtf8());

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply = manager.head(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(3000);
    loop.exec();
    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        return {};
    }

    const QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    reply->deleteLater();
    if (target.isEmpty()) {
        return {};
    }
    return sourceUrl.resolved(target).toString(QUrl::FullyEncoded);
}
}

void MainWindow::openInternalWebPage(const QString& source)
{
    QString prepared = source;
    QString error;
    const bool useSsh = sshTunnelManager_
                        && sshTunnelManager_->state() == SshTunnelManager::State::Connected;

    auto prepareUrl = [&](const QString& url) {
        if (!useSsh) {
            prepared = url;
            return true;
        }
        const bool ok = sshTunnelManager_->prepareUrlViaSsh(url, &prepared, &error);
        updateSshActionIcon();
        return ok;
    };

    if (!prepareUrl(source)) {
        QMessageBox::warning(this,
                             QStringLiteral("Internal Web Access"),
                             error.isEmpty() ? QStringLiteral("Could not establish the SSH tunnel.") : error);
        setStatus(QStringLiteral("Internal web access failed"));
        return;
    }

    if (useSsh) {
        QString currentSource = source;
        for (int redirect = 0; redirect < 5; ++redirect) {
            const QString target = httpRedirectTarget(currentSource, prepared);
            if (target.isEmpty()) {
                break;
            }
            if (!prepareUrl(target)) {
                QMessageBox::warning(this,
                                     QStringLiteral("Internal Web Access"),
                                     error.isEmpty()
                                         ? QStringLiteral("Could not forward the redirected address.")
                                         : error);
                setStatus(QStringLiteral("Internal web redirect failed"));
                return;
            }
            currentSource = target;
        }
    }

    if (!QDesktopServices::openUrl(QUrl(prepared))) {
        QMessageBox::warning(this,
                             QStringLiteral("Internal Web Access"),
                             QStringLiteral("Could not open the system default browser."));
        setStatus(QStringLiteral("Could not open the default browser"));
        return;
    }
    setStatus(QStringLiteral("Opened internal web page"));
}
