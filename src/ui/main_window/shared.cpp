// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shared.h"

void setLabelTextIfChanged(QLabel* label, const QString& text)
{
    if (label && label->text() != text) {
        label->setText(text);
    }
}

void clearCustomRanges(PlotSpec* plot)
{
    if (!plot) {
        return;
    }
    plot->customXRange = false;
    plot->customYRange = false;
    plot->xmin = qQNaN();
    plot->xmax = qQNaN();
    plot->ymin = qQNaN();
    plot->ymax = qQNaN();
}

bool loadedSignalMatchesConfig(const LayoutConfig& config, const LoadedSignal& item)
{
    if (item.column < 0 || item.row < 0 || item.signal < 0
        || item.column >= config.columns.size()
        || item.row >= config.columns[item.column].size()
        || item.signal >= config.columns[item.column][item.row].signalSpecs.size()) {
        return false;
    }
    const PlotSpec& plot = config.columns[item.column][item.row];
    const SignalSpec& sig = plot.signalSpecs[item.signal];
    if (sig.hidden) {
        return false;
    }
    if (item.shot != effectiveSignalShot(plot, sig)) {
        return false;
    }
    const QString expectedName = normalizedMdsSignal(sig.yExpr);
    return item.series.name.isEmpty() || item.series.name == expectedName;
}

QString signalRefreshSignature(const SignalSpec& sig)
{
    return QStringList{
        sig.shot,
        sig.yExpr,
        sig.xExpr,
        sig.experiment,
        sig.serverIp,
        sig.colorName,
        sig.manualColor ? QStringLiteral("manual-color") : QStringLiteral("auto-color"),
        sig.hidden ? QStringLiteral("hidden") : QStringLiteral("shown"),
        QString::number(static_cast<int>(sig.readMode)),
    }.join(QChar(0x1f));
}

QString plotRefreshSignature(const PlotSpec& plot)
{
    QStringList parts{
        plot.shot,
        plot.title,
        plot.xLabel,
        plot.yLabel,
        QString::number(plot.extractionPoints),
        plot.grid ? QStringLiteral("grid") : QStringLiteral("no-grid"),
        plot.customXRange ? QStringLiteral("custom-x") : QStringLiteral("auto-x"),
        plot.customYRange ? QStringLiteral("custom-y") : QStringLiteral("auto-y"),
        QString::number(plot.xmin, 'g', 17),
        QString::number(plot.xmax, 'g', 17),
        QString::number(plot.ymin, 'g', 17),
        QString::number(plot.ymax, 'g', 17),
    };
    for (const SignalSpec& sig : plot.signalSpecs) {
        parts.push_back(signalRefreshSignature(sig));
    }
    return parts.join(QChar(0x1e));
}

QString layoutRefreshSignature(const LayoutConfig& config)
{
    QStringList parts{config.filePath};
    for (int c = 0; c < config.columns.size(); ++c) {
        parts.push_back(QStringLiteral("col:%1").arg(c));
        for (int r = 0; r < config.columns[c].size(); ++r) {
            parts.push_back(QStringLiteral("row:%1").arg(r));
            parts.push_back(plotRefreshSignature(config.columns[c][r]));
        }
    }
    return parts.join(QChar(0x1d));
}

bool signalDataSourceEqual(const SignalSpec& lhs, const SignalSpec& rhs)
{
    return lhs.shot == rhs.shot
           && lhs.yExpr == rhs.yExpr
           && lhs.xExpr == rhs.xExpr
           && lhs.experiment == rhs.experiment
           && lhs.serverIp == rhs.serverIp
           && lhs.hidden == rhs.hidden
           && lhs.readMode == rhs.readMode;
}

bool signalDataSourcesEqual(const QVector<SignalSpec>& lhs, const QVector<SignalSpec>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (int i = 0; i < lhs.size(); ++i) {
        if (!signalDataSourceEqual(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

bool signalSpecEqual(const SignalSpec& lhs, const SignalSpec& rhs)
{
    return signalDataSourceEqual(lhs, rhs)
           && lhs.colorName == rhs.colorName
           && lhs.manualColor == rhs.manualColor
           && lhs.hidden == rhs.hidden;
}

bool signalSpecsEqual(const QVector<SignalSpec>& lhs, const QVector<SignalSpec>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (int i = 0; i < lhs.size(); ++i) {
        if (!signalSpecEqual(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

bool optionalDoubleFromText(const QString& text, double* value)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        *value = qQNaN();
        return true;
    }
    bool ok = false;
    const double parsed = trimmed.toDouble(&ok);
    if (ok) {
        *value = parsed;
    }
    return ok;
}

QString exportFileToken(QString text)
{
    text = text.trimmed();
    if (text.isEmpty()) {
        text = QStringLiteral("unknown");
    }
    text.replace('\\', "");
    text.replace('/', "_");
    text.replace(':', "_");
    text.replace('*', "_");
    text.replace('?', "_");
    text.replace('"', "_");
    text.replace('<', "_");
    text.replace('>', "_");
    text.replace('|', "_");
    text.replace(QRegularExpression("\\s+"), "_");
    return text;
}

QString exportFormatExtension(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Csv:
        return QStringLiteral("csv");
    case ExportFormat::Tsv:
        return QStringLiteral("tsv");
    case ExportFormat::Json:
        return QStringLiteral("json");
    case ExportFormat::Text:
    default:
        return QStringLiteral("txt");
    }
}

QString exportFormatName(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Csv:
        return QStringLiteral("CSV");
    case ExportFormat::Tsv:
        return QStringLiteral("TSV");
    case ExportFormat::Json:
        return QStringLiteral("JSON");
    case ExportFormat::Text:
    default:
        return QStringLiteral("Text");
    }
}

QString exportFormatSettingValue(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Csv:
        return QStringLiteral("csv");
    case ExportFormat::Tsv:
        return QStringLiteral("tsv");
    case ExportFormat::Json:
        return QStringLiteral("json");
    case ExportFormat::Text:
    default:
        return QStringLiteral("text");
    }
}

ExportFormat exportFormatFromSetting(QString value)
{
    value = value.trimmed().toLower();
    if (value == QStringLiteral("csv")) {
        return ExportFormat::Csv;
    }
    if (value == QStringLiteral("tsv")) {
        return ExportFormat::Tsv;
    }
    if (value == QStringLiteral("json")) {
        return ExportFormat::Json;
    }
    return ExportFormat::Text;
}

QString uniqueExportPath(const QDir& dir, const QString& baseName, ExportFormat format)
{
    const QString extension = exportFormatExtension(format);
    QString path = dir.filePath(baseName + "." + extension);
    if (!QFileInfo::exists(path)) {
        return path;
    }
    for (int i = 2; i < 10000; ++i) {
        path = dir.filePath(QString("%1_%2.%3").arg(baseName).arg(i).arg(extension));
        if (!QFileInfo::exists(path)) {
            return path;
        }
    }
    return dir.filePath(baseName + "_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "." + extension);
}

QString exportRangeFileSuffix(bool useXRange, double xmin, double xmax)
{
    if (!useXRange) {
        return {};
    }
    return QStringLiteral("x_%1_%2")
        .arg(exportFileToken(QString::number(xmin, 'g', 12)),
             exportFileToken(QString::number(xmax, 'g', 12)));
}

QString jsonString(const QString& value)
{
    const QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

bool pointInExportRange(double x, bool useXRange, double xmin, double xmax)
{
    return !useXRange || (x >= xmin && x <= xmax);
}

bool writeSeriesDataFile(const QString& path,
                         const SignalSeries& series,
                         ExportFormat format,
                         bool useXRange,
                         double xmin,
                         double xmax,
                         QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = "Cannot write " + path;
        }
        return false;
    }
    QTextStream out(&file);
    if (format == ExportFormat::Json) {
        out << "{\n";
        if (!series.error.isEmpty()) {
            out << "  \"error\": " << jsonString(series.error) << ",\n";
        }
        out << "  \"points\": [\n";
        bool first = true;
        auto writeJsonPoint = [&out, &first](double x, double y) {
            if (!first) {
                out << ",\n";
            }
            first = false;
            out << "    [" << QString::number(x, 'g', 17) << ", " << QString::number(y, 'g', 17) << "]";
        };
        if (series.hasUniformData()) {
            for (int i = 0; i < series.uniformY.size(); ++i) {
                const double x = series.uniformStart + static_cast<double>(i) * series.uniformStep;
                if (pointInExportRange(x, useXRange, xmin, xmax)) {
                    writeJsonPoint(x, series.uniformY[i]);
                }
            }
        } else {
            for (const QPointF& point : series.points) {
                if (pointInExportRange(point.x(), useXRange, xmin, xmax)) {
                    writeJsonPoint(point.x(), point.y());
                }
            }
        }
        out << "\n  ]\n}\n";
        return true;
    }

    if (format == ExportFormat::Csv) {
        out << "x,y\n";
    } else if (format == ExportFormat::Tsv) {
        out << "x\ty\n";
    } else {
        out << "# x y\n";
    }
    if (!series.error.isEmpty()) {
        if (format == ExportFormat::Csv) {
            out << "# error," << series.error << '\n';
        } else if (format == ExportFormat::Tsv) {
            out << "# error\t" << series.error << '\n';
        } else {
            out << "# error: " << series.error << '\n';
        }
    }
    auto writePoint = [&out, format](double x, double y) {
        if (format == ExportFormat::Csv) {
            out << QString::number(x, 'g', 17) << ',' << QString::number(y, 'g', 17) << '\n';
        } else if (format == ExportFormat::Tsv) {
            out << QString::number(x, 'g', 17) << '\t' << QString::number(y, 'g', 17) << '\n';
        } else {
            out << QString::number(x, 'g', 17) << ' ' << QString::number(y, 'g', 17) << '\n';
        }
    };
    if (series.hasUniformData()) {
        for (int i = 0; i < series.uniformY.size(); ++i) {
            const double x = series.uniformStart + static_cast<double>(i) * series.uniformStep;
            if (pointInExportRange(x, useXRange, xmin, xmax)) {
                writePoint(x, series.uniformY[i]);
            }
        }
    } else {
        for (const QPointF& point : series.points) {
            if (pointInExportRange(point.x(), useXRange, xmin, xmax)) {
                writePoint(point.x(), point.y());
            }
        }
    }
    return true;
}

QIcon infoIcon()
{
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    const QColor fill = Qt::transparent;
    const QColor border = dark ? QColor("#cbd5e1") : QColor("#475569");
    const QColor glyph = dark ? QColor("#cbd5e1") : QColor("#475569");
    const QColor highlight = dark ? QColor("#e5e7eb") : QColor("#334155");

    painter.setPen(QPen(border, 1.8));
    painter.setBrush(fill);
    painter.drawEllipse(QRectF(1.9, 1.9, 24.2, 24.2));

    painter.setPen(Qt::NoPen);
    painter.setBrush(highlight);
    painter.drawEllipse(QPointF(14.0, 8.3), 1.9, 1.9);

    painter.setBrush(glyph);
    painter.drawRoundedRect(QRectF(12.45, 11.6, 3.1, 10.4), 1.55, 1.55);
    return QIcon(pixmap);
}

QIcon recentArrowIcon()
{
    QPixmap pixmap(12, 30);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QApplication::palette().color(QPalette::ButtonText));
    painter.drawPolygon(QPolygonF{
        QPointF(1.5, 12.5),
        QPointF(10.5, 12.5),
        QPointF(6.0, 18.0),
    });
    return QIcon(pixmap);
}

