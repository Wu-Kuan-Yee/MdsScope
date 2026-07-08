// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.h"
#include "shared.h"


void MainWindow::loadDefaultEnvironment(bool useLatestWhenNoCurrentShot)
{
    const QString defaultTomlConfig = QDir(environmentPath_).filePath("init.toml");
    const QString defaultWebscpConfig = QDir(environmentPath_).filePath("init.webscp");
    if (QFileInfo::exists(defaultTomlConfig)) {
        loadEnvironmentFile(defaultTomlConfig, useLatestWhenNoCurrentShot, false, true);
    } else if (QFileInfo::exists(defaultWebscpConfig)) {
        loadEnvironmentFile(defaultWebscpConfig, useLatestWhenNoCurrentShot, false, true);
    } else {
        loadEnvironmentList(useLatestWhenNoCurrentShot);
    }
}

void MainWindow::loadEnvironmentList(bool useLatestWhenNoCurrentShot)
{
    QDir dir(environmentPath_);
    const auto files = dir.entryInfoList({"*.toml", "*.webscp"}, QDir::Files, QDir::Name);
    if (!files.isEmpty()) {
        loadEnvironmentFile(files.first().absoluteFilePath(), useLatestWhenNoCurrentShot, false, true);
        return;
    }
    setStatus("No environment files found");
}

void MainWindow::loadSelectedEnvironment()
{
    openEnvironmentFile();
}

QString MainWindow::rememberedFileDialogDir() const
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    const QString savedPath = settings.value("files/last_dir").toString().trimmed();
    return !savedPath.isEmpty() && QDir(savedPath).exists()
               ? QDir(savedPath).absolutePath()
               : environmentPath_;
}

void MainWindow::rememberFileDialogDir(const QString& path)
{
    QFileInfo info(path);
    const QString dirPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (dirPath.isEmpty()) {
        return;
    }
    const QString selectedPath = QDir(dirPath).absolutePath();
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    settings.setValue("files/last_dir", selectedPath);
}

QStringList MainWindow::recentEnvironmentFiles() const
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    QStringList files = settings.value("files/recent").toStringList();
    QStringList cleaned;
    for (const QString& file : files) {
        const QString path = QFileInfo(file).absoluteFilePath();
        if (!path.isEmpty() && QFileInfo::exists(path) && !cleaned.contains(path)) {
            cleaned.push_back(path);
        }
        if (cleaned.size() >= 10) {
            break;
        }
    }
    return cleaned;
}

void MainWindow::rememberRecentEnvironmentFile(const QString& path)
{
    const QString filePath = QFileInfo(path).absoluteFilePath();
    if (filePath.isEmpty()) {
        return;
    }
    QStringList files = recentEnvironmentFiles();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > 10) {
        files.removeLast();
    }
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    settings.setValue("files/recent", files);
}

void MainWindow::clearRecentEnvironmentFiles()
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    settings.remove("files/recent");
    refreshRecentEnvironmentMenu();
}

void MainWindow::openEnvironmentFile()
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    const QString allFilter = "All MdsScope Config (*.toml *.webscp)";
    const QString tomlFilter = "MdsScope TOML (*.toml)";
    const QString webscpFilter = "Legacy WebScope Config (*.webscp)";
    const QString filters = allFilter + ";;" + tomlFilter + ";;" + webscpFilter + ";;All Files (*)";
    QString selectedFilter = settings.value("files/open_filter", allFilter).toString();
    const QString path = QFileDialog::getOpenFileName(this, "Open MdsScope Config", rememberedFileDialogDir(), filters, &selectedFilter);
    if (!path.isEmpty()) {
        settings.setValue("files/open_filter", selectedFilter);
        rememberFileDialogDir(path);
        loadEnvironmentFile(path);
    }
}

void MainWindow::openRecentEnvironmentFile(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, "Open MdsScope Config", "Recent file no longer exists:\n" + path);
        refreshRecentEnvironmentMenu();
        return;
    }
    rememberFileDialogDir(path);
    loadEnvironmentFile(path);
}

void MainWindow::refreshRecentEnvironmentMenu()
{
    if (!recentEnvironmentMenu_) {
        return;
    }
    recentEnvironmentMenu_->clear();
    const QStringList files = recentEnvironmentFiles();
    if (files.isEmpty()) {
        QAction* empty = recentEnvironmentMenu_->addAction("No Recent Files");
        empty->setEnabled(false);
        return;
    }

    const QFontMetrics fm(recentEnvironmentMenu_->font());
    int menuTextWidth = fm.horizontalAdvance("Clear Recent Files");
    for (const QString& path : files) {
        menuTextWidth = std::max(menuTextWidth, fm.horizontalAdvance(QFileInfo(path).fileName()));
    }
    const int menuWidth = std::clamp(menuTextWidth + 56, 220, 520);
    recentEnvironmentMenu_->setMinimumWidth(menuWidth);
    for (const QString& path : files) {
        const QFileInfo info(path);
        QAction* action = recentEnvironmentMenu_->addAction(fm.elidedText(info.fileName(), Qt::ElideMiddle, menuWidth - 36));
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path] { openRecentEnvironmentFile(path); });
    }
    recentEnvironmentMenu_->addSeparator();
    QAction* clearAction = recentEnvironmentMenu_->addAction("Clear Recent Files");
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearRecentEnvironmentFiles);
}

void MainWindow::showRecentEnvironmentMenu()
{
    if (!openButton_ || !recentEnvironmentMenu_) {
        return;
    }
    refreshRecentEnvironmentMenu();
    recentEnvironmentMenu_->popup(openButton_->mapToGlobal(QPoint(0, openButton_->height())));
}

QStringList MainWindow::recentShotExpressions() const
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    QStringList shots = settings.value("shot/recent").toStringList();
    QStringList cleaned;
    for (const QString& shot : shots) {
        const QString value = shot.trimmed();
        if (!value.isEmpty() && !cleaned.contains(value)) {
            cleaned.push_back(value);
        }
        if (cleaned.size() >= 10) {
            break;
        }
    }
    return cleaned;
}

void MainWindow::rememberShotExpression(const QString& shot)
{
    const QString value = shot.trimmed();
    if (value.isEmpty()) {
        return;
    }
    QStringList shots = recentShotExpressions();
    shots.removeAll(value);
    shots.prepend(value);
    while (shots.size() > 10) {
        shots.removeLast();
    }
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    settings.setValue("shot/recent", shots);
    refreshShotHistory();
}

void MainWindow::refreshShotHistory()
{
    if (!shotCombo_ || !shotEdit_) {
        return;
    }
    const QString current = shotEdit_->text();
    QSignalBlocker comboBlocker(shotCombo_);
    QSignalBlocker editBlocker(shotEdit_);
    shotCombo_->clear();
    const QFontMetrics fm(shotCombo_->font());
    for (const QString& shot : recentShotExpressions()) {
        shotCombo_->addItem(fm.elidedText(shot, Qt::ElideMiddle, 300), shot);
        shotCombo_->setItemData(shotCombo_->count() - 1, shot, Qt::ToolTipRole);
    }
    shotCombo_->setEditText(current);
}

bool MainWindow::loadEnvironmentFile(const QString& path,
                                     bool useLatestWhenNoCurrentShot,
                                     bool rememberRecent,
                                     bool prewarmBeforeRefresh)
{
    const QString previousShot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    const bool shouldFetchLatest = useLatestWhenNoCurrentShot && previousShot.isEmpty();
    pendingPrewarmRefresh_ = false;
    cancelPrewarmConnections();
    activeRefreshKey_.clear();
    activePanelRefreshKey_.clear();
    pendingRefresh_ = false;
    pendingPanelRefresh_ = false;
    queuedRefreshKey_.clear();
    queuedPanelRefreshKey_.clear();
    queuedLoadedSignals_.clear();
    queuedLoadedSignalApply_ = false;
    config_ = parseEnvironment(path);
    if (!shouldFetchLatest) {
        updateShotControlsFromConfig(previousShot);
    }
    selectedColumn_ = -1;
    selectedRow_ = -1;
    rebuildGrid();
    if (shouldFetchLatest) {
        setLabelTextIfChanged(topInfoLabel_, QStringLiteral("Shot: --"));
        setLabelTextIfChanged(ipInfoLabel_, QStringLiteral("Ip: --"));
        setLabelTextIfChanged(pulseInfoLabel_, QStringLiteral("Pulse: --"));
        setLabelTextIfChanged(itInfoLabel_, QStringLiteral("It: --"));
        setLabelTextIfChanged(timeInfoLabel_, QStringLiteral("Time: --"));
    } else {
        updateTopInfoLabels();
    }
    setStatus(QString("Loaded %1").arg(QFileInfo(path).fileName()));
    if (rememberRecent) {
        rememberRecentEnvironmentFile(path);
    }
    if (prewarmBeforeRefresh && !shouldFetchLatest) {
        pendingPrewarmRefresh_ = true;
        setStatus(QString("Preparing MDS connections for %1...").arg(QFileInfo(path).fileName()));
        if (!prewarmConnections()) {
            pendingPrewarmRefresh_ = false;
            refreshData();
        }
    } else if (!shouldFetchLatest) {
        refreshData();
    } else if (prewarmBeforeRefresh) {
        pendingPrewarmRefresh_ = true;
    }
    if (shouldFetchLatest) {
        fetchLatestShotAsync();
    }
    QTimer::singleShot(0, this, [this] {
        if (shotEdit_) {
            shotEdit_->clearFocus();
        }
        if (gridHost_) {
            gridHost_->setFocus(Qt::OtherFocusReason);
        }
    });
    return true;
}

void MainWindow::saveCurrentEnvironment()
{
    if (config_.filePath.isEmpty()) {
        saveCurrentEnvironmentAs();
        return;
    }
    if (saveEnvironmentFile(config_.filePath)) {
        setStatus("Saved " + QFileInfo(config_.filePath).fileName());
    }
}

void MainWindow::saveCurrentEnvironmentAs()
{
    const QString path = QFileDialog::getSaveFileName(this, "Save MdsScope Config", rememberedFileDialogDir(), "MdsScope Config (*.toml)");
    if (path.isEmpty()) {
        return;
    }
    if (saveEnvironmentFile(path)) {
        const QFileInfo info(path);
        config_.filePath = info.suffix().isEmpty() ? path + ".toml" : path;
        rememberFileDialogDir(config_.filePath);
        loadEnvironmentFile(config_.filePath);
        setStatus("Saved " + QFileInfo(config_.filePath).fileName());
    }
}

bool MainWindow::saveEnvironmentFile(const QString& path) const
{
    QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    QString primaryPath = path;
    if (suffix != "toml" && suffix != "webscp") {
        primaryPath += ".toml";
        info = QFileInfo(primaryPath);
    }

    const QString baseName = info.completeBaseName();
    const QDir dir(info.absolutePath());
    const QString tomlPath = suffix == "toml" ? primaryPath : dir.filePath(baseName + ".toml");
    const QString webscpPath = suffix == "webscp" ? primaryPath : dir.filePath(baseName + ".webscp");

    QString tomlError;
    const bool tomlOk = writeEnvironmentToml(config_, tomlPath, &tomlError);
    const bool webscpOk = saveWebscpEnvironmentFile(webscpPath);

    if (!tomlOk && !webscpOk) {
        QMessageBox::warning(nullptr, "Save", "Cannot write TOML: " + tomlError + "\nCannot write webscp: " + webscpPath);
        return false;
    }
    if (!tomlOk) {
        QMessageBox::warning(nullptr, "Save", "Saved webscp, but TOML export failed: " + tomlError);
    }
    if (!webscpOk) {
        QMessageBox::warning(nullptr, "Save", "Saved TOML, but webscp export failed: " + webscpPath);
    }
    return tomlOk && webscpOk;
}

bool MainWindow::saveWebscpEnvironmentFile(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(nullptr, "Save", "Cannot write " + path);
        return false;
    }
    QTextStream out(&file);
    writeLine(out, "Title_Font", "java.awt.Font[family=Times New Roman,name=Times New Roman,style=plain,size=16]");
    writeLine(out, "Measurement_Units", "java.awt.Font[family=Times New Roman,name=Times New Roman,style=plain,size=14]");
    writeLine(out, "Coordinate_Axis", "java.awt.Font[family=Times New Roman,name=Times New Roman,style=plain,size=12]");
    writeLine(out, "Grid_Mode", "1");
    writeLine(out, "X_Lines", "5");
    writeLine(out, "Y_Lines", "5");
    writeLine(out, "Extraction_points", "2000");
    writeLine(out, "Vertical_offset", "0");
    writeLine(out, "Horizontal_offset", "0");
    writeLine(out, "xmax", "");
    writeLine(out, "xmin", "");
    writeLine(out, "ymax", "");
    writeLine(out, "ymin", "");
    writeLine(out, "File_position", QDir(rootPath_).filePath("data"));
    out << "\n \n";
    writeLine(out, "cols", QString::number(config_.columns.size()));
    out << " \n";
    for (int c = 0; c < config_.columns.size(); ++c) {
        writeLine(out, QString("%1.rows").arg(c + 1), QString::number(config_.columns[c].size()));
        for (int r = 0; r < config_.columns[c].size(); ++r) {
            const PlotSpec& plot = config_.columns[c][r];
            const QString p = QString("%1_%2.").arg(c + 1).arg(r + 1);
            writeLine(out, p + "shot_txt", plot.shot);
            writeLine(out, p + "num_shot", "1");
            writeLine(out, p + "num_sig", QString::number(plot.signalSpecs.size()));
            writeLine(out, p + "title_position", "0");
            writeLine(out, p + "y_log", "0");
            writeLine(out, p + "legend", "1");
            writeLine(out, p + "xseting_mode", plot.customXRange ? "0" : "1");
            writeLine(out, p + "yseting_mode", plot.customYRange ? "0" : "1");
            writeLine(out, p + "x_line_num", "5");
            writeLine(out, p + "y_line_num", "5");
            writeLine(out, p + "extraction_points", QString::number(plot.extractionPoints));
            writeLine(out, p + "vertical_offset", "0");
            writeLine(out, p + "horizontal_offset", "0");
            writeLine(out, p + "grid_mode", plot.grid ? "1" : "0");
            writeLine(out, p + "xmin_custom", std::isfinite(plot.xmin) ? QString::number(plot.xmin, 'g', 12) : "");
            writeLine(out, p + "xmax_custom", std::isfinite(plot.xmax) ? QString::number(plot.xmax, 'g', 12) : "");
            writeLine(out, p + "ymin_custom", std::isfinite(plot.ymin) ? QString::number(plot.ymin, 'g', 12) : "");
            writeLine(out, p + "ymax_custom", std::isfinite(plot.ymax) ? QString::number(plot.ymax, 'g', 12) : "");
            writeLine(out, p + "title", plot.title);
            writeLine(out, p + "xlabel", plot.xLabel);
            writeLine(out, p + "ylabel", plot.yLabel);
            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                const SignalSpec& sig = plot.signalSpecs[s];
                const bool defaultColor = !sig.manualColor || isDefaultSeriesColor(sig.colorName, s);
                const int colorIndex = defaultColor ? s : colorIndexForName(sig.colorName, s);
                writeLine(out, p + QString("color_%1_%2").arg(r + 1).arg(s + 1), QString::number(colorIndex));
                writeLine(out, p + QString("markers_%1_%2").arg(r + 1).arg(s + 1), "0");
                writeLine(out, p + QString("interpolate_%1_%2").arg(r + 1).arg(s + 1), "1");
                writeLine(out, p + QString("color_name_%1").arg(s + 1), defaultColor ? QString() : sig.colorName);
                writeLine(out, p + QString("color_manual_%1").arg(s + 1), defaultColor ? "0" : "1");
                writeLine(out, p + QString("shot_%1").arg(s + 1), sig.shot);
                writeLine(out, p + QString("y_expr_%1").arg(s + 1), sig.yExpr);
                writeLine(out, p + QString("x_expr_%1").arg(s + 1), sig.xExpr);
                writeLine(out, p + QString("experiment_%1").arg(s + 1), sig.experiment);
                writeLine(out, p + QString("server_ip_%1").arg(s + 1), sig.serverIp);
            }
        }
    }
    return true;
}
