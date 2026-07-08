// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "export_dialog.hpp"
#include "shared.hpp"
#include "mds_client.hpp"


void MainWindow::openExportDataDialog()
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    const ExportFormat defaultFormat = exportFormatFromSetting(settings.value("export/format", "text").toString());
    ExportDataDialog dialog(config_, exportBasePath_, defaultFormat, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    settings.setValue("export/format", exportFormatSettingValue(dialog.exportFormat()));
    exportDataForPanels(dialog.selectedPanels(),
                        dialog.outputBaseDir(),
                        static_cast<int>(dialog.exportFormat()),
                        static_cast<int>(dialog.exportRange()),
                        dialog.customXMin(),
                        dialog.customXMax());
}

void MainWindow::exportCurrentPanelData()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0) {
        return;
    }
    if (selectedColumn_ >= config_.columns.size() || selectedRow_ >= config_.columns[selectedColumn_].size()) {
        return;
    }
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    const ExportFormat defaultFormat = exportFormatFromSetting(settings.value("export/format", "text").toString());
    LayoutConfig dialogConfig;
    dialogConfig.columns = {{config_.columns[selectedColumn_][selectedRow_]}};
    const QString currentShot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    if (!currentShot.isEmpty()) {
        dialogConfig.columns[0][0].shot = currentShot;
    }
    dialogConfig = expandedShotLayout(dialogConfig);
    const PlotSpec& plot = dialogConfig.columns[0][0];
    ExportDataDialog dialog(config_, exportBasePath_, defaultFormat, this, &plot);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    settings.setValue("export/format", exportFormatSettingValue(dialog.exportFormat()));
    QSet<int> selectedSignalIndexes;
    for (int signal : dialog.selectedSignals()) {
        selectedSignalIndexes.insert(signal);
    }
    QHash<QString, QSet<int>> signalFilter;
    signalFilter.insert(QStringLiteral("%1:%2").arg(selectedColumn_).arg(selectedRow_), selectedSignalIndexes);
    exportDataForPanels({{selectedColumn_, selectedRow_}},
                        dialog.outputBaseDir(),
                        static_cast<int>(dialog.exportFormat()),
                        static_cast<int>(dialog.exportRange()),
                        dialog.customXMin(),
                        dialog.customXMax(),
                        signalFilter);
}

void MainWindow::exportDataForPanels(const QVector<QPair<int, int>>& panels,
                                     const QString& baseDirPath,
                                     int exportFormat,
                                     int exportRange,
                                     double customXMin,
                                     double customXMax,
                                     const QHash<QString, QSet<int>>& signalFilter)
{
    if (panels.isEmpty()) {
        QMessageBox::warning(this, "Export Data", "Select at least one panel.");
        return;
    }
    if (baseDirPath.trimmed().isEmpty()) {
        QMessageBox::warning(this, "Export Data", "Choose an output directory.");
        return;
    }
    exportBasePath_ = QDir(baseDirPath.trimmed()).absolutePath();
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    settings.setValue("export/base_dir", exportBasePath_);
    settings.setValue("export/format", exportFormatSettingValue(static_cast<ExportFormat>(exportFormat)));

    LayoutConfig snapshot = config_;
    QSet<QPair<int, int>> selected;
    QHash<QString, QRectF> viewRanges;
    const ExportRange rangeMode = static_cast<ExportRange>(exportRange);
    const bool useCurrentView = rangeMode == ExportRange::CurrentView;
    const bool useCustomRange = rangeMode == ExportRange::CustomXRange && std::isfinite(customXMin) && std::isfinite(customXMax);
    if (useCustomRange && customXMin > customXMax) {
        std::swap(customXMin, customXMax);
    }
    for (const auto& panel : panels) {
        selected.insert(panel);
        if (useCurrentView
            && panel.first >= 0 && panel.first < plotWidgets_.size()
            && panel.second >= 0 && panel.second < plotWidgets_[panel.first].size()
            && plotWidgets_[panel.first][panel.second]) {
            viewRanges.insert(QStringLiteral("%1:%2").arg(panel.first).arg(panel.second),
                              plotWidgets_[panel.first][panel.second]->currentView());
        }
    }
    const QString currentShot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            if (!selected.contains({c, r})) {
                snapshot.columns[c][r].signalSpecs.clear();
                continue;
            }
            if (!currentShot.isEmpty()) {
                snapshot.columns[c][r].shot = currentShot;
            }
        }
    }
    snapshot = expandedShotLayout(snapshot);
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            const QString filterKey = QStringLiteral("%1:%2").arg(c).arg(r);
            if (!signalFilter.contains(filterKey)) {
                continue;
            }
            const QSet<int> selectedSignals = signalFilter.value(filterKey);
            QVector<SignalSpec> filteredSignals;
            filteredSignals.reserve(selectedSignals.size());
            for (int i = 0; i < snapshot.columns[c][r].signalSpecs.size(); ++i) {
                if (selectedSignals.contains(i)) {
                    filteredSignals.push_back(snapshot.columns[c][r].signalSpecs[i]);
                }
            }
            snapshot.columns[c][r].signalSpecs = std::move(filteredSignals);
        }
    }

    const DataReadMode readMode = dataModeCombo_
                                      ? static_cast<DataReadMode>(dataModeCombo_->currentData().toInt())
                                      : DataReadMode::Thin;
    const ExportFormat format = static_cast<ExportFormat>(exportFormat);
    setStatus(QString("Exporting data from %1 panels...").arg(panels.size()));
    QPointer<MainWindow> self(this);
    QThreadPool::globalInstance()->start([self,
                                          snapshot,
                                          baseDirPath = baseDirPath.trimmed(),
                                          readMode,
                                          format,
                                          useCurrentView,
                                          useCustomRange,
                                          customXMin,
                                          customXMax,
                                          viewRanges] {
        QStringList errors;
        int written = 0;
        QDir baseDir(baseDirPath);
        if (!baseDir.exists() && !QDir().mkpath(baseDirPath)) {
            errors.push_back("Cannot create " + baseDirPath);
        }
        if (errors.isEmpty() && !baseDir.mkpath("output")) {
            errors.push_back("Cannot create " + baseDir.filePath("output"));
        }
        QDir outputDir(baseDir.filePath("output"));
        if (errors.isEmpty()) {
            const QVector<LoadedSignal> loaded = fetchMdsSignals(snapshot, readMode);
            for (const LoadedSignal& item : loaded) {
                if (item.column < 0 || item.row < 0 || item.signal < 0
                    || item.column >= snapshot.columns.size()
                    || item.row >= snapshot.columns[item.column].size()
                    || item.signal >= snapshot.columns[item.column][item.row].signalSpecs.size()) {
                    continue;
                }
                const PlotSpec& plot = snapshot.columns[item.column][item.row];
                const SignalSpec& sig = plot.signalSpecs[item.signal];
                if (sig.hidden) {
                    continue;
                }
                const QString shot = exportFileToken(item.shot.isEmpty() ? effectiveSignalShot(plot, sig) : item.shot);
                const QString tree = exportFileToken(sig.experiment);
                const QString signal = exportFileToken(normalizedMdsSignal(sig.yExpr));
                const QRectF viewRange = viewRanges.value(QStringLiteral("%1:%2").arg(item.column).arg(item.row));
                bool useXRange = false;
                double xmin = qQNaN();
                double xmax = qQNaN();
                if (useCustomRange) {
                    useXRange = true;
                    xmin = customXMin;
                    xmax = customXMax;
                } else if (useCurrentView && viewRange.isValid()) {
                    useXRange = true;
                    xmin = std::min(viewRange.left(), viewRange.right());
                    xmax = std::max(viewRange.left(), viewRange.right());
                }
                QString baseName = QString("%1-%2-%3").arg(shot, tree, signal);
                const QString rangeSuffix = exportRangeFileSuffix(useXRange, xmin, xmax);
                if (!rangeSuffix.isEmpty()) {
                    baseName += "-" + rangeSuffix;
                }
                const QString path = uniqueExportPath(outputDir, baseName, format);
                QString error;
                if (writeSeriesDataFile(path,
                                        item.series,
                                        format,
                                        useXRange,
                                        xmin,
                                        xmax,
                                        &error)) {
                    ++written;
                } else {
                    errors.push_back(error);
                }
            }
        }

        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self, [self, written, errors, outputPath = outputDir.absolutePath()] {
            if (!self) {
                return;
            }
            if (!errors.isEmpty()) {
                QMessageBox::warning(self, "Export Data", errors.join("\n"));
            }
            self->setStatus(QString("Exported %1 files to %2").arg(written).arg(outputPath));
        }, Qt::QueuedConnection);
    });
}
