// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"

QString trimQuotes(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2) {
        const QChar first = value.front();
        const QChar last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            value = value.mid(1, value.size() - 2);
        }
    }
    return value.trimmed();
}

double parseDouble(const QString& value)
{
    bool ok = false;
    const double parsed = trimQuotes(value).toDouble(&ok);
    return ok ? parsed : qQNaN();
}

int parseInt(const QHash<QString, QString>& map, const QString& key, int fallback)
{
    bool ok = false;
    const int parsed = map.value(key).trimmed().toInt(&ok);
    return ok ? parsed : fallback;
}

QString javaUnescape(QString value)
{
    value.replace("\\:", ":");
    value.replace("\\=", "=");
    value.replace("\\\\", "\\");
    return value;
}

QHash<QString, QString> readKeyValueFile(const QString& path)
{
    QHash<QString, QString> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }

    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        int pos = line.indexOf(':');
        const int eq = line.indexOf('=');
        if (eq >= 0 && (pos < 0 || eq < pos)) {
            pos = eq;
        }
        if (pos < 0) {
            continue;
        }
        const QString key = line.left(pos).trimmed();
        const QString value = javaUnescape(line.mid(pos + 1).trimmed());
        out.insert(key, value);
    }
    return out;
}

