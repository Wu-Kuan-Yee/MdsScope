// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "base_dialog.hpp"
#include <QScrollArea>
#include <QScroller>
#include <QScrollerProperties>
#include <QTimer>
#include "shared.hpp"
#include "theme.hpp"
#include "ssh_tunnel_manager.hpp"
#include "mobile_menu.hpp"
#include "dialog_overlay.hpp"

#include <QScreen>
#include <QScrollBar>
#include <QGuiApplication>
#include <QInputMethod>

class PopupPositionFilter : public QObject {
public:
    PopupPositionFilter(QComboBox* combo) : QObject(combo), combo_(combo) {}
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Show) {
            if (QWidget* popup = qobject_cast<QWidget*>(obj)) {
                QPoint globalPos = combo_->mapToGlobal(QPoint(0, 0));
                QRect screenRect;
                if (auto* screen = QGuiApplication::screenAt(globalPos)) {
                    screenRect = screen->availableGeometry();
                } else if (!QGuiApplication::screens().isEmpty()) {
                    screenRect = QGuiApplication::screens().first()->availableGeometry();
                }
                
                if (!screenRect.isEmpty()) {
                    int bottomSpace = screenRect.bottom() - (globalPos.y() + combo_->height());
                    if (popup->height() > bottomSpace) {
                        popup->move(globalPos.x(), globalPos.y() - popup->height());
                    }
                }
                
                if (combo_->isEditable()) {
                    if (QLineEdit* le = combo_->lineEdit()) {
                        le->clearFocus();
                    }
                    if (QInputMethod* im = QGuiApplication::inputMethod()) {
                        im->hide();
                    }
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }
private:
    QComboBox* combo_;
};

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
    toolbar->toggleViewAction()->setVisible(false);
    toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setStyleSheet(
        "QToolBar { spacing: 5px; padding: 2px 4px; border: 0px; }"
        "QToolButton { margin: 0px; padding: 3px; min-width: 30px; min-height: 30px; }"
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        "QToolBar::extension-button { min-width: 48px; min-height: 48px; }"
#endif
    );
    QAction* openAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Open configure file");
    connect(openAction, &QAction::triggered, this, &MainWindow::openEnvironmentFile, Qt::QueuedConnection);
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
    QAction* saveAction = toolbar->addAction(saveIcon(), "Save");
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveCurrentEnvironment, Qt::QueuedConnection);
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(saveAction);
    QAction* exportAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "Export data");
    connect(exportAction, &QAction::triggered, this, &MainWindow::openExportDataDialog, Qt::QueuedConnection);
    toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Refresh", this, &MainWindow::refreshData);
    loginAction_ = toolbar->addAction(loginIcon(false), "Login");
    connect(loginAction_, &QAction::triggered, this, &MainWindow::openLoginDialog, Qt::QueuedConnection);
    updateLoginActionIcon();
    sshAction_ = toolbar->addAction(sshIcon(0), "SSH remote access");
    connect(sshAction_, &QAction::triggered, this, &MainWindow::openSshDialog, Qt::QueuedConnection);
    connect(sshTunnelManager_, &SshTunnelManager::stateChanged, this, [this] {
        cachedApiSourceUrl_.clear();
        cachedPreparedApiUrl_.clear();
        updateSshActionIcon();
    });
    updateSshActionIcon();
    QAction* internalWebAction = toolbar->addAction(browserIcon(), "Internal web pages");
    if (auto* internalWebButton = qobject_cast<QToolButton*>(toolbar->widgetForAction(internalWebAction))) {
        internalWebButton->setObjectName(QStringLiteral("internalWebButton"));
        internalWebButton->setStyleSheet(
            QStringLiteral("QToolButton#internalWebButton::menu-indicator { image: none; width: 0px; }"));
        internalWebMenu_ = new QMenu(internalWebButton);
        // connect(internalWebMenu_, &QMenu::aboutToShow, this, &MainWindow::refreshInternalWebMenu);
        internalWebButton->setMenu(internalWebMenu_);
        internalWebButton->setPopupMode(QToolButton::InstantPopup);
        refreshInternalWebMenu();
    }
    QAction* layoutSetupAction = toolbar->addAction(gearIcon(), "Layout setup");
    connect(layoutSetupAction, &QAction::triggered, this, &MainWindow::openLayoutSetupDialog, Qt::QueuedConnection);
    QAction* customizeFontsAction = toolbar->addAction(fontIcon(), "Customize fonts");
    connect(customizeFontsAction, &QAction::triggered, this, &MainWindow::openCustomizeDialog, Qt::QueuedConnection);

    gridHost_ = new QWidget(this);
    gridHost_->setFocusPolicy(Qt::StrongFocus);
    gridHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    gridLayout_ = new QGridLayout(gridHost_);
    gridLayout_->setContentsMargins(0, 0, 0, 0);
    gridLayout_->setSpacing(0);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setWidget(gridHost_);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // Ensure the scroll area has a transparent background
    scrollArea_->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    
    // CRITICAL ANDROID FIX FOR TEARING:
    // When the scroll area is transparent, Qt disables bitBlt for scrolling.
    // This fixes the 'static ghost cover' bug in Max Mode on Android!
    // HOWEVER, it causes QScrollArea's move() to generate multiple dirty rects for each PlotWidget.
    // Android's SurfaceFlinger composite swaps buffers between these rects, causing severe tearing.
    // We MUST force a single FULL-WINDOW update whenever the scroll area moves!
    connect(scrollArea_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (window()) window()->update();
    });
    
    // Enable native kinetic scrolling with one finger
    QScroller* scroller = QScroller::scroller(scrollArea_->viewport());
    QScrollerProperties props = scroller->scrollerProperties();
    // CRITICAL: Disable overscroll (bounce effect) on Android! 
    // Overscrolling exposes the viewport background outside the widget bounds.
    // Due to Android Qt QPA bitBlt bugs, these exposed regions fail to paint correctly
    // and leave severe ghost artifacts of the widget's previous pixels.
    props.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
    props.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, QScrollerProperties::OvershootAlwaysOff);
    scroller->setScrollerProperties(props);
    QScroller::grabGesture(scrollArea_->viewport(), QScroller::TouchGesture);
    setCentralWidget(scrollArea_);
#else
    setCentralWidget(gridHost_);
#endif

    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet("color: palette(highlight);");
    statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    toolbar->addSeparator();

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    addToolBarBreak(Qt::TopToolBarArea);
    auto* topControls1 = addToolBar("Info 1");
    topControls1->setMovable(false);
    topControls1->setContextMenuPolicy(Qt::PreventContextMenu);
    topControls1->setStyleSheet(
        "QToolBar { background: palette(window); border: none; padding: 0px; margin: 0px; }"
        "QPushButton { padding: 1px 8px; min-height: 18px; }"
        "QLabel { margin-left: 2px; margin-right: 2px; }"
    );
    
    addToolBarBreak(Qt::TopToolBarArea);
    auto* topControls2 = addToolBar("Info 2");
    topControls2->setMovable(false);
    topControls2->setContextMenuPolicy(Qt::PreventContextMenu);
    topControls2->setStyleSheet(
        "QToolBar { background: palette(window); border: none; padding: 0px; margin: 0px; }"
        "QPushButton { padding: 1px 8px; min-height: 18px; }"
        "QLabel { margin-left: 2px; margin-right: 2px; }"
    );
    
    addToolBarBreak(Qt::TopToolBarArea);
    auto* topControls3 = addToolBar("Info 3");
    topControls3->setMovable(false);
    topControls3->setContextMenuPolicy(Qt::PreventContextMenu);
    topControls3->setStyleSheet(
        "QToolBar { background: palette(window); border: none; padding: 0px; margin: 0px; }"
        "QPushButton { padding: 1px 8px; min-height: 18px; }"
        "QLabel { margin-left: 2px; margin-right: 2px; }"
    );
    
    topControls1->addWidget(new QLabel("Rate", topControls1));

    dataModeCombo_ = new QComboBox(topControls1);
    dataModeCombo_->addItem("Thin", static_cast<int>(DataReadMode::Thin));
    dataModeCombo_->addItem("Medium", static_cast<int>(DataReadMode::Medium));
    dataModeCombo_->addItem("Full", static_cast<int>(DataReadMode::Full));
    dataModeCombo_->setCurrentIndex(0);
    if (auto* popup = dataModeCombo_->view()->window()) {
        popup->installEventFilter(new PopupPositionFilter(dataModeCombo_));
    }
    topControls1->addWidget(dataModeCombo_);

    topInfoLabel_ = new QLabel("Shot: --", topControls1);
    ipInfoLabel_ = new QLabel("Ip: --", topControls1);
    
    pulseInfoLabel_ = new QLabel("Pulse: --", topControls2);
    itInfoLabel_ = new QLabel("It: --", topControls2);
    timeInfoLabel_ = new QLabel("Time: --", topControls3);
    
    for (QLabel* label : {topInfoLabel_, ipInfoLabel_}) {
        label->setStyleSheet("color: palette(highlight);");
        topControls1->addWidget(label);
    }
    for (QLabel* label : {pulseInfoLabel_, itInfoLabel_}) {
        label->setStyleSheet("color: palette(highlight);");
        topControls2->addWidget(label);
    }
    timeInfoLabel_->setStyleSheet("color: palette(highlight);");
    topControls3->addWidget(timeInfoLabel_);
    
    auto* spacer1 = new QWidget(topControls1);
    spacer1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    topControls1->addWidget(spacer1);
    
    auto* spacer2 = new QWidget(topControls2);
    spacer2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    topControls2->addWidget(spacer2);
    
    auto* spacer3 = new QWidget(topControls3);
    spacer3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    topControls3->addWidget(spacer3);
    
    topControls2->addWidget(new ThemeModeButton(topControls2));

    aboutButton_ = new QToolButton(topControls2);
    aboutButton_->setObjectName("aboutButton");
    aboutButton_->setIcon(infoIcon());
    aboutButton_->setIconSize(QSize(28, 28));
    aboutButton_->setFixedSize(34, 34);
    aboutButton_->setToolTip("About MdsScope");
    aboutButton_->setStyleSheet(
        "QToolButton#aboutButton {"
        "  border: 1px solid transparent;"
        "  border-radius: 15px;"
        "  background: transparent;"
        "  padding: 0px;"
        "  margin-left: 8px;"
        "}");
    topControls2->addWidget(aboutButton_);

#else
    auto* topControls = new QWidget(toolbar);
    topControls->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* topLayout = new QHBoxLayout(topControls);
    topLayout->setContentsMargins(2, 0, 2, 0);
    topLayout->setSpacing(3);
    topControls->setStyleSheet(
        "QPushButton { padding: 1px 8px; min-height: 18px; }"
        "QLabel { margin-left: 2px; margin-right: 2px; }"
    );

    topLayout->addWidget(new QLabel("Rate", topControls));

    dataModeCombo_ = new QComboBox(topControls);
    dataModeCombo_->addItem("Thin", static_cast<int>(DataReadMode::Thin));
    dataModeCombo_->addItem("Medium", static_cast<int>(DataReadMode::Medium));
    dataModeCombo_->addItem("Full", static_cast<int>(DataReadMode::Full));
    dataModeCombo_->setCurrentIndex(0);
    if (auto* popup = dataModeCombo_->view()->window()) {
        popup->installEventFilter(new PopupPositionFilter(dataModeCombo_));
    }
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
    aboutButton_->setStyleSheet(
        "QToolButton#aboutButton {"
        "  border: 1px solid transparent;"
        "  border-radius: 15px;"
        "  background: transparent;"
        "  padding: 0px;"
        "  margin-left: 8px;"
        "}");
    topLayout->addWidget(aboutButton_);

    toolbar->addWidget(topControls);
#endif

    auto* bottomToolBar = addToolBar("Controls");
    addToolBar(Qt::BottomToolBarArea, bottomToolBar);
    bottomToolBar->setMovable(false);
    bottomToolBar->setContextMenuPolicy(Qt::PreventContextMenu);
    bottomToolBar->setStyleSheet(
        "QPushButton { padding: 1px 8px; min-height: 18px; }"
        "QToolButton { margin: 0px; padding: 1px; min-width: 30px; min-height: 28px; }"
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        "QToolBar::extension-button { min-width: 48px; min-height: 48px; }"
#endif
    );
    zoomButton_ = new QToolButton(bottomToolBar);
    pointButton_ = new QToolButton(bottomToolBar);
    for (QToolButton* button : {zoomButton_, pointButton_}) {
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setIconSize(QSize(24, 24));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    zoomButton_->setToolTip("Zoom / Move (Ctrl+Z): drag to zoom, middle-drag or Shift-drag to move");
    pointButton_->setToolTip("Point (Ctrl+P): click to activate, Esc to exit");
    pointButton_->setChecked(true);
    bottomToolBar->addWidget(zoomButton_);
    bottomToolBar->addWidget(pointButton_);
    bottomToolBar->addWidget(new QLabel("Shot", bottomToolBar));
    shotCombo_ = new QComboBox(bottomToolBar);
    shotCombo_->setEditable(true);
    shotCombo_->setInsertPolicy(QComboBox::NoInsert);
    shotCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    shotCombo_->setMaxVisibleItems(10);
    shotCombo_->view()->setTextElideMode(Qt::ElideMiddle);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (auto* comp = shotCombo_->completer()) {
        shotCombo_->setCompleter(nullptr);
    }
    shotCombo_->view()->setMinimumWidth(200);
    shotCombo_->setMinimumWidth(80);
    shotCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    shotCombo_->setStyleSheet("QComboBox::drop-down { width: 44px; border-left: 1px solid palette(mid); } QComboBox::down-arrow { image: url(\":/images/down_arrow.png\"); }");
#else
    shotCombo_->view()->setMinimumWidth(260);
    shotCombo_->view()->setMaximumWidth(520);
#endif
    if (auto* popup = shotCombo_->view()->window()) {
        popup->installEventFilter(new PopupPositionFilter(shotCombo_));
    }
    shotEdit_ = shotCombo_->lineEdit();
    refreshShotHistory();
    auto resizeShotEdit = [this] {
        if (!shotEdit_ || !shotCombo_) {
            return;
        }
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
        const QFontMetrics fm(shotEdit_->font());
        const int textWidth = fm.horizontalAdvance(shotEdit_->text().trimmed().isEmpty()
                                                       ? QStringLiteral("143850-143858")
                                                       : shotEdit_->text().trimmed());
        shotCombo_->setFixedWidth(std::clamp(textWidth + 52, 120, 620));
#endif
    };
    resizeShotEdit();
    bottomToolBar->addWidget(shotCombo_);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    addToolBarBreak(Qt::BottomToolBarArea);
    auto* bottomToolBar2 = addToolBar("Controls Actions");
    addToolBar(Qt::BottomToolBarArea, bottomToolBar2);
    bottomToolBar2->setMovable(false);
    bottomToolBar2->setContextMenuPolicy(Qt::PreventContextMenu);
    bottomToolBar2->setStyleSheet(
        "QPushButton { padding: 1px 8px; min-height: 18px; }"
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        "QToolBar::extension-button { min-width: 48px; min-height: 48px; }"
#endif
    );
#else
    auto* bottomToolBar2 = bottomToolBar;
#endif

    auto* apply = new QPushButton("Apply", bottomToolBar2);
    auto* prev = new QPushButton("Prev", bottomToolBar2);
    auto* next = new QPushButton("Next", bottomToolBar2);
    auto* stop = new QPushButton("Stop", bottomToolBar2);
    auto* latest = new QPushButton("Latest", bottomToolBar2);
    stopButton_ = stop;
    stop->setText("Continue");
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    stop->setFixedWidth(stop->sizeHint().width() + 16);
#endif
    stop->setText("Stop");
    bottomToolBar2->addWidget(apply);
    bottomToolBar2->addWidget(prev);
    bottomToolBar2->addWidget(next);
    bottomToolBar2->addWidget(stop);
    bottomToolBar2->addWidget(latest);
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    addToolBarBreak(Qt::BottomToolBarArea);
    auto* bottomToolBar3 = addToolBar("Controls Status");
    addToolBar(Qt::BottomToolBarArea, bottomToolBar3);
    bottomToolBar3->setMovable(false);
    bottomToolBar3->setContextMenuPolicy(Qt::PreventContextMenu);
#else
    auto* bottomToolBar3 = bottomToolBar;
#endif
    
    bottomToolBar3->addWidget(statusLabel_);
    setInteractionMode(InteractionMode::Point);

    connect(zoomButton_, &QToolButton::clicked, this, [this] { setInteractionMode(InteractionMode::Zoom); });
    connect(pointButton_, &QToolButton::clicked, this, [this] { setInteractionMode(InteractionMode::Point); });
    connect(apply, &QPushButton::clicked, this, &MainWindow::applyShot);
    connect(prev, &QPushButton::clicked, this, [this] { stepShot(-1); });
    connect(next, &QPushButton::clicked, this, [this] { stepShot(1); });
    connect(latest, &QPushButton::clicked, this, &MainWindow::latestShot);
    connect(stop, &QPushButton::clicked, this, &MainWindow::onStopOrContinue);
    connect(shotEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyShot);
    connect(shotEdit_, &QLineEdit::textChanged, this, [resizeShotEdit] { QTimer::singleShot(0, [resizeShotEdit] { resizeShotEdit(); }); });
    connect(shotCombo_, &QComboBox::activated, this, [this](int index) {
        const QString shot = shotCombo_->itemData(index).toString();
        QTimer::singleShot(250, this, [this, shot] {
            if (!shot.isEmpty()) {
                shotCombo_->setEditText(shot);
            }
            applyShot();
        });
    });
    connect(dataModeCombo_, &QComboBox::currentIndexChanged, this, [this] { refreshData(); });
    connect(aboutButton_, &QToolButton::clicked, this, &MainWindow::openAboutDialog);

#ifdef Q_OS_IOS
    CachedAuth auth;
    if (!loadCachedAuth(&auth) || auth.token.trimmed().isEmpty() || tokenExpiresSoon(auth.token)) {
        QTimer::singleShot(0, this, &MainWindow::openLoginDialog);
    }
#endif
}

void MainWindow::showPanelContextMenu(PlotWidget* plot, int column, int row, const QPoint& pos)
{
    if (!plot) {
        return;
    }
    selectPlot(column, row);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    auto* menu = new MobileContextMenu("Panel Actions", this);
    menu->addAction("Max", [this] { maximizeCurrentPanel(); });
    if (singlePanelMaximized_) {
        menu->addAction("Show All Panels", [this] { showAllPanels(); });
    }
    menu->addSeparator();
    menu->addAction("Panel Setup", [this] { panelSetupForCurrentPanel(); });
    menu->addAction("Data Source Setup", [this] { dataSourceSetupForCurrentPanel(); });
    menu->addAction("Export Data", [this] { exportCurrentPanelData(); });
    menu->addSeparator();
    menu->addAction("Reset Current Scale", [this] { resetCurrentScale(); });
    menu->addAction("Reset All Panels", [this] { resetScales(); });
    menu->addAction("All Same X Scale", [this] { applyScaleToAll(); });
    menu->addAction("All Same Y Scale", [this] { applyYScaleToAll(); });
    
    // Calculate a nice position: centered or slightly offset
    menu->setMinimumWidth(240);
    menu->adjustSize();
    QPoint globalPos = plot->mapToGlobal(pos);
    globalPos.rx() -= menu->width() / 2;
    globalPos.ry() -= menu->height() / 2;
    menu->move(globalPos);
    menu->show();
#else
    auto* menu = new QMenu(this);
    #ifndef Q_OS_IOS
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
#endif
    
    QAction* maxAction = menu->addAction("Max");
    QAction* showAllAction = menu->addAction("Show All Panels");
    showAllAction->setEnabled(singlePanelMaximized_);
    menu->addSeparator();
    QAction* panelSetupAction = menu->addAction("Panel Setup");
    QAction* dataSourceAction = menu->addAction("Data Source Setup");
    QAction* exportDataAction = menu->addAction("Export Data");
    menu->addSeparator();
    QAction* resetCurrentAction = menu->addAction("Reset Current Scale");
    QAction* resetAllAction = menu->addAction("Reset All Panels");
    QAction* sameXAction = menu->addAction("All Same X Scale");
    QAction* sameYAction = menu->addAction("All Same Y Scale");

    connect(maxAction, &QAction::triggered, this, [this] { QTimer::singleShot(250, this, &MainWindow::maximizeCurrentPanel); });
    connect(showAllAction, &QAction::triggered, this, [this] { QTimer::singleShot(250, this, &MainWindow::showAllPanels); });
    connect(panelSetupAction, &QAction::triggered, this, [this] { QTimer::singleShot(250, this, &MainWindow::panelSetupForCurrentPanel); });
    connect(dataSourceAction, &QAction::triggered, this, [this] { QTimer::singleShot(250, this, &MainWindow::dataSourceSetupForCurrentPanel); });
    connect(exportDataAction, &QAction::triggered, this, [this] { QTimer::singleShot(250, this, &MainWindow::exportCurrentPanelData); });
    connect(resetCurrentAction, &QAction::triggered, this, [this] { QTimer::singleShot(250, this, &MainWindow::resetCurrentScale); });
    connect(resetAllAction, &QAction::triggered, this, [this] { QTimer::singleShot(250, this, &MainWindow::resetScales); });
    connect(sameXAction, &QAction::triggered, this, [this] { QTimer::singleShot(250, this, &MainWindow::applyScaleToAll); });
    connect(sameYAction, &QAction::triggered, this, [this] { QTimer::singleShot(250, this, &MainWindow::applyYScaleToAll); });

    menu->popup(plot->mapToGlobal(pos));
#endif
}

void MainWindow::openCustomizeDialog()
{
    FontSettings& fonts = fontSettings();
    auto* dialog = new BaseDialog(this);
    dialog->setWindowTitle("Customize Fonts");
    if (QApplication::palette().color(QPalette::Window).lightness() >= 128) {
        dialog->setStyleSheet(
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
    auto* layout = new QFormLayout(dialog);
    auto* family = new QFontComboBox(dialog);
    family->setCurrentFont(QFont(fonts.family));
    auto* legendSize = new QSpinBox(dialog);
    auto* axisSize = new QSpinBox(dialog);
    auto* unitSize = new QSpinBox(dialog);
    auto* uiSize = new QSpinBox(dialog);
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
    auto* ok = new QPushButton("OK", dialog);
    auto* cancel = new QPushButton("Cancel", dialog);
    buttons->addStretch(1);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    layout->addRow(buttons);
    connect(ok, &QPushButton::clicked, dialog, &BaseDialog::accept);
    connect(cancel, &QPushButton::clicked, dialog, &BaseDialog::reject);

    connect(dialog, &BaseDialog::accepted, this, [this, family, legendSize, axisSize, unitSize, uiSize]() {
        FontSettings& fonts = fontSettings();
        fonts.family = family->currentFont().family();
        fonts.legendSize = legendSize->value();
        fonts.axisSize = axisSize->value();
        fonts.unitSize = unitSize->value();
        fonts.uiSize = uiSize->value();
        saveFontSettings(rootPath_);
        applyUiFont();
        refreshPlotFonts();
    });
    #ifndef Q_OS_IOS
    connect(dialog, &BaseDialog::finished, dialog, &QObject::deleteLater);
#endif
#ifdef Q_OS_IOS
    new DialogOverlayManager(this, dialog);
#else
    #ifdef Q_OS_IOS
    new DialogOverlayManager(this, dialog);
    #else
    dialog->open();
    #endif
#endif
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
    // The top strip describes the shot currently displayed by the plots.
    // latestShot_ is maintained independently by the background poll and is
    // only applied when the user explicitly chooses Latest.
    QString shot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
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

    const auto properties = readApiSettings(rootPath_);
    const QString api = apiUrl.trimmed().isEmpty() ? properties.value("ApiUrl") : apiUrl.trimmed();
    const QString token = properties.value("Token");
    const QString prefix = properties.value("Authorization_Prefix", properties.value("Init_Prefix", "Bearer"));
    const QString charset = properties.value("Charset", "UTF-8");
    if (api.isEmpty() || token.isEmpty() || trimmedShot.isEmpty()) {
        pendingTopSummaryShot_.clear();
        return;
    }

    bool shotOk = false;
    const int shotNumber = trimmedShot.toInt(&shotOk);
    if (!shotOk) {
        pendingTopSummaryShot_.clear();
        return;
    }

    auto* manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(api + "/pcsEastTree"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=" + charset);
    request.setRawHeader("Authorization", (prefix + " " + token).toUtf8());
    request.setRawHeader("User-Agent", "MdsScope/0.1");
    request.setTransferTimeout(2500);

    QJsonObject payload;
    payload.insert("treeshot", shotNumber);

    QNetworkReply* reply = manager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager, trimmedShot, generation]() {
        reply->deleteLater();
        manager->deleteLater();

        if (generation != topSummaryGeneration_ || pendingTopSummaryShot_ != trimmedShot) {
            return;
        }

        const QByteArray body = reply->readAll();
        const auto error = reply->error();
        bool ok = false;
        QString ip, pulse, it, shotTime;

        if (error == QNetworkReply::NoError && !body.isEmpty()) {
            const QJsonObject root = QJsonDocument::fromJson(body).object();
            const QString code = root.value("code").isString()
                ? root.value("code").toString()
                : QString::number(root.value("code").toInt());
            if (code == "20000") {
                const QJsonObject data = root.value("data").toObject();
                if (!data.isEmpty()) {
                    auto scalarText = [](const QJsonValue& value) {
                        if (value.isString()) return value.toString().trimmed();
                        if (value.isDouble()) return QString::number(value.toDouble(), 'g', 8);
                        if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
                        return QString();
                    };
                    ip = scalarText(data.value("pcrl01"));
                    pulse = scalarText(data.value("shot_len"));
                    it = scalarText(data.value("iv"));
                    shotTime = scalarText(data.value("curr_time"));
                    ok = true;
                }
            }
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
    });
}

void MainWindow::setStatus(const QString& text)
{
    qDebug() << "setStatus:" << text;
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
