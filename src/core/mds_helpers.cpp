// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.h"

void writeLine(QTextStream& out, const QString& key, const QString& value)
{
    out << key << ':' << value << '\n';
}

QString escapedMdsExpr(QString expr)
{
    expr = expr.trimmed();
    expr.replace("\"", "\\\"");
    return expr;
}

QString normalizedMdsSignal(QString expr)
{
    expr = expr.trimmed();
    while (expr.startsWith("\\\\")) {
        expr.remove(0, 1);
    }
    return expr;
}

QString effectiveSignalShot(const PlotSpec& plot, const SignalSpec& sig)
{
    const QString shot = sig.shot.trimmed();
    return shot.isEmpty() ? plot.shot.trimmed() : shot;
}

QStringList expandedShotList(const QString& expression)
{
    constexpr int kMaxExpandedShots = 128;
    QStringList out;
    QSet<QString> seen;
    const QStringList parts = expression.split(';', Qt::SkipEmptyParts);
    for (QString part : parts) {
        part = part.trimmed();
        if (part.isEmpty()) {
            continue;
        }

        int dash = -1;
        for (int i = 1; i < part.size(); ++i) {
            if (part.at(i) == '-') {
                dash = i;
                break;
            }
        }

        QStringList shots;
        if (dash > 0) {
            bool startOk = false;
            bool endOk = false;
            const int start = part.left(dash).trimmed().toInt(&startOk);
            const int end = part.mid(dash + 1).trimmed().toInt(&endOk);
            if (startOk && endOk) {
                const int step = start <= end ? 1 : -1;
                for (int shot = start;; shot += step) {
                    shots.push_back(QString::number(shot));
                    if (shot == end || shots.size() >= kMaxExpandedShots) {
                        break;
                    }
                }
            } else {
                shots.push_back(part);
            }
        } else {
            shots.push_back(part);
        }

        for (const QString& shot : std::as_const(shots)) {
            const QString trimmed = shot.trimmed();
            if (trimmed.isEmpty() || seen.contains(trimmed) || out.size() >= kMaxExpandedShots) {
                continue;
            }
            seen.insert(trimmed);
            out.push_back(trimmed);
        }
        if (out.size() >= kMaxExpandedShots) {
            break;
        }
    }
    return out;
}

LayoutConfig expandedShotLayout(const LayoutConfig& config)
{
    LayoutConfig expanded = config;
    for (QVector<PlotSpec>& column : expanded.columns) {
        for (PlotSpec& plot : column) {
            QVector<SignalSpec> expandedSignals;
            expandedSignals.reserve(plot.signalSpecs.size());
            for (const SignalSpec& sig : std::as_const(plot.signalSpecs)) {
                const QStringList shots = expandedShotList(effectiveSignalShot(plot, sig));
                if (shots.size() <= 1) {
                    expandedSignals.push_back(sig);
                    continue;
                }
                for (const QString& shot : shots) {
                    SignalSpec copy = sig;
                    copy.shot = shot;
                    copy.manualColor = false;
                    copy.colorName = colorForIndex(expandedSignals.size());
                    expandedSignals.push_back(std::move(copy));
                }
            }
            plot.signalSpecs = std::move(expandedSignals);
        }
    }
    return expanded;
}

DataReadMode effectiveSignalReadMode(DataReadMode globalMode, const SignalSpec& sig)
{
    // The higher-precision of the global mode and the per-signal override wins.
    // Precision order: Thin < Medium < Full.
    auto rank = [](DataReadMode m) {
        switch (m) {
        case DataReadMode::Thin:   return 0;
        case DataReadMode::Medium: return 1;
        case DataReadMode::Full:   return 2;
        }
        return 0;
    };
    return rank(sig.readMode) >= rank(globalMode) ? sig.readMode : globalMode;
}
