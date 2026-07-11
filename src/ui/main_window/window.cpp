// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "mds_client.hpp"
#include "point_overlay.hpp"
#include "ssh_tunnel_manager.hpp"

MainWindow::MainWindow(QString rootPath, QWidget* parent)
    : QMainWindow(parent), rootPath_(std::move(rootPath))
{
    setWindowIcon(appIcon());
    sshTunnelManager_ = new SshTunnelManager(this);
    environmentPath_ = appEnvironmentDir(rootPath_);
    ensureSourceIndexCache(rootPath_);
    exportBasePath_ = QSettings(uiSettingsPath(rootPath_), QSettings::IniFormat)
                          .value("export/base_dir", defaultExportBaseDir())
                          .toString();
    loadFontSettings(rootPath_);
    buildUi();
    applyUiFont();
    connect(&panelWatcher_, &QFutureWatcher<QVector<LoadedSignal>>::finished, this, [this] {
        if (!activePanelRefreshKey_.isEmpty()) {
            applyPanelLoadedSignals(panelWatcher_.result());
            activePanelRefreshKey_.clear();
        }
        if (pendingPanelRefresh_) {
            const int column = pendingPanelColumn_;
            const int row = pendingPanelRow_;
            const int signal = pendingPanelSignal_;
            pendingPanelRefresh_ = false;
            pendingPanelColumn_ = -1;
            pendingPanelRow_ = -1;
            pendingPanelSignal_ = -1;
            queuedPanelRefreshKey_.clear();
            refreshOne(column, row, signal);
        }
        // A prewarm may have finished while this panel fetch was in flight; the
        // idle guard keeps pendingPrewarmRefresh_ set until the panel drains, so
        // launch the deferred initial full refresh now if nothing else is running.
        maybeStartDeferredRefresh();
    });
    connect(&warmWatcher_, &QFutureWatcher<void>::finished, this, [this] {
        const bool idle = canStartDeferredRefresh();
        if (pendingPrewarmRefresh_ && idle) {
            pendingPrewarmRefresh_ = false;
            refreshData();
            return;
        }
        // If a fetch is still in flight, keep pendingPrewarmRefresh_ set so the
        // fetch-completion handler triggers the deferred initial refresh once it
        // drains, rather than dropping it here.
        if (idle) {
            setStatus("MDS connections ready");
        }
    });
    QTimer::singleShot(16, this, [this] {
        loadDefaultEnvironment(true);
    });
}
