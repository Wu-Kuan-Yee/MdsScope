// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ssh_tunnel_manager.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>

SshTunnelManager::SshTunnelManager(QObject* parent)
    : QObject(parent)
{
    reloadSettings();
}

SshTunnelManager::~SshTunnelManager()
{
    disconnectAll();
}

void SshTunnelManager::reloadSettings()
{
    CachedAuth auth;
    settings_ = loadCachedAuth(&auth) ? auth.ssh : SshSettings{};
    if (settings_.mode == SshMode::Disabled || settings_.host.trimmed().isEmpty()) {
        setState(State::Unconfigured);
    } else if (tunnels_.isEmpty()) {
        setState(State::Ready);
    }
}

bool SshTunnelManager::splitEndpoint(const QString& endpoint, QString* host, int* port)
{
    QString value = endpoint.trimmed();
    if (value.isEmpty()) {
        return false;
    }
    *port = 8000;
    if (value.startsWith('[')) {
        const int close = value.indexOf(']');
        if (close <= 1) {
            return false;
        }
        *host = value.mid(1, close - 1);
        if (close + 1 < value.size() && value.at(close + 1) == ':') {
            bool ok = false;
            const int parsed = value.mid(close + 2).toInt(&ok);
            if (!ok || parsed < 1 || parsed > 65535) {
                return false;
            }
            *port = parsed;
        }
        return true;
    }
    const int colon = value.lastIndexOf(':');
    if (colon > 0 && value.indexOf(':') == colon) {
        bool ok = false;
        const int parsed = value.mid(colon + 1).toInt(&ok);
        if (ok && parsed >= 1 && parsed <= 65535) {
            value = value.left(colon);
            *port = parsed;
        }
    }
    *host = value;
    return !host->isEmpty();
}

bool SshTunnelManager::tcpReachable(const QString& host, int port, int timeoutMs)
{
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    socket.connectToHost(host, static_cast<quint16>(port));
    return socket.waitForConnected(timeoutMs);
}

int SshTunnelManager::reserveLocalPort()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return static_cast<int>(server.serverPort());
}

QString SshTunnelManager::sshTarget(const SshSettings& settings)
{
    const QString host = settings.host.trimmed();
    return settings.user.trimmed().isEmpty() ? host : settings.user.trimmed() + '@' + host;
}

QStringList SshTunnelManager::commonArguments(const SshSettings& settings, bool tunnel)
{
    QStringList args{
        QStringLiteral("-C"),
        QStringLiteral("-T"),
        QStringLiteral("-p"), QString::number(settings.port),
        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=8"),
        QStringLiteral("-o"), QStringLiteral("ServerAliveInterval=30"),
        QStringLiteral("-o"), QStringLiteral("ServerAliveCountMax=3"),
        QStringLiteral("-o"), QStringLiteral("NumberOfPasswordPrompts=1"),
    };
    if (tunnel) {
        args << QStringLiteral("-N")
             << QStringLiteral("-o") << QStringLiteral("ExitOnForwardFailure=yes");
    }
    if (settings.password.isEmpty() && !settings.identityFile.trimmed().isEmpty()) {
        QString identity = settings.identityFile.trimmed();
        if (identity == QStringLiteral("~")) {
            identity = QDir::homePath();
        } else if (identity.startsWith(QStringLiteral("~/"))) {
            identity = QDir::home().filePath(identity.mid(2));
        }
        args << QStringLiteral("-i") << identity;
    }
    if (settings.password.isEmpty()) {
        args << QStringLiteral("-o") << QStringLiteral("BatchMode=yes");
    } else {
        args << QStringLiteral("-o") << QStringLiteral("PreferredAuthentications=password,keyboard-interactive");
    }
    return args;
}

void SshTunnelManager::configureAskPass(QProcess* process, const SshSettings& settings)
{
    if (!process || settings.password.isEmpty()) {
        return;
    }
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("SSH_ASKPASS"), QCoreApplication::applicationFilePath());
    environment.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
    environment.insert(QStringLiteral("MDSSCOPE_SSH_ASKPASS"), QStringLiteral("1"));
    if (!environment.contains(QStringLiteral("DISPLAY"))) {
        environment.insert(QStringLiteral("DISPLAY"), QStringLiteral(":0"));
    }
    process->setProcessEnvironment(environment);
}

bool SshTunnelManager::testConnection(const SshSettings& settings, QString* error)
{
    if (settings.host.trimmed().isEmpty()) {
        *error = QStringLiteral("SSH host is required.");
        return false;
    }
    setState(State::Connecting, QStringLiteral("Testing SSH connection..."));
    QProcess process;
    configureAskPass(&process, settings);
    QStringList args = commonArguments(settings, false);
    args << sshTarget(settings) << QStringLiteral("true");
    process.start(QStringLiteral("ssh"), args);
    if (!process.waitForStarted(3000)) {
        *error = process.errorString();
        setState(State::Error, *error);
        return false;
    }
    if (!process.waitForFinished(12000)) {
        process.kill();
        process.waitForFinished(1000);
        *error = QStringLiteral("SSH connection timed out.");
        setState(State::Error, *error);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (error->isEmpty()) {
            *error = QStringLiteral("SSH exited with code %1.").arg(process.exitCode());
        }
        setState(State::Error, *error);
        return false;
    }
    setState(State::Ready, QStringLiteral("SSH login succeeded"));
    return true;
}

bool SshTunnelManager::ensureTunnel(const QString& endpoint, QString* localEndpoint, QString* error)
{
    auto existing = tunnels_.find(endpoint);
    if (existing != tunnels_.end() && existing->process
        && existing->process->state() != QProcess::NotRunning) {
        *localEndpoint = QStringLiteral("127.0.0.1:%1").arg(existing->localPort);
        return true;
    }

    QString remoteHost;
    int remotePort = 8000;
    if (!splitEndpoint(endpoint, &remoteHost, &remotePort)) {
        *error = QStringLiteral("Invalid MDS server address: %1").arg(endpoint);
        return false;
    }
    const int localPort = reserveLocalPort();
    if (localPort <= 0) {
        *error = QStringLiteral("Cannot allocate a local SSH forwarding port.");
        return false;
    }

    setState(State::Connecting, QStringLiteral("Connecting through SSH..."));
    auto* process = new QProcess(this);
    configureAskPass(process, settings_);
    QStringList args = commonArguments(settings_, true);
    const QString forwardingHost = remoteHost.contains(':')
                                       ? QStringLiteral("[%1]").arg(remoteHost)
                                       : remoteHost;
    args << QStringLiteral("-L")
         << QStringLiteral("127.0.0.1:%1:%2:%3").arg(localPort).arg(forwardingHost).arg(remotePort)
         << sshTarget(settings_);
    process->start(QStringLiteral("ssh"), args);
    if (!process->waitForStarted(3000)) {
        *error = process->errorString();
        process->deleteLater();
        setState(State::Error, *error);
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 9000 && process->state() != QProcess::NotRunning) {
        if (tcpReachable(QStringLiteral("127.0.0.1"), localPort, 100)) {
            Tunnel tunnel;
            tunnel.endpoint = endpoint;
            tunnel.host = remoteHost;
            tunnel.remotePort = remotePort;
            tunnel.localPort = localPort;
            tunnel.process = process;
            tunnels_.insert(endpoint, tunnel);
            connect(process, &QProcess::finished, this, [this, endpoint, process](int, QProcess::ExitStatus) {
                tunnels_.remove(endpoint);
                setState(State::Error, QStringLiteral("SSH tunnel disconnected"));
                process->deleteLater();
            });
            *localEndpoint = QStringLiteral("127.0.0.1:%1").arg(localPort);
            setState(State::Connected, QStringLiteral("SSH tunnel connected"));
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 25);
        QThread::msleep(25);
    }

    const QString stderrText = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
    process->kill();
    process->waitForFinished(1000);
    process->deleteLater();
    *error = stderrText.isEmpty() ? QStringLiteral("SSH tunnel setup timed out.") : stderrText;
    setState(State::Error, *error);
    return false;
}

bool SshTunnelManager::prepareLayout(const LayoutConfig& source, LayoutConfig* prepared, QString* error)
{
    if (!prepared) {
        return false;
    }
    *prepared = source;
    reloadSettings();
    if (settings_.mode == SshMode::Disabled) {
        return true;
    }
    if (settings_.host.trimmed().isEmpty()) {
        *error = QStringLiteral("SSH is enabled but no SSH host is configured.");
        setState(State::Error, *error);
        return false;
    }

    QHash<QString, QString> mapped;
    for (auto& column : prepared->columns) {
        for (PlotSpec& plot : column) {
            for (SignalSpec& signal : plot.signalSpecs) {
                const QString endpoint = signal.serverIp.trimmed();
                if (endpoint.isEmpty() || mapped.contains(endpoint)) {
                    if (mapped.contains(endpoint)) {
                        signal.serverIp = mapped.value(endpoint);
                    }
                    continue;
                }
                QString host;
                int port = 8000;
                if (!splitEndpoint(endpoint, &host, &port)) {
                    continue;
                }
                if (settings_.mode == SshMode::Auto && tcpReachable(host, port, 450)) {
                    mapped.insert(endpoint, endpoint);
                    continue;
                }
                QString localEndpoint;
                if (!ensureTunnel(endpoint, &localEndpoint, error)) {
                    return false;
                }
                mapped.insert(endpoint, localEndpoint);
                signal.serverIp = localEndpoint;
            }
        }
    }
    return true;
}

bool SshTunnelManager::prepareUrl(const QString& source, QString* prepared, QString* error)
{
    if (!prepared) {
        return false;
    }
    *prepared = source;
    const QUrl url(source);
    if (!url.isValid() || url.host().isEmpty()) {
        *error = QStringLiteral("Invalid API URL.");
        return false;
    }

    reloadSettings();
    if (settings_.mode == SshMode::Disabled) {
        return true;
    }
    if (settings_.host.trimmed().isEmpty()) {
        *error = QStringLiteral("SSH is enabled but no SSH host is configured.");
        return false;
    }

    const int remotePort = url.port(url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 ? 443 : 80);
    const QString endpoint = url.host().contains(':')
                                 ? QStringLiteral("[%1]:%2").arg(url.host()).arg(remotePort)
                                 : QStringLiteral("%1:%2").arg(url.host()).arg(remotePort);

    auto existing = tunnels_.find(endpoint);
    QString localEndpoint;
    if (existing != tunnels_.end() && existing->process
        && existing->process->state() != QProcess::NotRunning) {
        localEndpoint = QStringLiteral("127.0.0.1:%1").arg(existing->localPort);
    } else {
        if (settings_.mode == SshMode::Auto && tcpReachable(url.host(), remotePort, 450)) {
            return true;
        }
        if (url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0) {
            *error = QStringLiteral("Automatic API tunneling currently supports HTTP URLs only.");
            return false;
        }
        if (!ensureTunnel(endpoint, &localEndpoint, error)) {
            return false;
        }
    }

    QString localHost;
    int localPort = 0;
    if (!splitEndpoint(localEndpoint, &localHost, &localPort)) {
        *error = QStringLiteral("Invalid local SSH tunnel endpoint.");
        return false;
    }
    QUrl tunneled = url;
    tunneled.setHost(localHost);
    tunneled.setPort(localPort);
    *prepared = tunneled.toString(QUrl::FullyEncoded);
    return true;
}

void SshTunnelManager::disconnectAll()
{
    const auto tunnels = tunnels_;
    tunnels_.clear();
    for (const Tunnel& tunnel : tunnels) {
        if (!tunnel.process) {
            continue;
        }
        tunnel.process->disconnect(this);
        if (tunnel.process->state() != QProcess::NotRunning) {
            tunnel.process->terminate();
            if (!tunnel.process->waitForFinished(1000)) {
                tunnel.process->kill();
                tunnel.process->waitForFinished(1000);
            }
        }
        delete tunnel.process;
    }
    setState(settings_.mode == SshMode::Disabled || settings_.host.isEmpty() ? State::Unconfigured : State::Ready);
}

void SshTunnelManager::setState(State state, const QString& detail)
{
    state_ = state;
    lastError_ = state == State::Error ? detail : QString();
    emit stateChanged(state_, detail);
}
