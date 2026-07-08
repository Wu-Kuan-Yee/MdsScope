// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mdsscope_app.hpp"
#include <atomic>
#include <functional>
#include <memory>

using LoadedSignalCallback = std::function<void(const LoadedSignal&)>;

QVector<LoadedSignal> fetchMdsSignals(const LayoutConfig& snapshot,
                                      DataReadMode readMode,
                                      LoadedSignalCallback callback = {},
                                      std::shared_ptr<std::atomic_bool> cancel = {});
void warmMdsConnections(const LayoutConfig& snapshot, std::shared_ptr<std::atomic_bool> cancel = {});
SignalSeries fetchMdsSignal(const PlotSpec& plot, const SignalSpec& sig, DataReadMode readMode);
void clearMdsCurrentThreadConnections();
