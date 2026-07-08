// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mdsscope_internal.hpp"

QString trimQuotes(QString value);
double parseDouble(const QString& value);
int parseInt(const QHash<QString, QString>& map, const QString& key, int fallback = 0);
QString javaUnescape(QString value);
