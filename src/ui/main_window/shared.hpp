// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mdsscope_internal.hpp"

void setLabelTextIfChanged(QLabel* label, const QString& text);
void clearCustomRanges(PlotSpec* plot);
bool loadedSignalMatchesConfig(const LayoutConfig& config, const LoadedSignal& item);
QString plotRefreshSignature(const PlotSpec& plot);
QString layoutRefreshSignature(const LayoutConfig& config);
bool signalDataSourcesEqual(const QVector<SignalSpec>& lhs, const QVector<SignalSpec>& rhs);
bool signalSpecsEqual(const QVector<SignalSpec>& lhs, const QVector<SignalSpec>& rhs);
bool optionalDoubleFromText(const QString& text, double* value);
QString exportFileToken(QString text);

enum class ExportFormat {
    Text,
    Csv,
    Tsv,
    Json,
};

enum class ExportRange {
    AllData,
    CurrentView,
    CustomXRange,
};

QString exportFormatExtension(ExportFormat format);
QString exportFormatName(ExportFormat format);
QString exportFormatSettingValue(ExportFormat format);
ExportFormat exportFormatFromSetting(QString value);
QString uniqueExportPath(const QDir& dir, const QString& baseName, ExportFormat format);
QString exportRangeFileSuffix(bool useXRange, double xmin, double xmax);
bool writeSeriesDataFile(const QString& path,
                         const SignalSeries& series,
                         ExportFormat format,
                         bool useXRange,
                         double xmin,
                         double xmax,
                         QString* error);
QIcon infoIcon();
QIcon recentArrowIcon();
