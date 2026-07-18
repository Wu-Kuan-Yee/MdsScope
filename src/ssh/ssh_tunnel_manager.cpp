// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ssh_tunnel_manager.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QScopedValueRollback>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#ifdef MDSSCOPE_HAS_LIBSSH2
#include <libssh2.h>
#endif

#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#define CLOSE_SOCKET close
#elif defined(Q_OS_WIN)
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSE_SOCKET closesocket
#endif

namespace {

#if defined(Q_OS_UNIX) || defined(Q_OS_WIN)
int connectToHostRaw(const QString& host, int port, int timeoutMs, QString* error)
{
#ifdef Q_OS_WIN
    // One-time WinSock init.
    static bool wsaInit = false;
    if (!wsaInit) { WSADATA d; WSAStartup(MAKEWORD(2,2), &d); wsaInit = true; }
#endif

    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    QByteArray hostBytes = host.toUtf8();
    QByteArray portBytes = QString::number(port).toUtf8();

    int ret = getaddrinfo(hostBytes.constData(), portBytes.constData(), &hints, &result);
    if (ret != 0) {
#ifdef Q_OS_WIN
        *error = QStringLiteral("Host resolution failed: %1").arg(ret);
#else
        *error = QStringLiteral("Host resolution failed: %1").arg(gai_strerror(ret));
#endif
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* rp = result; rp; rp = rp->ai_next) {
        fd = static_cast<int>(socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol));
        if (fd < 0) continue;

#ifdef Q_OS_UNIX
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
        u_long mode = 1;
        ioctlsocket(fd, FIONBIO, &mode);
#endif

        ret = connect(fd, rp->ai_addr, static_cast<int>(rp->ai_addrlen));
        if (ret < 0
#ifdef Q_OS_UNIX
            && errno != EINPROGRESS
#else
            && WSAGetLastError() != WSAEWOULDBLOCK
#endif
            ) { CLOSE_SOCKET(fd); fd = -1; continue; }

        if (ret < 0) {
            fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
            struct timeval tv;
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            ret = select(fd + 1, nullptr, &wfds, nullptr, &tv);
            if (ret <= 0) { CLOSE_SOCKET(fd); fd = -1; continue; }
            int err = 0; socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR,
#ifdef Q_OS_WIN
                       reinterpret_cast<char*>(&err),
#else
                       &err,
#endif
                       &len);
            if (err != 0) { CLOSE_SOCKET(fd); fd = -1; continue; }
        }

#ifdef Q_OS_UNIX
        fcntl(fd, F_SETFL, flags);
#else
        mode = 0; ioctlsocket(fd, FIONBIO, &mode);
#endif
        break;
    }

    freeaddrinfo(result);
    if (fd < 0) *error = QStringLiteral("Cannot connect to %1:%2.").arg(host).arg(port);
    return fd;
}
#endif

#ifdef MDSSCOPE_HAS_LIBSSH2

void initLibssh2()
{
    static bool initialized = false;
    if (!initialized) { libssh2_init(0); initialized = true; }
}

template <typename Func>
int runSshOpFd(LIBSSH2_SESSION* session, int fd, Func op, int timeoutMs)
{
    QElapsedTimer timer; timer.start();
    int rc;
    do {
        rc = op();
        if (rc != LIBSSH2_ERROR_EAGAIN) break;

        const int dir = libssh2_session_block_directions(session);
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0) return LIBSSH2_ERROR_TIMEOUT;

        fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
        struct timeval tv;
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        if (dir & LIBSSH2_SESSION_BLOCK_INBOUND)
            select(fd + 1, &fds, nullptr, nullptr, &tv);
        if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
            select(fd + 1, nullptr, &fds, nullptr, &tv);

        if (timer.elapsed() > timeoutMs) return LIBSSH2_ERROR_TIMEOUT;
    } while (true);
    return rc;
}

static const QByteArray* s_kbdPassword = nullptr;

static void kbdIntCallback(const char*, int, const char*, int, int numPrompts,
                           const LIBSSH2_USERAUTH_KBDINT_PROMPT*,
                           LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void**)
{
    if (!s_kbdPassword) return;
    for (int i = 0; i < numPrompts; ++i) {
        const size_t sz = static_cast<size_t>(s_kbdPassword->size());
        responses[i].text = static_cast<char*>(malloc(sz + 1));
        memcpy(responses[i].text, s_kbdPassword->constData(), sz + 1);
        responses[i].length = static_cast<unsigned int>(sz);
    }
}

bool authenticate(LIBSSH2_SESSION* session, int fd,
                  const SshSettings& settings, QString* error)
{
    const QByteArray userUtf8 = settings.user.trimmed().toUtf8();
    const QString password = settings.password;
    const QString identityFile = settings.identityFile.trimmed();

    char* methods = libssh2_userauth_list(session, userUtf8.constData(),
                                          static_cast<unsigned int>(userUtf8.size()));
    QStringList methodList;
    if (methods) methodList = QString::fromUtf8(methods).split(',');

    bool tryPublickey = methodList.isEmpty() || methodList.contains(QStringLiteral("publickey"));
    bool tryPassword = methodList.isEmpty()
        || methodList.contains(QStringLiteral("password"))
        || methodList.contains(QStringLiteral("keyboard-interactive"));

    if (tryPublickey && !identityFile.isEmpty()) {
        const QByteArray keyPath = identityFile.toUtf8();
        int rc = runSshOpFd(session, fd, [&]() {
            return libssh2_userauth_publickey_fromfile(
                session, userUtf8.constData(), nullptr,
                keyPath.constData(), nullptr);
        }, 8000);
        if (rc == 0) return true;
    }

    if (tryPublickey && password.isEmpty() && identityFile.isEmpty()) {
        LIBSSH2_AGENT* agent = libssh2_agent_init(session);
        if (agent && libssh2_agent_connect(agent) == 0
            && libssh2_agent_list_identities(agent) == 0) {
            struct libssh2_agent_publickey *identity = nullptr, *prev = nullptr;
            while (true) {
                int r = libssh2_agent_get_identity(agent, &identity, prev);
                if (r == 1 || r < 0) break;
                int authRc = runSshOpFd(session, fd, [&]() {
                    return libssh2_agent_userauth(agent, userUtf8.constData(), identity);
                }, 3000);
                if (authRc == 0) {
                    libssh2_agent_disconnect(agent);
                    libssh2_agent_free(agent);
                    return true;
                }
                prev = identity;
            }
            libssh2_agent_disconnect(agent);
        }
        if (agent) libssh2_agent_free(agent);
    }

    if (tryPassword && !password.isEmpty()) {
        const QByteArray pwd = password.toUtf8();
        int rc = runSshOpFd(session, fd, [&]() {
            return libssh2_userauth_password(
                session, userUtf8.constData(), pwd.constData());
        }, 8000);
        if (rc == 0) return true;

        s_kbdPassword = &pwd;
        rc = runSshOpFd(session, fd, [&]() {
            return libssh2_userauth_keyboard_interactive_ex(
                session, userUtf8.constData(),
                static_cast<unsigned int>(userUtf8.size()), kbdIntCallback);
        }, 8000);
        s_kbdPassword = nullptr;
        if (rc == 0) return true;
    }

    char* errMsg = nullptr;
    libssh2_session_last_error(session, &errMsg, nullptr, 0);
    *error = errMsg ? QString::fromUtf8(errMsg)
                    : QStringLiteral("SSH authentication failed. Check credentials.");
    return false;
}

LIBSSH2_SESSION* createAuthenticatedSession(const SshSettings& settings, QString* error,
                                            int* outFd)
{
    *outFd = -1;
    initLibssh2();

    int fd = connectToHostRaw(settings.host.trimmed(), settings.port, 8000, error);
    if (fd < 0) return nullptr;
    *outFd = fd;

    LIBSSH2_SESSION* session = libssh2_session_init();
    if (!session) {
        *error = QStringLiteral("Failed to initialize SSH session.");
        CLOSE_SOCKET(fd); *outFd = -1;
        return nullptr;
    }

    libssh2_session_set_blocking(session, 0);
    int rc = runSshOpFd(session, fd, [&]() {
        return libssh2_session_handshake(session, fd);
    }, 10000);

    if (rc != 0) {
        char* errMsg = nullptr;
        libssh2_session_last_error(session, &errMsg, nullptr, 0);
        if (rc == LIBSSH2_ERROR_TIMEOUT)
            *error = QStringLiteral("SSH handshake timed out.");
        else
            *error = errMsg ? QString::fromUtf8(errMsg) : QStringLiteral("SSH handshake failed.");
        libssh2_session_free(session);
        CLOSE_SOCKET(fd); *outFd = -1;
        return nullptr;
    }

    if (!authenticate(session, fd, settings, error)) {
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
        CLOSE_SOCKET(fd); *outFd = -1;
        return nullptr;
    }

    return session;
}

#else // MDSSCOPE_HAS_LIBSSH2

// Stub: SSH not available without a crypto backend.
static void* createAuthenticatedSession(const SshSettings&, QString* error, int* outFd)
{
    *outFd = -1;
    if (error) *error = QStringLiteral("SSH is not supported on this platform.");
    return nullptr;
}

#endif // MDSSCOPE_HAS_LIBSSH2

} // anonymous namespace

// -------- SshTunnelManager --------

SshTunnelManager::SshTunnelManager(QObject* parent)
    : QObject(parent) { reloadSettings(); }

SshTunnelManager::~SshTunnelManager() { disconnectAll(); }

void SshTunnelManager::reloadSettings()
{
    CachedAuth auth;
    settings_ = loadCachedAuth(&auth) ? auth.ssh : SshSettings{};
    if (settings_.mode == SshMode::Disabled || settings_.host.trimmed().isEmpty())
        setState(State::Unconfigured);
    else if (tunnels_.isEmpty())
        setState(State::Ready);
}

bool SshTunnelManager::splitEndpoint(const QString& endpoint, QString* host, int* port)
{
    QString value = endpoint.trimmed();
    if (value.isEmpty()) return false;
    *port = 8000;
    if (value.startsWith('[')) {
        const int close = value.indexOf(']');
        if (close <= 1) return false;
        *host = value.mid(1, close - 1);
        if (close + 1 < value.size() && value.at(close + 1) == ':') {
            bool ok = false;
            const int parsed = value.mid(close + 2).toInt(&ok);
            if (!ok || parsed < 1 || parsed > 65535) return false;
            *port = parsed;
        }
        return true;
    }
    const int colon = value.lastIndexOf(':');
    if (colon > 0 && value.indexOf(':') == colon) {
        bool ok = false;
        const int parsed = value.mid(colon + 1).toInt(&ok);
        if (ok && parsed >= 1 && parsed <= 65535) { value = value.left(colon); *port = parsed; }
    }
    *host = value;
    return !host->isEmpty();
}

bool SshTunnelManager::tcpReachable(const QString& host, int port, int timeoutMs)
{
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    QEventLoop loop;
    QTimer timer; timer.setSingleShot(true);
    connect(&socket, &QTcpSocket::connected, &loop, &QEventLoop::quit);
    connect(&socket, &QTcpSocket::errorOccurred, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    socket.connectToHost(host, static_cast<quint16>(port));
    if (socket.state() != QAbstractSocket::ConnectedState
        && socket.state() != QAbstractSocket::UnconnectedState) {
        timer.start(timeoutMs); loop.exec();
    }
    const bool connected = socket.state() == QAbstractSocket::ConnectedState;
    socket.abort();
    return connected;
}

int SshTunnelManager::reserveLocalPort()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return 0;
    return static_cast<int>(server.serverPort());
}

bool SshTunnelManager::testConnection(const SshSettings& settings, QString* error)
{
    if (settings.host.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("SSH host is required.");
        return false;
    }

    setState(State::Connecting, QStringLiteral("Testing SSH connection..."));

    int fd = -1;
#ifdef MDSSCOPE_HAS_LIBSSH2
    LIBSSH2_SESSION* session = createAuthenticatedSession(settings, error, &fd);
    if (session) {
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
        if (fd >= 0) CLOSE_SOCKET(fd);
        setState(State::Ready, QStringLiteral("SSH login succeeded"));
        return true;
    }
#else
    createAuthenticatedSession(settings, error, &fd);
#endif
    setState(State::Error, error ? *error : QStringLiteral("SSH connection failed."));
    return false;
}

bool SshTunnelManager::ensureTunnel(const QString& endpoint, QString* localEndpoint, QString* error)
{
    auto existing = tunnels_.find(endpoint);
    if (existing != tunnels_.end() && existing->localServer) {
        *localEndpoint = QStringLiteral("127.0.0.1:%1").arg(existing->localPort);
        return true;
    }

    QString remoteHost; int remotePort = 8000;
    if (!splitEndpoint(endpoint, &remoteHost, &remotePort)) {
        if (error) *error = QStringLiteral("Invalid MDS server address: %1").arg(endpoint);
        return false;
    }
    const int localPort = reserveLocalPort();
    if (localPort <= 0) {
        if (error) *error = QStringLiteral("Cannot allocate a local SSH forwarding port.");
        return false;
    }

    const bool keepConnectedState = !tunnels_.isEmpty();
    if (!keepConnectedState)
        setState(State::Connecting, QStringLiteral("Connecting through SSH..."));

    int sshFd = -1;
#ifdef MDSSCOPE_HAS_LIBSSH2
    LIBSSH2_SESSION* session = createAuthenticatedSession(settings_, error, &sshFd);
    if (!session) {
        if (!keepConnectedState) setState(State::Error, error ? *error : QString());
        return false;
    }
#else
    createAuthenticatedSession(settings_, error, &sshFd);
    if (!keepConnectedState) setState(State::Error, error ? *error : QString());
    return false;
#endif

    auto* localServer = new QTcpServer(this);
    if (!localServer->listen(QHostAddress::LocalHost, static_cast<quint16>(localPort))) {
        if (error) *error = localServer->errorString();
#ifdef MDSSCOPE_HAS_LIBSSH2
        libssh2_session_disconnect(session, "Bye");
        libssh2_session_free(session);
#endif
        if (sshFd >= 0) CLOSE_SOCKET(sshFd);
        delete localServer;
        if (!keepConnectedState) setState(State::Error, error ? *error : QString());
        return false;
    }

#ifdef MDSSCOPE_HAS_LIBSSH2
    QObject::connect(localServer, &QTcpServer::newConnection, this,
        [this, localServer, session, remoteHost, remotePort]()
    {
        while (localServer->hasPendingConnections()) {
            QTcpSocket* client = localServer->nextPendingConnection();
            if (!client) continue;

            LIBSSH2_CHANNEL* channel = libssh2_channel_direct_tcpip(
                session, remoteHost.toUtf8().constData(), remotePort);
            if (!channel) { client->close(); client->deleteLater(); continue; }

            QObject::connect(client, &QTcpSocket::readyRead, this,
                [this, client, channel]()
            {
                QByteArray data = client->readAll();
                while (!data.isEmpty()) {
                    qint64 written = libssh2_channel_write(
                        channel, data.constData(), static_cast<size_t>(data.size()));
                    if (written == LIBSSH2_ERROR_EAGAIN) continue;
                    if (written < 0) { client->close(); return; }
                    data.remove(0, static_cast<int>(written));
                }
            });

            auto* pollTimer = new QTimer(this);
            pollTimer->setInterval(50);
            QObject::connect(pollTimer, &QTimer::timeout, this,
                [this, client, channel, pollTimer]()
            {
                if (!client || client->state() != QAbstractSocket::ConnectedState)
                { pollTimer->stop(); return; }
                char buf[16384];
                while (true) {
                    qint64 n = libssh2_channel_read(channel, buf, sizeof(buf));
                    if (n == LIBSSH2_ERROR_EAGAIN) break;
                    if (n <= 0) { if (n < 0) client->close(); pollTimer->stop(); return; }
                    client->write(buf, n);
                }
            });
            pollTimer->start();

            QObject::connect(client, &QTcpSocket::disconnected, this,
                [channel, pollTimer, client]()
            { pollTimer->stop(); pollTimer->deleteLater(); libssh2_channel_free(channel); client->deleteLater(); });
        }
    });
#endif // MDSSCOPE_HAS_LIBSSH2

    Tunnel tunnel;
    tunnel.endpoint = endpoint;
    tunnel.host = remoteHost;
    tunnel.remotePort = remotePort;
    tunnel.localPort = localPort;
#ifdef MDSSCOPE_HAS_LIBSSH2
    tunnel.session = session;
#endif
    tunnel.localServer = localServer;
    tunnel.sshFd = sshFd;
    tunnels_.insert(endpoint, tunnel);

    auto* healthTimer = new QTimer(this);
    healthTimer->setInterval(5000);
    QObject::connect(healthTimer, &QTimer::timeout, this, [this, endpoint, healthTimer]()
    {
        auto it = tunnels_.find(endpoint);
        if (it == tunnels_.end()) { healthTimer->stop(); healthTimer->deleteLater(); return; }
        if (!it->localServer) {
            cleanupTunnel(*it);
            tunnels_.erase(it);
            healthTimer->stop(); healthTimer->deleteLater();
            if (tunnels_.isEmpty())
                setState(State::Error, QStringLiteral("SSH tunnel disconnected"));
        }
    });
    healthTimer->start();

    *localEndpoint = QStringLiteral("127.0.0.1:%1").arg(localPort);
    setState(State::Connected, QStringLiteral("SSH tunnel connected"));
    return true;
}

// ... (prepareLayout, prepareUrl, etc. unchanged)
bool SshTunnelManager::prepareLayout(const LayoutConfig& source, LayoutConfig* prepared, QString* error)
{
    if (!prepared) return false;
    *prepared = source;
    reloadSettings();
    if (settings_.mode == SshMode::Disabled) return true;
    if (settings_.host.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("SSH is enabled but no SSH host is configured.");
        setState(State::Error, error ? *error : QString());
        return false;
    }
    if (preparationInProgress_) {
        if (error) *error = QStringLiteral("SSH tunnel preparation is already in progress.");
        return false;
    }
    QScopedValueRollback<bool> guard(preparationInProgress_, true);

    QHash<QString, QString> mapped;
    for (auto& column : prepared->columns) {
        for (PlotSpec& plot : column) {
            for (SignalSpec& signal : plot.signalSpecs) {
                const QString ep = signal.serverIp.trimmed();
                if (ep.isEmpty() || mapped.contains(ep)) {
                    if (mapped.contains(ep)) signal.serverIp = mapped.value(ep);
                    continue;
                }
                QString host; int port = 8000;
                if (!splitEndpoint(ep, &host, &port)) continue;
                auto existing = tunnels_.find(ep);
                if (existing != tunnels_.end() && existing->localServer) {
                    const QString lep = QStringLiteral("127.0.0.1:%1").arg(existing->localPort);
                    mapped.insert(ep, lep); signal.serverIp = lep;
                    continue;
                }
                if (settings_.mode == SshMode::Auto && tcpReachable(host, port, 450))
                { mapped.insert(ep, ep); continue; }
                QString lep;
                if (!ensureTunnel(ep, &lep, error)) return false;
                mapped.insert(ep, lep); signal.serverIp = lep;
            }
        }
    }
    return true;
}

bool SshTunnelManager::prepareUrl(const QString& s, QString* p, QString* e) { return prepareUrlImpl(s, p, e, true); }
bool SshTunnelManager::prepareUrlViaSsh(const QString& s, QString* p, QString* e) { return prepareUrlImpl(s, p, e, false); }

bool SshTunnelManager::prepareUrlImpl(const QString& source, QString* prepared,
                                      QString* error, bool allowDirect)
{
    if (!prepared) return false;
    *prepared = source;
    const QUrl url(source);
    if (!url.isValid() || url.host().isEmpty()) { if (error) *error = QStringLiteral("Invalid URL."); return false; }
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
    { if (error) *error = QStringLiteral("SSH web tunneling supports HTTP and HTTPS URLs only."); return false; }

    reloadSettings();
    if (settings_.mode == SshMode::Disabled && allowDirect) return true;
    if (settings_.mode == SshMode::Disabled) { if (error) *error = QStringLiteral("SSH is disabled."); return false; }
    if (settings_.host.trimmed().isEmpty())
    { if (error) *error = QStringLiteral("SSH is enabled but no SSH host is configured."); return false; }
    if (preparationInProgress_)
    { if (error) *error = QStringLiteral("SSH tunnel preparation is already in progress."); return false; }
    QScopedValueRollback<bool> guard(preparationInProgress_, true);

    const int rp = url.port(scheme == QStringLiteral("https") ? 443 : 80);
    const QString ep = url.host().contains(':')
        ? QStringLiteral("[%1]:%2").arg(url.host()).arg(rp)
        : QStringLiteral("%1:%2").arg(url.host()).arg(rp);

    auto existing = tunnels_.find(ep);
    QString lep;
    if (existing != tunnels_.end() && existing->localServer)
        lep = QStringLiteral("127.0.0.1:%1").arg(existing->localPort);
    else {
        if (allowDirect && settings_.mode == SshMode::Auto && tcpReachable(url.host(), rp, 450)) return true;
        if (!ensureTunnel(ep, &lep, error)) return false;
    }

    QString lh; int lp = 0;
    if (!splitEndpoint(lep, &lh, &lp)) { if (error) *error = QStringLiteral("Invalid local SSH tunnel endpoint."); return false; }
    QUrl t = url; t.setHost(lh); t.setPort(lp);
    *prepared = t.toString(QUrl::FullyEncoded);
    return true;
}

void SshTunnelManager::cleanupTunnel(const Tunnel& t)
{
#ifdef MDSSCOPE_HAS_LIBSSH2
    if (t.session) { libssh2_session_disconnect(t.session, "Bye"); libssh2_session_free(t.session); }
#endif
#if defined(Q_OS_UNIX) || defined(Q_OS_WIN)
    if (t.sshFd >= 0) CLOSE_SOCKET(t.sshFd);
#endif
    if (t.localServer) { t.localServer->close(); t.localServer->deleteLater(); }
}

void SshTunnelManager::disconnectAll()
{
    const auto tunnels = tunnels_;
    tunnels_.clear();
    for (const Tunnel& t : tunnels) cleanupTunnel(t);
    setState(settings_.mode == SshMode::Disabled || settings_.host.isEmpty()
             ? State::Unconfigured : State::Ready);
}

void SshTunnelManager::setState(State state, const QString& detail)
{
    state_ = state;
    lastError_ = state == State::Error ? detail : QString();
    emit stateChanged(state_, detail);
}
