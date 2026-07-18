// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mdsscope_app.hpp"
#include "mdsscope_internal.hpp"

#include <QObject>
#include <QHash>

class QProcess;
class QTcpServer;
#ifdef MDSSCOPE_HAS_LIBSSH2
struct _LIBSSH2_SESSION;
typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;
#endif

class SshTunnelManager final : public QObject {
    Q_OBJECT

public:
    enum class State {
        Unconfigured = 0,
        Ready = 1,
        Connecting = 2,
        Connected = 3,
        Error = 4,
    };

    explicit SshTunnelManager(QObject* parent = nullptr);
    ~SshTunnelManager() override;

    void reloadSettings();
    const SshSettings& settings() const { return settings_; }
    State state() const { return state_; }
    QString lastError() const { return lastError_; }

    bool testConnection(const SshSettings& settings, QString* error);
    bool prepareLayout(const LayoutConfig& source, LayoutConfig* prepared, QString* error);
    bool prepareUrl(const QString& source, QString* prepared, QString* error);
    bool prepareUrlViaSsh(const QString& source, QString* prepared, QString* error);
    void disconnectAll();

signals:
    void stateChanged(SshTunnelManager::State state, const QString& detail);

private:
    struct Tunnel {
        QString endpoint;
        QString host;
        int remotePort = 8000;
        int localPort = 0;
#ifdef MDSSCOPE_HAS_LIBSSH2
        LIBSSH2_SESSION* session = nullptr;
#endif
        int sshFd = -1;
        QTcpServer* localServer = nullptr;
    };

    void cleanupTunnel(const Tunnel& tunnel);

    static bool splitEndpoint(const QString& endpoint, QString* host, int* port);
    static bool tcpReachable(const QString& host, int port, int timeoutMs);
    static int reserveLocalPort();

    bool ensureTunnel(const QString& endpoint, QString* localEndpoint, QString* error);
    bool prepareUrlImpl(const QString& source, QString* prepared, QString* error, bool allowDirect);
    void setState(State state, const QString& detail = {});

    SshSettings settings_;
    State state_ = State::Unconfigured;
    QString lastError_;
    QHash<QString, Tunnel> tunnels_;
    bool preparationInProgress_ = false;
};
