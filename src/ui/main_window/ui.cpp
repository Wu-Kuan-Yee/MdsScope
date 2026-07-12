// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "shared.hpp"
#include "theme.hpp"
#include "ssh_tunnel_manager.hpp"


void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange && aboutButton_) {
        aboutButton_->setIcon(infoIcon());
    }
    if (event->type() == QEvent::PaletteChange && recentEnvironmentButton_) {
        recentEnvironmentButton_->setIcon(recentArrowIcon());
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::schedulePointSync(PlotWidget* source, double x)
{
    pendingPointX_ = x;
    pointSyncSource_ = source;
    const int generation = pointSyncGeneration_;
    if (pointSyncQueued_) {
        return;
    }
    pointSyncQueued_ = true;
    QTimer::singleShot(16, this, [this, generation] {
        if (generation != pointSyncGeneration_) {
            return;
        }
        pointSyncQueued_ = false;
        if (!(pointButton_ && pointButton_->isChecked()) || !std::isfinite(pendingPointX_)) {
            return;
        }
        PlotWidget* source = pointSyncSource_;
        const double x = pendingPointX_;
        QRect visibleArea = gridHost_->rect();
        if (scrollArea_ && scrollArea_->viewport()) {
            visibleArea = QRect(gridHost_->mapFrom(scrollArea_->viewport(), QPoint(0, 0)),
                                scrollArea_->viewport()->size()).intersected(gridHost_->rect());
        }
        for (const auto& col : plotWidgets_) {
            for (PlotWidget* plot : col) {
                if (!plot || !plot->isVisible()) {
                    continue;
                }
                const QRect plotGeometry = QRect(plot->mapTo(gridHost_, QPoint(0, 0)), plot->size());
                if (!visibleArea.intersects(plotGeometry)) {
                    continue;
                }
                const int seriesIndex = plot == source ? plot->activePointSeriesIndex() : 0;
                plot->setSyncedPointX(x, seriesIndex);
            }
        }
    });
}

void MainWindow::buildUi()
{
    setWindowTitle("MdsScope");
    menuBar()->hide();
    QAction* zoomModeAction = new QAction("Zoom / Move", this);
    QAction* pointModeAction = new QAction("Point", this);
    connect(zoomModeAction, &QAction::triggered, this, [this] { setInteractionMode(InteractionMode::Zoom); });
    connect(pointModeAction, &QAction::triggered, this, [this] { setInteractionMode(InteractionMode::Point); });
    zoomModeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
    pointModeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    addAction(zoomModeAction);
    addAction(pointModeAction);

    auto* toolbar = addToolBar("Tools");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setStyleSheet(
        "QToolBar { spacing: 5px; padding: 2px 4px; border: 0px; }"
        "QToolButton { margin: 0px; padding: 3px; min-width: 30px; min-height: 30px; }");
    QAction* openAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Open configure file", this, &MainWindow::openEnvironmentFile);
    openButton_ = qobject_cast<QToolButton*>(toolbar->widgetForAction(openAction));
    if (openButton_) {
        openButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    auto* recentEnvironmentSpace = new QWidget(toolbar);
    recentEnvironmentSpace->setFixedSize(7, 30);
    toolbar->addWidget(recentEnvironmentSpace);
    recentEnvironmentButton_ = new QToolButton(toolbar);
    recentEnvironmentButton_->setObjectName("recentEnvironmentButton");
    recentEnvironmentButton_->setIcon(recentArrowIcon());
    recentEnvironmentButton_->setIconSize(QSize(12, 30));
    recentEnvironmentButton_->setToolTip("Recent configure files");
    recentEnvironmentButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    recentEnvironmentButton_->setAutoRaise(true);
    recentEnvironmentButton_->setFixedSize(12, 30);
    recentEnvironmentButton_->setStyleSheet(
        "QToolButton#recentEnvironmentButton {"
        "  background: transparent;"
        "  color: palette(buttonText);"
        "  border: 0px;"
        "  padding: 0px;"
        "  margin: 0px;"
        "  min-width: 12px;"
        "  max-width: 12px;"
        "  min-height: 30px;"
        "  max-height: 30px;"
        "}");
    auto positionRecentEnvironmentButton = [toolbar, this] {
        if (openButton_ && recentEnvironmentButton_) {
            recentEnvironmentButton_->move(openButton_->mapTo(toolbar, QPoint(openButton_->width() - 4, 0)));
            recentEnvironmentButton_->raise();
        }
    };
    if (openButton_) {
        positionRecentEnvironmentButton();
        QTimer::singleShot(0, this, positionRecentEnvironmentButton);
        recentEnvironmentButton_->show();
    }
    connect(recentEnvironmentButton_, &QToolButton::clicked, this, &MainWindow::showRecentEnvironmentMenu);
    QWidget* recentMenuParent = openButton_ ? static_cast<QWidget*>(openButton_) : static_cast<QWidget*>(toolbar);
    recentEnvironmentMenu_ = new QMenu(recentMenuParent);
    connect(recentEnvironmentMenu_, &QMenu::aboutToShow, this, &MainWindow::refreshRecentEnvironmentMenu);
    refreshRecentEnvironmentMenu();
    QAction* saveAction = toolbar->addAction(saveIcon(), "Save", this, &MainWindow::saveCurrentEnvironment);
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(saveAction);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "Export data", this, &MainWindow::openExportDataDialog);
    toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Refresh", this, &MainWindow::refreshData);
    loginAction_ = toolbar->addAction(loginIcon(false), "Login", this, &MainWindow::openLoginDialog);
    updateLoginActionIcon();
    sshAction_ = toolbar->addAction(sshIcon(0), "SSH remote access", this, &MainWindow::openSshDialog);
    connect(sshTunnelManager_, &SshTunnelManager::stateChanged, this, [this] {
        cachedApiSourceUrl_.clear();
        cachedPreparedApiUrl_.clear();
        updateSshActionIcon();
    });
    updateSshActionIcon();
    toolbar->addAction(gearIcon(), "Layout setup", this, &MainWindow::openLayoutSetupDialog);
    toolbar->addAction(fontIcon(), "Customize fonts", this, &MainWindow::openCustomizeDialog);

    gridHost_ = new QWidget(this);
    gridHost_->setFocusPolicy(Qt::StrongFocus);
    gridHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    gridLayout_ = new QGridLayout(gridHost_);
    gridLayout_->setContentsMargins(0, 0, 0, 0);
    gridLayout_->setSpacing(0);
    setCentralWidget(gridHost_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet("color: palette(highlight);");

    toolbar->addSeparator();
    auto* topControls = new QWidget(toolbar);
    topControls->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* topLayout = new QHBoxLayout(topControls);
    topLayout->setContentsMargins(2, 0, 2, 0);
    topLayout->setSpacing(3);
    topControls->setStyleSheet(
        "QPushButton {"
        "  padding: 1px 8px;"
        "  min-height: 18px;"
        "}"
        "QLineEdit, QComboBox {"
        "  min-height: 18px;"
        "  padding: 0px 2px;"
        "}"
        "QLabel { margin-left: 2px; margin-right: 2px; }"
        "QToolButton#aboutButton {"
        "  border: 1px solid transparent;"
        "  border-radius: 15px;"
        "  background: transparent;"
        "  padding: 0px;"
        "  margin-left: 8px;"
        "}");
    topLayout->addWidget(new QLabel("Rate", topControls));
    dataModeCombo_ = new QComboBox(topControls);
    dataModeCombo_->addItem("Thin", static_cast<int>(DataReadMode::Thin));
    dataModeCombo_->addItem("Medium", static_cast<int>(DataReadMode::Medium));
    dataModeCombo_->addItem("Full", static_cast<int>(DataReadMode::Full));
    dataModeCombo_->setCurrentIndex(0);
    dataModeCombo_->setFixedWidth(90);
    topLayout->addWidget(dataModeCombo_);

    topInfoLabel_ = new QLabel("Shot: --", topControls);
    ipInfoLabel_ = new QLabel("Ip: --", topControls);
    pulseInfoLabel_ = new QLabel("Pulse: --", topControls);
    itInfoLabel_ = new QLabel("It: --", topControls);
    timeInfoLabel_ = new QLabel("Time: --", topControls);
    for (QLabel* label : {topInfoLabel_, ipInfoLabel_, pulseInfoLabel_, itInfoLabel_, timeInfoLabel_}) {
        label->setStyleSheet("color: palette(highlight);");
        topLayout->addWidget(label);
    }
    topLayout->addStretch(1);
    topLayout->addWidget(new ThemeModeButton(topControls));
    aboutButton_ = new QToolButton(topControls);
    aboutButton_->setObjectName("aboutButton");
    aboutButton_->setIcon(infoIcon());
    aboutButton_->setIconSize(QSize(28, 28));
    aboutButton_->setFixedSize(34, 34);
    aboutButton_->setToolTip("About MdsScope");
    topLayout->addWidget(aboutButton_);
    toolbar->addWidget(topControls);

    auto* bottom = new QWidget(this);
    auto* bottomLayout = new QHBoxLayout(bottom);
    bottomLayout->setContentsMargins(4, 1, 4, 1);
    bottomLayout->setSpacing(5);
    bottom->setMaximumHeight(34);
    bottom->setStyleSheet(
        "QPushButton {"
        "  padding: 1px 8px;"
        "  min-height: 18px;"
        "}"
        "QLineEdit, QComboBox {"
        "  min-height: 18px;"
        "  padding: 0px 2px;"
        "}"
        "QToolButton { margin: 0px; padding: 1px; min-width: 30px; min-height: 28px; }");
    zoomButton_ = new QToolButton(bottom);
    pointButton_ = new QToolButton(bottom);
    for (QToolButton* button : {zoomButton_, pointButton_}) {
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setIconSize(QSize(24, 24));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    zoomButton_->setToolTip("Zoom / Move (Ctrl+Z): drag to zoom, middle-drag or Shift-drag to move");
    pointButton_->setToolTip("Point (Ctrl+P): click to activate, Esc to exit");
    pointButton_->setChecked(true);
    bottomLayout->addWidget(zoomButton_);
    bottomLayout->addWidget(pointButton_);
    bottomLayout->addWidget(new QLabel("Shot", bottom));
    shotCombo_ = new QComboBox(bottom);
    shotCombo_->setEditable(true);
    shotCombo_->setInsertPolicy(QComboBox::NoInsert);
    shotCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    shotCombo_->setMaxVisibleItems(10);
    shotCombo_->view()->setTextElideMode(Qt::ElideMiddle);
    shotCombo_->view()->setMinimumWidth(260);
    shotCombo_->view()->setMaximumWidth(520);
    shotEdit_ = shotCombo_->lineEdit();
    refreshShotHistory();
    auto resizeShotEdit = [this] {
        if (!shotEdit_ || !shotCombo_) {
            return;
        }
        const QFontMetrics fm(shotEdit_->font());
        const int textWidth = fm.horizontalAdvance(shotEdit_->text().trimmed().isEmpty()
                                                       ? QStringLiteral("143850-143858")
                                                       : shotEdit_->text().trimmed());
        shotCombo_->setFixedWidth(std::clamp(textWidth + 52, 120, 620));
    };
    resizeShotEdit();
    bottomLayout->addWidget(shotCombo_);
    auto* apply = new QPushButton("Apply", bottom);
    auto* prev = new QPushButton("Prev", bottom);
    auto* next = new QPushButton("Next", bottom);
    auto* stop = new QPushButton("Stop", bottom);
    auto* latest = new QPushButton("Latest", bottom);
    stopButton_ = stop;
    // Fix the width to the wider "Continue" label (plus padding) so toggling
    // the text never clips it or shifts the buttons to its right.
    stop->setText("Continue");
    stop->setFixedWidth(stop->sizeHint().width() + 16);
    stop->setText("Stop");
    bottomLayout->addWidget(apply);
    bottomLayout->addWidget(prev);
    bottomLayout->addWidget(next);
    bottomLayout->addWidget(stop);
    bottomLayout->addWidget(latest);
    bottomLayout->addWidget(statusLabel_, 1);
    statusBar()->addWidget(bottom, 1);
    setInteractionMode(InteractionMode::Point);

    connect(zoomButton_, &QToolButton::clicked, this, [this] { setInteractionMode(InteractionMode::Zoom); });
    connect(pointButton_, &QToolButton::clicked, this, [this] { setInteractionMode(InteractionMode::Point); });
    connect(apply, &QPushButton::clicked, this, &MainWindow::applyShot);
    connect(prev, &QPushButton::clicked, this, [this] { stepShot(-1); });
    connect(next, &QPushButton::clicked, this, [this] { stepShot(1); });
    connect(latest, &QPushButton::clicked, this, &MainWindow::latestShot);
    connect(stop, &QPushButton::clicked, this, &MainWindow::onStopOrContinue);
    connect(shotEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyShot);
    connect(shotEdit_, &QLineEdit::textChanged, this, [resizeShotEdit] { resizeShotEdit(); });
    connect(shotCombo_, &QComboBox::activated, this, [this](int index) {
        const QString shot = shotCombo_->itemData(index).toString();
        if (!shot.isEmpty()) {
            shotCombo_->setEditText(shot);
        }
        applyShot();
    });
    connect(dataModeCombo_, &QComboBox::currentIndexChanged, this, [this] { refreshData(); });
    connect(aboutButton_, &QToolButton::clicked, this, &MainWindow::openAboutDialog);
}

void MainWindow::showPanelContextMenu(PlotWidget* plot, int column, int row, const QPoint& pos)
{
    if (!plot) {
        return;
    }
    selectPlot(column, row);

    QMenu menu(this);
    QAction* maxAction = menu.addAction("Max");
    QAction* showAllAction = menu.addAction("Show All Panels");
    showAllAction->setEnabled(singlePanelMaximized_);
    menu.addSeparator();
    QAction* panelSetupAction = menu.addAction("Panel Setup");
    QAction* dataSourceAction = menu.addAction("Data Source Setup");
    QAction* exportDataAction = menu.addAction("Export Data");
    menu.addSeparator();
    QAction* resetCurrentAction = menu.addAction("Reset Current Scale");
    QAction* resetAllAction = menu.addAction("Reset All Panels");
    QAction* sameXAction = menu.addAction("All Same X Scale");
    QAction* sameYAction = menu.addAction("All Same Y Scale");

    const QAction* chosen = menu.exec(plot->mapToGlobal(pos));
    if (!chosen) {
        return;
    }
    if (chosen == maxAction) {
        maximizeCurrentPanel();
    } else if (chosen == showAllAction) {
        showAllPanels();
    } else if (chosen == panelSetupAction) {
        panelSetupForCurrentPanel();
    } else if (chosen == dataSourceAction) {
        dataSourceSetupForCurrentPanel();
    } else if (chosen == exportDataAction) {
        exportCurrentPanelData();
    } else if (chosen == resetCurrentAction) {
        resetCurrentScale();
    } else if (chosen == resetAllAction) {
        resetScales();
    } else if (chosen == sameXAction) {
        applyScaleToAll();
    } else if (chosen == sameYAction) {
        applyYScaleToAll();
    }
}

void MainWindow::openCustomizeDialog()
{
    FontSettings& fonts = fontSettings();
    QDialog dialog(this);
    dialog.setWindowTitle("Customize Fonts");
    if (QApplication::palette().color(QPalette::Window).lightness() >= 128) {
        dialog.setStyleSheet(
            "QDialog { background: #f6f6f6; color: #111827; }"
            "QLabel { background: transparent; color: #111827; }"
            "QSpinBox, QFontComboBox {"
            "  background: #ffffff;"
            "  color: #111827;"
            "  border: 1px solid #cbd5e1;"
            "  border-radius: 3px;"
            "  padding: 3px 6px;"
            "  selection-background-color: #2563eb;"
            "  selection-color: #ffffff;"
            "}"
            "QSpinBox:focus, QFontComboBox:focus { border-color: #2563eb; }"
            "QSpinBox::up-button, QSpinBox::down-button { background: transparent; border: none; width: 16px; }");
    }
    auto* layout = new QFormLayout(&dialog);
    auto* family = new QFontComboBox(&dialog);
    family->setCurrentFont(QFont(fonts.family));
    auto* legendSize = new QSpinBox(&dialog);
    auto* axisSize = new QSpinBox(&dialog);
    auto* unitSize = new QSpinBox(&dialog);
    auto* uiSize = new QSpinBox(&dialog);
    for (QSpinBox* box : {legendSize, axisSize, unitSize, uiSize}) {
        box->setRange(6, 28);
        box->setSingleStep(1);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    }
    legendSize->setValue(fonts.legendSize);
    axisSize->setValue(fonts.axisSize);
    unitSize->setValue(fonts.unitSize);
    uiSize->setValue(fonts.uiSize);
    layout->addRow("Font", family);
    layout->addRow("Legend size", legendSize);
    layout->addRow("Axis size", axisSize);
    layout->addRow("Unit size", unitSize);
    layout->addRow("UI size", uiSize);
    auto* buttons = new QHBoxLayout();
    auto* ok = new QPushButton("OK", &dialog);
    auto* cancel = new QPushButton("Cancel", &dialog);
    buttons->addStretch(1);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    layout->addRow(buttons);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    fonts.family = family->currentFont().family();
    fonts.legendSize = legendSize->value();
    fonts.axisSize = axisSize->value();
    fonts.unitSize = unitSize->value();
    fonts.uiSize = uiSize->value();
    saveFontSettings(rootPath_);
    applyUiFont();
    refreshPlotFonts();
}

void MainWindow::applyUiFont()
{
    const FontSettings& fonts = fontSettings();
    QFont uiFont(fonts.family, fonts.uiSize);
    if (QApplication::font() != uiFont) {
        QApplication::setFont(uiFont);
    }
    if (font() != uiFont) {
        setFont(uiFont);
    }
    for (QWidget* widget : findChildren<QWidget*>()) {
        if (!qobject_cast<PlotWidget*>(widget) && widget->font() != uiFont) {
            widget->setFont(uiFont);
        }
    }
    if (statusLabel_ && statusLabel_->font() != uiFont) {
        statusLabel_->setFont(uiFont);
    }
    for (QLabel* label : {topInfoLabel_, ipInfoLabel_, pulseInfoLabel_, itInfoLabel_, timeInfoLabel_}) {
        if (label && label->font() != uiFont) {
            label->setFont(uiFont);
        }
    }
}

void MainWindow::refreshPlotFonts()
{
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot) {
                plot->refreshStyle();
            }
        }
    }
}

void MainWindow::setInteractionMode(InteractionMode mode)
{
    if (currentInteractionMode_ == mode
        && zoomButton_ && zoomButton_->isChecked() == (mode == InteractionMode::Zoom)
        && pointButton_ && pointButton_->isChecked() == (mode == InteractionMode::Point)) {
        return;
    }
    currentInteractionMode_ = mode;
    if (zoomButton_) {
        zoomButton_->setChecked(mode == InteractionMode::Zoom);
        pointButton_->setChecked(mode == InteractionMode::Point);
        zoomButton_->setIcon(modeIcon(InteractionMode::Zoom, mode == InteractionMode::Zoom));
        pointButton_->setIcon(modeIcon(InteractionMode::Point, mode == InteractionMode::Point));
    }
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            plot->setInteractionMode(mode);
        }
    }
}

void MainWindow::updateTopInfoLabels()
{
    // The top strip is a live summary of the API's latest shot.  The editable
    // shot field and plot panels may intentionally remain on an older shot.
    QString shot = latestShot_.trimmed();
    if (shot.isEmpty()) {
        shot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    }
    const PlotSpec* plot = nullptr;
    if (selectedColumn_ >= 0 && selectedRow_ >= 0
        && selectedColumn_ < config_.columns.size()
        && selectedRow_ < config_.columns[selectedColumn_].size()) {
        plot = &config_.columns[selectedColumn_][selectedRow_];
    }
    if (!plot) {
        for (const auto& col : config_.columns) {
            for (const PlotSpec& candidate : col) {
                if (!candidate.signalSpecs.isEmpty()) {
                    plot = &candidate;
                    break;
                }
            }
            if (plot) {
                break;
            }
        }
    }
    if (plot) {
        if (shot.isEmpty()) {
            shot = plot->shot.trimmed();
        }
    }

    if (shot.isEmpty()) {
        topSummaryShot_.clear();
        topSummaryIp_.clear();
        topSummaryPulse_.clear();
        topSummaryIt_.clear();
        topSummaryTime_.clear();
        pendingTopSummaryShot_.clear();
    } else if (shot != topSummaryShot_) {
        topSummaryIp_.clear();
        topSummaryPulse_.clear();
        topSummaryIt_.clear();
        topSummaryTime_.clear();
        scheduleTopInfoUpdate(shot);
    }

    setLabelTextIfChanged(topInfoLabel_, "Shot: " + (shot.isEmpty() ? QStringLiteral("--") : shot));
    const bool loading = !shot.isEmpty() && shot != topSummaryShot_ && pendingTopSummaryShot_ == shot;
    const QString emptyText = loading ? QStringLiteral("...") : QStringLiteral("--");
    setLabelTextIfChanged(ipInfoLabel_, "Ip: " + (topSummaryIp_.isEmpty() ? emptyText : topSummaryIp_ + " KA"));
    setLabelTextIfChanged(pulseInfoLabel_, "Pulse: " + (topSummaryPulse_.isEmpty() ? emptyText : topSummaryPulse_ + " s"));
    setLabelTextIfChanged(itInfoLabel_, "It: " + (topSummaryIt_.isEmpty() ? emptyText : topSummaryIt_ + " A"));
    setLabelTextIfChanged(timeInfoLabel_, "Time: " + (topSummaryTime_.isEmpty() ? emptyText : topSummaryTime_));
}

void MainWindow::scheduleTopInfoUpdate(const QString& shot)
{
    const QString trimmedShot = shot.trimmed();
    if (trimmedShot.isEmpty() || pendingTopSummaryShot_ == trimmedShot) {
        return;
    }

    pendingTopSummaryShot_ = trimmedShot;
    const int generation = ++topSummaryGeneration_;
    QString apiUrl;
    if (!prepareSshUrl(readApiUrl(rootPath_), &apiUrl)) {
        pendingTopSummaryShot_.clear();
        return;
    }
    QThreadPool::globalInstance()->start([this, trimmedShot, generation, apiUrl] {
        QString ip;
        QString pulse;
        QString it;
        QString shotTime;
        const bool ok = loadShotSummaryFromApi(trimmedShot, &ip, &pulse, &it, &shotTime, apiUrl);
        QMetaObject::invokeMethod(this, [this, trimmedShot, generation, ok, ip, pulse, it, shotTime] {
            if (generation != topSummaryGeneration_ || pendingTopSummaryShot_ != trimmedShot) {
                return;
            }
            pendingTopSummaryShot_.clear();
            topSummaryShot_ = trimmedShot;
            topSummaryIp_ = ok ? ip : QString();
            topSummaryPulse_ = ok ? pulse : QString();
            topSummaryIt_ = ok ? it : QString();
            topSummaryTime_ = ok ? shotTime : QString();
            if (!ok) {
                cachedApiSourceUrl_.clear();
                cachedPreparedApiUrl_.clear();
            }
            updateTopInfoLabels();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::setStatus(const QString& text)
{
    setLabelTextIfChanged(statusLabel_, text);
}

PlotWidget* MainWindow::currentPlotWidget() const
{
    if (selectedColumn_ < 0 || selectedRow_ < 0 ||
        selectedColumn_ >= plotWidgets_.size() || selectedRow_ >= plotWidgets_[selectedColumn_].size()) {
        return nullptr;
    }
    return plotWidgets_[selectedColumn_][selectedRow_];
}
