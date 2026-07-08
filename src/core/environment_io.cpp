// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.h"
#include "text_utils.h"

QString tomlEscape(QString value)
{
    value.replace("\\", "\\\\");
    value.replace("\"", "\\\"");
    value.replace("\n", "\\n");
    value.replace("\r", "\\r");
    value.replace("\t", "\\t");
    return "\"" + value + "\"";
}

QString tomlUnescape(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.mid(1, value.size() - 2);
    }
    QString out;
    out.reserve(value.size());
    bool escaped = false;
    for (QChar ch : value) {
        if (escaped) {
            if (ch == 'n') {
                out += '\n';
            } else if (ch == 'r') {
                out += '\r';
            } else if (ch == 't') {
                out += '\t';
            } else {
                out += ch;
            }
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else {
            out += ch;
        }
    }
    if (escaped) {
        out += '\\';
    }
    return out;
}

QString stripTomlComment(QString line)
{
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && inString) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            inString = !inString;
            continue;
        }
        if (ch == '#' && !inString) {
            return line.left(i).trimmed();
        }
    }
    return line.trimmed();
}

bool tomlBool(const QString& value, bool fallback = false)
{
    const QString v = value.trimmed().toLower();
    if (v == "true") {
        return true;
    }
    if (v == "false") {
        return false;
    }
    return fallback;
}

int tomlInt(const QString& value, int fallback = 0)
{
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    return ok ? parsed : fallback;
}

double tomlDouble(const QString& value)
{
    bool ok = false;
    const double parsed = value.trimmed().toDouble(&ok);
    return ok ? parsed : qQNaN();
}

void writeTomlString(QTextStream& out, const QString& key, const QString& value)
{
    out << key << " = " << tomlEscape(value) << '\n';
}

void writeTomlStringIfNotEmpty(QTextStream& out, const QString& key, const QString& value)
{
    if (!value.isEmpty()) {
        writeTomlString(out, key, value);
    }
}

void writeTomlBool(QTextStream& out, const QString& key, bool value)
{
    out << key << " = " << (value ? "true" : "false") << '\n';
}

void writeTomlBoolIfNotDefault(QTextStream& out, const QString& key, bool value, bool defaultValue)
{
    if (value != defaultValue) {
        writeTomlBool(out, key, value);
    }
}

void writeTomlDouble(QTextStream& out, const QString& key, double value)
{
    if (std::isfinite(value)) {
        out << key << " = " << QString::number(value, 'g', 12) << '\n';
    }
}

LayoutConfig parseTomlEnvironment(const QString& path)
{
    LayoutConfig config;
    config.filePath = path;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return config;
    }

    struct PanelSlot {
        int column = 1;
        int row = 1;
        PlotSpec plot;
    };
    QVector<PanelSlot> panels;
    PanelSlot* currentPanel = nullptr;
    SignalSpec* currentSignal = nullptr;
    QString section;

    while (!file.atEnd()) {
        const QString line = stripTomlComment(QString::fromUtf8(file.readLine()));
        if (line.isEmpty()) {
            continue;
        }
        if (line == "[[panels]]") {
            PanelSlot panel;
            panels.push_back(std::move(panel));
            currentPanel = &panels.back();
            currentSignal = nullptr;
            section = "panel";
            continue;
        }
        if (line == "[[panels.signals]]") {
            if (!currentPanel) {
                continue;
            }
            SignalSpec sig;
            currentPanel->plot.signalSpecs.push_back(sig);
            currentSignal = &currentPanel->plot.signalSpecs.back();
            section = "signal";
            continue;
        }

        const int eq = line.indexOf('=');
        if (eq < 0) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();

        if (section == "panel" && currentPanel) {
            PlotSpec& plot = currentPanel->plot;
            if (key == "column") currentPanel->column = std::max(1, tomlInt(value, 1));
            else if (key == "row") currentPanel->row = std::max(1, tomlInt(value, 1));
            else if (key == "shot") plot.shot = tomlUnescape(value);
            else if (key == "title") plot.title = tomlUnescape(value);
            else if (key == "x_label") plot.xLabel = tomlUnescape(value);
            else if (key == "y_label") plot.yLabel = tomlUnescape(value);
            else if (key == "extraction_points") plot.extractionPoints = tomlInt(value, 2000);
            else if (key == "grid") plot.grid = tomlBool(value, true);
            else if (key == "custom_x_range") plot.customXRange = tomlBool(value, false);
            else if (key == "custom_y_range") plot.customYRange = tomlBool(value, false);
            else if (key == "xmin") plot.xmin = tomlDouble(value);
            else if (key == "xmax") plot.xmax = tomlDouble(value);
            else if (key == "ymin") plot.ymin = tomlDouble(value);
            else if (key == "ymax") plot.ymax = tomlDouble(value);
        } else if (section == "signal" && currentSignal) {
            if (key == "shot") currentSignal->shot = tomlUnescape(value);
            else if (key == "tree") currentSignal->experiment = tomlUnescape(value);
            else if (key == "server") currentSignal->serverIp = tomlUnescape(value);
            else if (key == "y") currentSignal->yExpr = tomlUnescape(value);
            else if (key == "x") currentSignal->xExpr = tomlUnescape(value);
            else if (key == "color") currentSignal->colorName = tomlUnescape(value);
            else if (key == "manual_color") currentSignal->manualColor = tomlBool(value, false);
            else if (key == "hidden") currentSignal->hidden = tomlBool(value, false);
            else if (key == "full") currentSignal->readMode = tomlBool(value, false) ? DataReadMode::Full : DataReadMode::Thin;
            else if (key == "read_mode") {
                QString mode = tomlUnescape(value).toLower();
                if (mode == "full") currentSignal->readMode = DataReadMode::Full;
                else if (mode == "medium") currentSignal->readMode = DataReadMode::Medium;
                else currentSignal->readMode = DataReadMode::Thin;
            }
        }
    }

    int maxColumn = 0;
    for (const PanelSlot& panel : panels) {
        maxColumn = std::max(maxColumn, panel.column);
    }
    config.columns.resize(std::max(1, maxColumn));
    std::sort(panels.begin(), panels.end(), [](const PanelSlot& a, const PanelSlot& b) {
        if (a.column != b.column) {
            return a.column < b.column;
        }
        return a.row < b.row;
    });
    for (PanelSlot& panel : panels) {
        normalizePresetColors(panel.plot.signalSpecs);
        config.columns[panel.column - 1].push_back(std::move(panel.plot));
    }
    return config;
}

LayoutConfig parseWebscpEnvironment(const QString& path)
{
    const auto map = readKeyValueFile(path);
    LayoutConfig config;
    config.filePath = path;
    const int cols = std::max(1, parseInt(map, "cols", 1));
    config.columns.resize(cols);

    for (int c = 1; c <= cols; ++c) {
        const int rows = std::max(0, parseInt(map, QString::number(c) + ".rows", 0));
        config.columns[c - 1].reserve(rows);
        for (int r = 1; r <= rows; ++r) {
            const QString prefix = QString("%1_%2.").arg(c).arg(r);
            PlotSpec plot;
            plot.shot = trimQuotes(map.value(prefix + "shot_txt"));
            plot.title = trimQuotes(map.value(prefix + "title"));
            plot.xLabel = trimQuotes(map.value(prefix + "xlabel"));
            plot.yLabel = trimQuotes(map.value(prefix + "ylabel"));
            plot.extractionPoints = parseInt(map, prefix + "extraction_points", parseInt(map, "Extraction_points", 2000));
            plot.grid = parseInt(map, prefix + "grid_mode", parseInt(map, "Grid_Mode", 1)) != 0;
            plot.customXRange = parseInt(map, prefix + "xseting_mode", 1) == 0;
            plot.customYRange = parseInt(map, prefix + "yseting_mode", 1) == 0;
            plot.xmin = plot.customXRange ? parseDouble(map.value(prefix + "xmin_custom", map.value("xmin"))) : qQNaN();
            plot.xmax = plot.customXRange ? parseDouble(map.value(prefix + "xmax_custom", map.value("xmax"))) : qQNaN();
            plot.ymin = plot.customYRange ? parseDouble(map.value(prefix + "ymin_custom", map.value("ymin"))) : qQNaN();
            plot.ymax = plot.customYRange ? parseDouble(map.value(prefix + "ymax_custom", map.value("ymax"))) : qQNaN();

            const int signalCount = std::max(1, parseInt(map, prefix + "num_sig", 1));
            plot.signalSpecs.reserve(signalCount);
            for (int s = 1; s <= signalCount; ++s) {
                SignalSpec sig;
                sig.shot = trimQuotes(map.value(prefix + QString("shot_%1").arg(s)));
                sig.yExpr = trimQuotes(map.value(prefix + QString("y_expr_%1").arg(s)));
                sig.xExpr = trimQuotes(map.value(prefix + QString("x_expr_%1").arg(s)));
                sig.experiment = trimQuotes(map.value(prefix + QString("experiment_%1").arg(s)));
                sig.serverIp = trimQuotes(map.value(prefix + QString("server_ip_%1").arg(s)));
                const QString colorName = trimQuotes(map.value(prefix + QString("color_name_%1").arg(s)));
                sig.manualColor = parseInt(map, prefix + QString("color_manual_%1").arg(s), 0) != 0;
                if (sig.manualColor && !colorName.isEmpty()) {
                    sig.colorName = colorName;
                } else {
                    sig.colorName = colorForIndex(s - 1);
                    sig.manualColor = false;
                }
                if (!sig.yExpr.isEmpty()) {
                    plot.signalSpecs.push_back(sig);
                }
            }
            normalizePresetColors(plot.signalSpecs);
            config.columns[c - 1].push_back(plot);
        }
    }
    return config;
}

LayoutConfig parseEnvironment(const QString& path)
{
    if (path.endsWith(".toml", Qt::CaseInsensitive)) {
        return parseTomlEnvironment(path);
    }
    return parseWebscpEnvironment(path);
}

bool writeEnvironmentToml(const LayoutConfig& config, const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = "Cannot write " + path;
        }
        return false;
    }

    QTextStream out(&file);
    out << "version = 1\n\n";

    QHash<QString, int> shotCounts;
    for (const auto& column : config.columns) {
        for (const PlotSpec& plot : column) {
            const QString shot = plot.shot.trimmed();
            if (!shot.isEmpty()) {
                shotCounts[shot] += std::max(1, static_cast<int>(plot.signalSpecs.size()));
            }
        }
    }
    QString defaultShot;
    int defaultShotCount = 0;
    for (auto it = shotCounts.cbegin(); it != shotCounts.cend(); ++it) {
        if (it.value() > defaultShotCount) {
            defaultShot = it.key();
            defaultShotCount = it.value();
        }
    }

    for (int c = 0; c < config.columns.size(); ++c) {
        for (int r = 0; r < config.columns[c].size(); ++r) {
            const PlotSpec& plot = config.columns[c][r];
            const QString panelShot = plot.shot.trimmed();
            out << "[[panels]]\n";
            out << "column = " << c + 1 << '\n';
            out << "row = " << r + 1 << '\n';
            writeTomlStringIfNotEmpty(out, "title", plot.title);
            writeTomlStringIfNotEmpty(out, "x_label", plot.xLabel);
            writeTomlStringIfNotEmpty(out, "y_label", plot.yLabel);
            if (plot.extractionPoints != 2000) {
                out << "extraction_points = " << plot.extractionPoints << '\n';
            }
            writeTomlBoolIfNotDefault(out, "grid", plot.grid, true);
            writeTomlBoolIfNotDefault(out, "custom_x_range", plot.customXRange, false);
            writeTomlBoolIfNotDefault(out, "custom_y_range", plot.customYRange, false);
            writeTomlDouble(out, "xmin", plot.xmin);
            writeTomlDouble(out, "xmax", plot.xmax);
            writeTomlDouble(out, "ymin", plot.ymin);
            writeTomlDouble(out, "ymax", plot.ymax);
            out << '\n';

            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                const SignalSpec& sig = plot.signalSpecs[s];
                out << "[[panels.signals]]\n";
                QString signalShot = sig.shot.trimmed();
                if (signalShot.isEmpty() && !panelShot.isEmpty() && panelShot != defaultShot) {
                    signalShot = panelShot;
                }
                if (!signalShot.isEmpty() && signalShot != defaultShot) {
                    writeTomlString(out, "shot", signalShot);
                }
                writeTomlStringIfNotEmpty(out, "tree", sig.experiment);
                writeTomlStringIfNotEmpty(out, "server", sig.serverIp);
                writeTomlStringIfNotEmpty(out, "y", sig.yExpr);
                writeTomlStringIfNotEmpty(out, "x", sig.xExpr);
                if (sig.manualColor) {
                    writeTomlString(out, "color", sig.colorName.isEmpty() ? colorForIndex(s) : sig.colorName);
                    writeTomlBool(out, "manual_color", true);
                }
                if (sig.hidden) {
                    writeTomlBool(out, "hidden", true);
                }
                if (sig.readMode == DataReadMode::Full) {
                    writeTomlString(out, "read_mode", "full");
                } else if (sig.readMode == DataReadMode::Medium) {
                    writeTomlString(out, "read_mode", "medium");
                }
                out << '\n';
            }
        }
    }
    return true;
}

