// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

enum class SshMode {
    Disabled = 0,
    Auto = 1,
    Always = 2,
};

struct SshSettings {
    SshMode mode = SshMode::Disabled;
    QString host;
    int port = 22;
    QString user;
    QString password;
    QString identityFile;
};
