// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

int runSshTunnelBenchmark(const QString& configPath, const QString& shotOverride = {});
int runSshApiTest(const QString& rootPath);
