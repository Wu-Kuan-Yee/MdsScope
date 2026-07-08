// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.h"
#include "mds_client.h"
#include "point_overlay.h"

namespace {
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

QString themeModeLabel(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Light:
        return QStringLiteral("Light");
    case ThemeMode::Dark:
        return QStringLiteral("Dark");
    case ThemeMode::Auto:
    default:
        return QStringLiteral("Auto");
    }
}

class ThemeModeButton final : public QWidget {
public:
    explicit ThemeModeButton(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("themeModeButton");
        setFixedSize(92, 34);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
        animation_.setDuration(150);
        animation_.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            thumbPosition_ = value.toReal();
            update();
        });
        setMode(mdsScopeThemeMode(), false);
    }

    void setMode(ThemeMode mode, bool animated = true)
    {
        if (mode_ == mode && animated) {
            return;
        }
        mode_ = mode;
        setToolTip(QStringLiteral("Theme: %1").arg(themeModeLabel(mode)));
        const qreal target = positionForMode(mode);
        animation_.stop();
        if (animated) {
            animation_.setStartValue(thumbPosition_);
            animation_.setEndValue(target);
            animation_.start();
        } else {
            thumbPosition_ = target;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QPalette pal = palette();
        const QColor track = pal.color(QPalette::AlternateBase);
        const QColor border = pal.color(QPalette::Mid);
        const QColor quiet = pal.color(QPalette::Disabled, QPalette::WindowText);
        const QColor sunActive("#f59e0b");
        const QColor autoActive("#22c55e");
        const QColor moonActive("#60a5fa");
        const QColor thumb = pal.color(QPalette::Button);

        const QRectF outer = rect().adjusted(1, 2, -1, -2);
        const qreal radius = outer.height() / 2.0;
        painter.setPen(QPen(border, 1));
        painter.setBrush(track);
        painter.drawRoundedRect(outer, radius, radius);

        const qreal segmentWidth = outer.width() / 3.0;
        const qreal knobSize = outer.height() - 4;
        const qreal knobX = outer.left() + thumbPosition_ * segmentWidth + (segmentWidth - knobSize) / 2.0;
        const QRectF knob(knobX, outer.top() + 2, knobSize, knobSize);
        painter.setPen(QPen(border, 1));
        painter.setBrush(thumb);
        painter.drawEllipse(knob);

        const QPointF lightCenter(outer.left() + segmentWidth * 0.5, outer.center().y());
        const QPointF autoCenter(outer.left() + segmentWidth * 1.5, outer.center().y());
        const QPointF darkCenter(outer.left() + segmentWidth * 2.5, outer.center().y());
        drawSun(&painter, lightCenter, mode_ == ThemeMode::Light ? sunActive : quiet, mode_ == ThemeMode::Light);
        drawSystemIcon(&painter, autoCenter, mode_ == ThemeMode::Auto ? autoActive : quiet);
        drawMoon(&painter, darkCenter, mode_ == ThemeMode::Dark ? moonActive : quiet);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        dragging_ = true;
        applyModeForX(event->position().x());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (dragging_) {
            applyModeForX(event->position().x());
            event->accept();
            return;
        }
        updateTooltipForX(event->position().x());
        QWidget::mouseMoveEvent(event);
    }

    void enterEvent(QEnterEvent* event) override
    {
        updateTooltipForX(event->position().x());
        QWidget::enterEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            applyModeForX(event->position().x());
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void changeEvent(QEvent* event) override
    {
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange) {
            update();
        }
        QWidget::changeEvent(event);
    }

private:
    static qreal positionForMode(ThemeMode mode)
    {
        switch (mode) {
        case ThemeMode::Light:
            return 0.0;
        case ThemeMode::Dark:
            return 2.0;
        case ThemeMode::Auto:
        default:
            return 1.0;
        }
    }

    static ThemeMode modeForX(qreal x, qreal width)
    {
        if (x < width / 3.0) {
            return ThemeMode::Light;
        }
        if (x > width * 2.0 / 3.0) {
            return ThemeMode::Dark;
        }
        return ThemeMode::Auto;
    }

    void applyModeForX(qreal x)
    {
        const ThemeMode mode = modeForX(x, width());
        if (mode == mode_) {
            return;
        }
        setMdsScopeThemeMode(mode);
        setMode(mode);
    }

    void updateTooltipForX(qreal x)
    {
        const ThemeMode hoverMode = modeForX(x, width());
        setToolTip(QStringLiteral("Theme: %1").arg(themeModeLabel(hoverMode)));
    }

    static void drawSun(QPainter* painter, const QPointF& center, const QColor& color, bool filled)
    {
        painter->save();
        painter->setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap));
        painter->setBrush(filled ? color : Qt::NoBrush);
        painter->drawEllipse(center, 4.6, 4.6);
        constexpr qreal pi = 3.14159265358979323846;
        for (int i = 0; i < 8; ++i) {
            const qreal angle = (pi / 4.0) * i;
            const QPointF inner(center.x() + std::cos(angle) * 7.8, center.y() + std::sin(angle) * 7.8);
            const QPointF outer(center.x() + std::cos(angle) * 10.4, center.y() + std::sin(angle) * 10.4);
            painter->drawLine(inner, outer);
        }
        painter->restore();
    }

    static void drawMoon(QPainter* painter, const QPointF& center, const QColor& color)
    {
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        QPainterPath moon;
        moon.moveTo(center.x() + 3.5, center.y() - 8.7);
        moon.cubicTo(center.x() - 5.8, center.y() - 6.0,
                     center.x() - 6.3, center.y() + 6.1,
                     center.x() + 3.4, center.y() + 8.7);
        moon.cubicTo(center.x() - 0.9, center.y() + 4.7,
                     center.x() - 0.9, center.y() - 4.7,
                     center.x() + 3.5, center.y() - 8.7);
        painter->drawPath(moon);
        painter->restore();
    }

    static void drawSystemIcon(QPainter* painter, const QPointF& center, const QColor& color)
    {
        painter->save();
        painter->setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(center.x() - 8.0, center.y() - 6.8, 16.0, 10.8), 2.0, 2.0);
        painter->drawLine(QPointF(center.x(), center.y() + 4.2), QPointF(center.x(), center.y() + 7.6));
        painter->drawLine(QPointF(center.x() - 5.2, center.y() + 7.6), QPointF(center.x() + 5.2, center.y() + 7.6));
        painter->restore();
    }

    QVariantAnimation animation_;
    ThemeMode mode_ = ThemeMode::Auto;
    qreal thumbPosition_ = 0.5;
    bool dragging_ = false;
};

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

class SignalDialog final : public QDialog {
public:
    explicit SignalDialog(const PlotSpec& base, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Signal");
        auto* layout = new QFormLayout(this);
        shot_ = new QLineEdit(base.shot, this);
        y_ = new QLineEdit(this);
        x_ = new QLineEdit(this);
        experiment_ = new QLineEdit(this);
        server_ = new QLineEdit(this);
        if (!base.signalSpecs.isEmpty()) {
            experiment_->setText(base.signalSpecs.front().experiment);
            server_->setText(base.signalSpecs.front().serverIp);
        }
        layout->addRow("Shot", shot_);
        layout->addRow("Y expr", y_);
        layout->addRow("X expr", x_);
        layout->addRow("Tree", experiment_);
        layout->addRow("Server", server_);

        auto* buttons = new QHBoxLayout;
        auto* ok = new QPushButton("OK", this);
        auto* cancel = new QPushButton("Cancel", this);
        buttons->addStretch();
        buttons->addWidget(ok);
        buttons->addWidget(cancel);
        layout->addRow(buttons);
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QString shot() const { return shot_->text().trimmed(); }
    SignalSpec signal() const
    {
        SignalSpec sig;
        sig.yExpr = y_->text().trimmed();
        sig.xExpr = x_->text().trimmed();
        sig.experiment = experiment_->text().trimmed();
        sig.serverIp = server_->text().trimmed();
        sig.colorName = colorForIndex(0);
        return sig;
    }

private:
    QLineEdit* shot_ = nullptr;
    QLineEdit* y_ = nullptr;
    QLineEdit* x_ = nullptr;
    QLineEdit* experiment_ = nullptr;
    QLineEdit* server_ = nullptr;
};

class DataSourceDialog final : public QDialog {
public:
    explicit DataSourceDialog(const PlotSpec& base,
                              const QString& currentShot,
                              const QString& sourceIndexDir,
                              QWidget* parent = nullptr)
        : QDialog(parent)
        , defaultShot_(currentShot.trimmed().isEmpty() ? base.shot.trimmed() : currentShot.trimmed())
        , sourceIndexDir_(sourceIndexDir)
    {
        setWindowTitle("Data Source Setup");
        treeNames_ = readSourceIndexLines(QDir(sourceIndexDir_).filePath("trees.txt"));
        auto* mainLayout = new QVBoxLayout(this);

        rowsHost_ = new QWidget(this);
        rowsLayout_ = new QGridLayout(rowsHost_);
        rowsLayout_->setContentsMargins(0, 0, 0, 0);
        rowsLayout_->setHorizontalSpacing(6);
        rowsLayout_->setVerticalSpacing(4);
        rowsLayout_->addWidget(new QLabel("Shot", rowsHost_), 0, 0);
        rowsLayout_->addWidget(new QLabel("Tree", rowsHost_), 0, 1);
        rowsLayout_->addWidget(new QLabel("Signal", rowsHost_), 0, 2);
        rowsLayout_->addWidget(new QLabel("Server IP", rowsHost_), 0, 3);
        rowsLayout_->addWidget(new QLabel("Color", rowsHost_), 0, 4);
        rowsLayout_->addWidget(new QLabel("Hide", rowsHost_), 0, 5);
        rowsLayout_->addWidget(new QLabel("Data", rowsHost_), 0, 6);
        rowsLayout_->addWidget(new QLabel("", rowsHost_), 0, 7);
        mainLayout->addWidget(rowsHost_);

        QString defaultTree;
        QString defaultServer;
        if (!base.signalSpecs.isEmpty()) {
            defaultTree = base.signalSpecs.front().experiment;
            defaultServer = base.signalSpecs.front().serverIp;
            for (int i = 0; i < base.signalSpecs.size(); ++i) {
                addRow(base.signalSpecs[i], i);
            }
        } else {
            SignalSpec sig;
            sig.experiment = defaultTree;
            sig.serverIp = defaultServer;
            sig.colorName = colorForIndex(0);
            addRow(sig, 0);
        }

        auto* commandLayout = new QHBoxLayout();
        auto* addCurve = new QPushButton("Add Curve", this);
        auto* ok = new QPushButton("OK", this);
        auto* cancel = new QPushButton("Cancel", this);
        commandLayout->addWidget(addCurve);
        commandLayout->addStretch(1);
        commandLayout->addWidget(ok);
        commandLayout->addWidget(cancel);
        mainLayout->addLayout(commandLayout);

        connect(addCurve, &QPushButton::clicked, this, [this] {
            SignalSpec sig;
            int activeCount = 0;
            bool copiedDefaults = false;
            if (!rows_.isEmpty()) {
                for (const Row* row : std::as_const(rows_)) {
                    if (!row->deleted) {
                        ++activeCount;
                        if (!copiedDefaults) {
                            sig.serverIp = row->server->text().trimmed();
                            copiedDefaults = true;
                        }
                    }
                }
            }
            sig.colorName = colorForIndex(activeCount);
            addRow(sig, activeCount);
        });
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QVector<SignalSpec> signalSpecs() const
    {
        QVector<SignalSpec> out;
        out.reserve(rows_.size());
        for (const Row* row : rows_) {
            if (row->deleted) {
                continue;
            }
            SignalSpec sig;
            sig.yExpr = row->signal->text().trimmed();
            if (sig.yExpr.isEmpty()) {
                continue;
            }
            const QString shot = row->shot->text().trimmed();
            sig.shot = shotCoveredByDefault(shot) ? QString() : shot;
            sig.xExpr = row->xExpr;
            sig.experiment = row->tree->text().trimmed();
            sig.serverIp = row->server->text().trimmed();
            sig.colorName = row->color.name();
            sig.manualColor = row->manualColor;
            sig.hidden = row->hidden->isChecked();
            sig.readMode = static_cast<DataReadMode>(row->dataMode->currentData().toInt());
            out.push_back(std::move(sig));
        }
        return out;
    }

private:
    struct Row {
        QLineEdit* shot = nullptr;
        QLineEdit* tree = nullptr;
        QLineEdit* signal = nullptr;
        QLineEdit* server = nullptr;
        QStringListModel* treeModel = nullptr;
        QCompleter* treeCompleter = nullptr;
        QStringListModel* signalModel = nullptr;
        QCompleter* signalCompleter = nullptr;
        QMenu* reverseTreeMenu = nullptr;
        QPushButton* colorButton = nullptr;
        QCheckBox* hidden = nullptr;
        QComboBox* dataMode = nullptr;
        QPushButton* deleteButton = nullptr;
        QColor color;
        QString xExpr;
        QString autoTree;
        bool deleted = false;
        bool manualColor = false;
        bool updatingTreeFromAutoMatch = false;
    };

    void addRow(const SignalSpec& sig, int colorIndex)
    {
        auto* row = new Row;
        row->shot = new QLineEdit(sig.shot.trimmed().isEmpty() ? defaultShot_ : sig.shot.trimmed(), rowsHost_);
        row->tree = new QLineEdit(sig.experiment, rowsHost_);
        row->signal = new QLineEdit(sig.yExpr, rowsHost_);
        row->server = new QLineEdit(sig.serverIp, rowsHost_);
        row->treeModel = new QStringListModel(row->tree);
        row->treeCompleter = makeCompleter(row->treeModel, row->tree);
        row->signalModel = new QStringListModel(row->signal);
        row->signalCompleter = makeCompleter(row->signalModel, row->signal);
        row->reverseTreeMenu = new QMenu(row->tree);
        row->colorButton = new QPushButton(rowsHost_);
        row->hidden = new QCheckBox(rowsHost_);
        row->dataMode = new QComboBox(rowsHost_);
        row->deleteButton = new QPushButton("Delete", rowsHost_);
        row->manualColor = sig.manualColor;
        row->color = QColor(row->manualColor && !sig.colorName.isEmpty() ? sig.colorName : colorForIndex(colorIndex));
        row->xExpr = sig.xExpr;
        row->hidden->setChecked(sig.hidden);
        row->dataMode->addItem("Thin", static_cast<int>(DataReadMode::Thin));
        row->dataMode->addItem("Medium", static_cast<int>(DataReadMode::Medium));
        row->dataMode->addItem("Full", static_cast<int>(DataReadMode::Full));
        row->dataMode->setCurrentIndex(row->dataMode->findData(static_cast<int>(sig.readMode)));
        row->shot->setMinimumWidth(72);
        row->signal->setMinimumWidth(150);
        row->server->setMinimumWidth(120);
        row->dataMode->setMinimumWidth(72);
        row->tree->setCompleter(row->treeCompleter);
        row->signal->setCompleter(row->signalCompleter);
        updateTreeCompleter(row, false);
        updateSignalCompleter(row);
        updateColorButton(row);

        const int gridRow = rows_.size() + 1;
        rowsLayout_->addWidget(row->shot, gridRow, 0);
        rowsLayout_->addWidget(row->tree, gridRow, 1);
        rowsLayout_->addWidget(row->signal, gridRow, 2);
        rowsLayout_->addWidget(row->server, gridRow, 3);
        rowsLayout_->addWidget(row->colorButton, gridRow, 4);
        rowsLayout_->addWidget(row->hidden, gridRow, 5, Qt::AlignCenter);
        rowsLayout_->addWidget(row->dataMode, gridRow, 6);
        rowsLayout_->addWidget(row->deleteButton, gridRow, 7);
        rows_.push_back(row);

        connect(row->colorButton, &QPushButton::clicked, this, [this, row] {
            const QColor chosen = QColorDialog::getColor(row->color, this, "Curve Color");
            if (chosen.isValid()) {
                row->color = chosen;
                row->manualColor = true;
                updateColorButton(row);
            }
        });
        connect(row->tree, &QLineEdit::textChanged, this, [this, row] {
            if (!row->updatingTreeFromAutoMatch) {
                row->autoTree.clear();
            }
            updateSignalCompleter(row);
        });
        connect(row->signal, &QLineEdit::textChanged, this, [this, row] {
            updateTreeCompleter(row, true);
        });
        connect(row->deleteButton, &QPushButton::clicked, this, [row] {
            row->deleted = true;
            for (QWidget* widget : {static_cast<QWidget*>(row->shot),
                                    static_cast<QWidget*>(row->tree),
                                    static_cast<QWidget*>(row->signal),
                                    static_cast<QWidget*>(row->server),
                                    static_cast<QWidget*>(row->colorButton),
                                    static_cast<QWidget*>(row->hidden),
                                    static_cast<QWidget*>(row->dataMode),
                                    static_cast<QWidget*>(row->deleteButton)}) {
                widget->hide();
            }
        });
    }

    bool shotCoveredByDefault(const QString& shot) const
    {
        const QString trimmedShot = shot.trimmed();
        const QString trimmedDefault = defaultShot_.trimmed();
        if (trimmedShot.isEmpty() || trimmedShot == trimmedDefault) {
            return true;
        }

        const QStringList rowShots = expandedShotList(trimmedShot);
        const QStringList defaultShots = expandedShotList(trimmedDefault);
        if (rowShots.isEmpty() || defaultShots.isEmpty()) {
            return false;
        }

        const QSet<QString> defaultSet(defaultShots.cbegin(), defaultShots.cend());
        for (const QString& rowShot : rowShots) {
            if (!defaultSet.contains(rowShot)) {
                return false;
            }
        }
        return true;
    }

    static QCompleter* makeCompleter(const QStringList& values, QObject* parent)
    {
        return makeCompleter(new QStringListModel(values, parent), parent);
    }

    static QCompleter* makeCompleter(QStringListModel* model, QObject* parent)
    {
        auto* completer = new QCompleter(model, parent);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        completer->setMaxVisibleItems(16);
        QAbstractItemView* popup = completer->popup();
        popup->setMouseTracking(true);
        popup->viewport()->setMouseTracking(true);
        popup->setAttribute(Qt::WA_Hover, true);
        popup->viewport()->setAttribute(Qt::WA_Hover, true);
        popup->setSelectionBehavior(QAbstractItemView::SelectRows);
        popup->setStyleSheet(
            "QAbstractItemView { outline: 0; }"
            "QAbstractItemView::item { padding: 2px 6px; }"
            "QAbstractItemView::item:hover, QAbstractItemView::item:selected {"
            " background: palette(highlight); color: palette(highlighted-text);"
            "}");
        return completer;
    }

    static QString sourceIndexFileName(QString tree)
    {
        tree = tree.trimmed().toLower();
        tree.replace(QRegularExpression("[^a-z0-9_-]+"), "_");
        return tree.isEmpty() ? QString() : tree + QStringLiteral(".txt");
    }

    static QStringList readSourceIndexLines(const QString& path)
    {
        QStringList values;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return values;
        }
        QSet<QString> seen;
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            const QString key = line.toLower();
            if (!line.isEmpty() && !seen.contains(key)) {
                values.push_back(line);
                seen.insert(key);
            }
        }
        values.sort(Qt::CaseInsensitive);
        return values;
    }

    QStringList signalNamesForTree(const QString& tree)
    {
        const QString resolvedTree = exactTreeName(tree);
        const QString key = resolvedTree.trimmed().toLower();
        if (key.isEmpty()) {
            return allSignalNames();
        }
        if (!signalCache_.contains(key)) {
            const QString path = QDir(QDir(sourceIndexDir_).filePath("signals")).filePath(sourceIndexFileName(resolvedTree));
            signalCache_.insert(key, readSourceIndexLines(path));
        }
        return signalCache_.value(key);
    }

    QString exactTreeName(const QString& tree) const
    {
        const QString key = tree.trimmed();
        if (key.isEmpty()) {
            return {};
        }
        for (const QString& candidate : treeNames_) {
            if (candidate.compare(key, Qt::CaseInsensitive) == 0) {
                return candidate;
            }
        }
        return {};
    }

    void ensureGlobalSignalIndex()
    {
        if (globalSignalIndexLoaded_) {
            return;
        }
        globalSignalIndexLoaded_ = true;

        QSet<QString> seenSignals;
        for (const QString& tree : std::as_const(treeNames_)) {
            const QString treeKey = tree.trimmed().toLower();
            if (treeKey.isEmpty()) {
                continue;
            }

            const QStringList signalNames = signalNamesForTree(tree);
            for (const QString& signalName : signalNames) {
                const QString signalKey = signalIndexKey(signalName);
                if (signalKey.isEmpty()) {
                    continue;
                }
                signalToTrees_[signalKey].insert(tree);
                if (!seenSignals.contains(signalKey)) {
                    globalSignals_.push_back(signalName);
                    seenSignals.insert(signalKey);
                }
            }
        }
        globalSignals_.sort(Qt::CaseInsensitive);
    }

    QStringList allSignalNames()
    {
        ensureGlobalSignalIndex();
        return globalSignals_;
    }

    QStringList treeNamesForSignal(const QString& signal)
    {
        const QString filter = signalIndexKey(signal);
        if (filter.isEmpty()) {
            return treeNames_;
        }

        ensureGlobalSignalIndex();
        const auto exactTrees = signalToTrees_.constFind(filter);
        if (exactTrees != signalToTrees_.cend()) {
            QStringList trees(exactTrees.value().cbegin(), exactTrees.value().cend());
            trees.sort(Qt::CaseInsensitive);
            return trees;
        }

        QSet<QString> seenTrees;
        QStringList trees;
        for (auto it = signalToTrees_.cbegin(); it != signalToTrees_.cend(); ++it) {
            if (!it.key().contains(filter)) {
                continue;
            }
            for (const QString& tree : it.value()) {
                const QString treeKey = tree.toLower();
                if (!seenTrees.contains(treeKey)) {
                    trees.push_back(tree);
                    seenTrees.insert(treeKey);
                }
            }
        }
        trees.sort(Qt::CaseInsensitive);
        return trees;
    }

    bool signalHasExactTreeMatches(const QString& signal)
    {
        const QString key = signalIndexKey(signal);
        if (key.isEmpty()) {
            return false;
        }
        ensureGlobalSignalIndex();
        return signalToTrees_.contains(key);
    }

    QStringList exactTreeMatchesForSignal(const QString& signal)
    {
        const QString key = signalIndexKey(signal);
        if (key.isEmpty()) {
            return {};
        }
        ensureGlobalSignalIndex();
        const auto it = signalToTrees_.constFind(key);
        if (it == signalToTrees_.cend()) {
            return {};
        }
        QStringList trees(it.value().cbegin(), it.value().cend());
        trees.sort(Qt::CaseInsensitive);
        return trees;
    }

    static QString signalIndexKey(const QString& signal)
    {
        QString key = normalizedMdsSignal(signal).trimmed().toLower();
        while (key.startsWith(QStringLiteral("\\\\"))) {
            key.remove(0, 1);
        }
        return key;
    }

    void updateTreeCompleter(Row* row, bool showReverseMatches)
    {
        if (!row || !row->treeModel) {
            return;
        }
        row->treeModel->setStringList(treeNamesForSignal(row->signal->text()));
        if (!treeAvailableForReverseMatch(row)) {
            if (row->reverseTreeMenu) {
                row->reverseTreeMenu->hide();
            }
            return;
        }
        const bool hasExactMatches = signalHasExactTreeMatches(row->signal->text());
        const QStringList exactTrees = hasExactMatches ? exactTreeMatchesForSignal(row->signal->text()) : QStringList();
        maybeApplyUniqueTreeMatch(row, exactTrees);
        if (exactTrees.size() <= 1) {
            if (row->reverseTreeMenu) {
                row->reverseTreeMenu->hide();
            }
            return;
        }
        if ((!showReverseMatches || row->signal->text().trimmed().isEmpty() || !hasExactMatches) && row->reverseTreeMenu) {
            row->reverseTreeMenu->hide();
        }
        if (!showReverseMatches || row->signal->text().trimmed().isEmpty()
            || row->treeModel->rowCount() == 0 || !hasExactMatches) {
            return;
        }

        if (row->signalCompleter && row->signalCompleter->popup()) {
            row->signalCompleter->popup()->hide();
        }
        if (row->treeCompleter && row->treeCompleter->popup()) {
            row->treeCompleter->popup()->hide();
        }
        if (row->reverseTreeMenu) {
            row->reverseTreeMenu->hide();
        }

        const QString signalText = row->signal->text();
        QTimer::singleShot(80, this, [this, row, signalText] {
            if (!row || row->deleted || !row->tree || !row->reverseTreeMenu || !row->treeModel
                || row->signal->text() != signalText || !signalHasExactTreeMatches(signalText)
                || row->treeModel->rowCount() == 0 || !treeAvailableForReverseMatch(row)
                || exactTreeMatchesForSignal(signalText).size() <= 1) {
                return;
            }
            if (row->signalCompleter && row->signalCompleter->popup() && row->signalCompleter->popup()->isVisible()) {
                row->signalCompleter->popup()->hide();
                QTimer::singleShot(80, this, [this, row, signalText] {
                    if (!row || row->deleted || !row->tree || !row->reverseTreeMenu || !row->treeModel
                        || row->signal->text() != signalText || !signalHasExactTreeMatches(signalText)
                        || row->treeModel->rowCount() == 0 || !treeAvailableForReverseMatch(row)
                        || exactTreeMatchesForSignal(signalText).size() <= 1) {
                        return;
                    }
                    showReverseTreeMenu(row, signalText);
                });
                return;
            }
            showReverseTreeMenu(row, signalText);
        });
    }

    static bool treeAvailableForReverseMatch(const Row* row)
    {
        if (!row || !row->tree) {
            return false;
        }
        const QString currentTree = row->tree->text().trimmed();
        return currentTree.isEmpty()
               || (!row->autoTree.isEmpty()
                   && currentTree.compare(row->autoTree, Qt::CaseInsensitive) == 0);
    }

    void maybeApplyUniqueTreeMatch(Row* row, const QStringList& trees)
    {
        if (!row || row->deleted || !row->tree || trees.size() != 1) {
            return;
        }

        const QString tree = trees.front();
        const QString currentTree = row->tree->text().trimmed();
        if (!treeAvailableForReverseMatch(row)) {
            return;
        }

        if (currentTree.compare(tree, Qt::CaseInsensitive) == 0) {
            row->autoTree = tree;
            return;
        }

        row->updatingTreeFromAutoMatch = true;
        row->tree->setText(tree);
        row->updatingTreeFromAutoMatch = false;
        row->autoTree = tree;
    }

    void showReverseTreeMenu(Row* row, const QString& signal)
    {
        if (!row || row->deleted || !row->tree || !row->reverseTreeMenu) {
            return;
        }

        const QStringList trees = exactTreeMatchesForSignal(signal);
        if (trees.isEmpty()) {
            return;
        }

        row->reverseTreeMenu->clear();
        for (const QString& tree : trees) {
            QAction* action = row->reverseTreeMenu->addAction(tree);
            connect(action, &QAction::triggered, this, [row, tree] {
                if (row && row->tree) {
                    row->tree->setText(tree);
                }
            });
        }

        const QFontMetrics fm(row->reverseTreeMenu->font());
        int menuWidth = row->tree->width();
        for (const QString& tree : trees) {
            menuWidth = std::max(menuWidth, fm.horizontalAdvance(tree) + 48);
        }
        row->reverseTreeMenu->setMinimumWidth(menuWidth);
        row->reverseTreeMenu->popup(row->tree->mapToGlobal(QPoint(0, row->tree->height())));
    }

    void updateSignalCompleter(Row* row)
    {
        if (!row || !row->signalModel) {
            return;
        }
        row->signalModel->setStringList(signalNamesForTree(row->tree->text()));
    }

    static void updateColorButton(Row* row)
    {
        row->colorButton->setFixedSize(36, 20);
        row->colorButton->setText("");
        row->colorButton->setStyleSheet(QString("QPushButton { background: %1; border: 1px solid palette(mid); }")
                                            .arg(row->color.name()));
    }

    QString defaultShot_;
    QString sourceIndexDir_;
    QWidget* rowsHost_ = nullptr;
    QGridLayout* rowsLayout_ = nullptr;
    QStringList treeNames_;
    QStringList globalSignals_;
    QHash<QString, QStringList> signalCache_;
    QHash<QString, QSet<QString>> signalToTrees_;
    QVector<Row*> rows_;
    bool globalSignalIndexLoaded_ = false;
};

class PanelSetupDialog final : public QDialog {
public:
    explicit PanelSetupDialog(const PlotSpec& plot, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Panel Setup");
        auto* layout = new QFormLayout(this);
        title_ = new QLineEdit(plot.title, this);
        xLabel_ = new QLineEdit(plot.xLabel, this);
        yLabel_ = new QLineEdit(plot.yLabel, this);
        customX_ = new QCheckBox("Custom X range", this);
        customY_ = new QCheckBox("Custom Y range", this);
        xmin_ = new QLineEdit(this);
        xmax_ = new QLineEdit(this);
        ymin_ = new QLineEdit(this);
        ymax_ = new QLineEdit(this);
        grid_ = new QCheckBox("Show grid", this);
        grid_->setChecked(plot.grid);
        customX_->setChecked(plot.customXRange);
        customY_->setChecked(plot.customYRange);
        if (std::isfinite(plot.xmin)) xmin_->setText(QString::number(plot.xmin, 'g', 12));
        if (std::isfinite(plot.xmax)) xmax_->setText(QString::number(plot.xmax, 'g', 12));
        if (std::isfinite(plot.ymin)) ymin_->setText(QString::number(plot.ymin, 'g', 12));
        if (std::isfinite(plot.ymax)) ymax_->setText(QString::number(plot.ymax, 'g', 12));

        layout->addRow("Title", title_);
        layout->addRow("X label", xLabel_);
        layout->addRow("Y label", yLabel_);
        layout->addRow(grid_);
        layout->addRow(customX_);
        layout->addRow("X min", xmin_);
        layout->addRow("X max", xmax_);
        layout->addRow(customY_);
        layout->addRow("Y min", ymin_);
        layout->addRow("Y max", ymax_);

        auto* buttons = new QHBoxLayout;
        auto* ok = new QPushButton("OK", this);
        auto* cancel = new QPushButton("Cancel", this);
        buttons->addStretch(1);
        buttons->addWidget(ok);
        buttons->addWidget(cancel);
        layout->addRow(buttons);
        connect(ok, &QPushButton::clicked, this, [this] {
            if (!validateRanges()) {
                return;
            }
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    void applyTo(PlotSpec* plot) const
    {
        plot->title = title_->text().trimmed();
        plot->xLabel = xLabel_->text().trimmed();
        plot->yLabel = yLabel_->text().trimmed();
        plot->grid = grid_->isChecked();
        plot->customXRange = customX_->isChecked();
        plot->customYRange = customY_->isChecked();
        optionalDoubleFromText(xmin_->text(), &plot->xmin);
        optionalDoubleFromText(xmax_->text(), &plot->xmax);
        optionalDoubleFromText(ymin_->text(), &plot->ymin);
        optionalDoubleFromText(ymax_->text(), &plot->ymax);
    }

private:
    bool validateRanges()
    {
        double value = qQNaN();
        for (QLineEdit* edit : {xmin_, xmax_, ymin_, ymax_}) {
            if (!optionalDoubleFromText(edit->text(), &value)) {
                QMessageBox::warning(this, "Panel Setup", "Range values must be numeric or empty.");
                return false;
            }
        }
        return true;
    }

    QLineEdit* title_ = nullptr;
    QLineEdit* xLabel_ = nullptr;
    QLineEdit* yLabel_ = nullptr;
    QCheckBox* customX_ = nullptr;
    QCheckBox* customY_ = nullptr;
    QCheckBox* grid_ = nullptr;
    QLineEdit* xmin_ = nullptr;
    QLineEdit* xmax_ = nullptr;
    QLineEdit* ymin_ = nullptr;
    QLineEdit* ymax_ = nullptr;
};

class LayoutCanvas final : public QWidget {
public:
    struct Item {
        int originalColumn = -1;
        int originalRow = -1;
        bool isNew = false;
        bool selected = false;
    };

    explicit LayoutCanvas(const LayoutConfig& config, QWidget* parent = nullptr, bool editable = true)
        : QWidget(parent)
        , editable_(editable)
    {
        setMinimumSize(620, 420);
        setFocusPolicy(Qt::StrongFocus);
        for (int c = 0; c < config.columns.size(); ++c) {
            QVector<Item> col;
            for (int r = 0; r < config.columns[c].size(); ++r) {
                col.push_back(Item{c, r, false, false});
            }
            columns_.push_back(std::move(col));
        }
        if (columns_.isEmpty()) {
            columns_.push_back({});
        }
        initialColumns_ = columns_;
    }

    void createPendingPanelAtRight()
    {
        if (!editable_) {
            return;
        }
        clearSelection();
        QVector<Item> col;
        col.push_back(Item{-1, -1, true, true});
        columns_.push_back(std::move(col));
        draggingNew_ = false;
        lastDragTarget_ = {-1, -1};
        update();
    }

    void deleteSelected()
    {
        if (!editable_) {
            return;
        }
        for (int c = columns_.size() - 1; c >= 0; --c) {
            for (int r = columns_[c].size() - 1; r >= 0; --r) {
                if (columns_[c][r].selected) {
                    columns_[c].removeAt(r);
                }
            }
            if (columns_[c].isEmpty() && columns_.size() > 1) {
                columns_.removeAt(c);
            }
        }
        if (columns_.isEmpty()) {
            columns_.push_back({});
        }
        draggingNew_ = false;
        lastDragTarget_ = {-1, -1};
        update();
    }

    void reset()
    {
        columns_ = initialColumns_;
        draggingNew_ = false;
        dragMoved_ = false;
        lastDragTarget_ = {-1, -1};
        update();
    }

    QVector<QVector<Item>> layoutItems() const
    {
        return columns_;
    }

    QVector<QPair<int, int>> selectedOriginalPanels() const
    {
        QVector<QPair<int, int>> panels;
        for (int c = 0; c < columns_.size(); ++c) {
            for (int r = 0; r < columns_[c].size(); ++r) {
                const Item& item = columns_[c][r];
                if (item.selected && !item.isNew && item.originalColumn >= 0 && item.originalRow >= 0) {
                    panels.push_back({item.originalColumn, item.originalRow});
                }
            }
        }
        return panels;
    }

    void selectAllOriginalPanels()
    {
        for (auto& col : columns_) {
            for (Item& item : col) {
                item.selected = !item.isNew;
            }
        }
        update();
    }

    void clearSelectedPanels()
    {
        clearSelection();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPalette pal = palette();
        painter.fillRect(rect(), pal.color(QPalette::Base));
        painter.setPen(QPen(pal.color(QPalette::Mid), 1));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        const QRectF area = drawingArea();
        const int displayCols = std::max(1, static_cast<int>(columns_.size()));
        const int displayRows = std::max(1, maxRows());
        const double gap = 8.0;
        const double cellW = std::max(28.0, (area.width() - gap * (displayCols - 1)) / displayCols);
        const double cellH = std::max(22.0, (area.height() - gap * (displayRows - 1)) / displayRows);
        for (int c = 0; c < columns_.size(); ++c) {
            for (int r = 0; r < columns_[c].size(); ++r) {
                const Item& item = columns_[c][r];
                const QColor fill = item.selected ? pal.color(QPalette::Highlight) : pal.color(QPalette::Midlight);
                const QRectF rect = cellRect(area, c, r, cellW, cellH, gap);
                drawCell(painter, rect, fill);
                if (item.isNew) {
                    painter.setPen(pal.color(QPalette::HighlightedText));
                    QFont f = painter.font();
                    f.setBold(true);
                    painter.setFont(f);
                    painter.drawText(rect, Qt::AlignCenter, "New panel");
                }
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        setFocus(Qt::MouseFocusReason);
        const auto hit = hitItem(event->position());
        if (hit.first < 0) {
            clearSelection();
            update();
            return;
        }
        Item& item = columns_[hit.first][hit.second];
        if (item.isNew) {
            pressWasSelected_ = item.selected;
            if (!item.selected) {
                clearSelection();
                item.selected = true;
            }
            draggingNew_ = item.selected;
            dragMoved_ = false;
            dragStart_ = event->position();
            lastDragTarget_ = {-1, -1};
        } else {
            item.selected = !item.selected;
            draggingNew_ = false;
            dragMoved_ = false;
            lastDragTarget_ = {-1, -1};
        }
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!draggingNew_ || !(event->buttons() & Qt::LeftButton)) {
            return;
        }
        if (!dragMoved_ && (event->position() - dragStart_).manhattanLength() < 3.0) {
            return;
        }
        dragMoved_ = true;
        const QPair<int, int> target = targetForPosition(event->position());
        if (target == lastDragTarget_) {
            return;
        }
        lastDragTarget_ = target;
        moveSelectedNewTo(event->position());
    }

    void mouseReleaseEvent(QMouseEvent*) override
    {
        if (draggingNew_ && !dragMoved_ && pressWasSelected_) {
            clearSelection();
            update();
        }
        draggingNew_ = false;
        dragMoved_ = false;
        lastDragTarget_ = {-1, -1};
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (editable_ && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
            deleteSelected();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    QRectF drawingArea() const
    {
        return rect().adjusted(18, 18, -18, -18);
    }

    int maxRows() const
    {
        int rows = 1;
        for (const auto& col : columns_) {
            rows = std::max(rows, static_cast<int>(col.size()));
        }
        return rows;
    }

    void clearSelection()
    {
        for (auto& col : columns_) {
            for (Item& item : col) {
                item.selected = false;
            }
        }
    }

    QPair<int, int> hitItem(const QPointF& pos) const
    {
        const QRectF area = drawingArea();
        const int displayCols = std::max(1, static_cast<int>(columns_.size()));
        const int displayRows = std::max(1, maxRows());
        const double gap = 8.0;
        const double cellW = std::max(28.0, (area.width() - gap * (displayCols - 1)) / displayCols);
        const double cellH = std::max(22.0, (area.height() - gap * (displayRows - 1)) / displayRows);
        for (int c = 0; c < columns_.size(); ++c) {
            for (int r = 0; r < columns_[c].size(); ++r) {
                if (cellRect(area, c, r, cellW, cellH, gap).contains(pos)) {
                    return {c, r};
                }
            }
        }
        return {-1, -1};
    }

    QPair<int, int> targetForPosition(const QPointF& pos) const
    {
        const QRectF area = drawingArea();
        const int displayCols = std::max(1, static_cast<int>(columns_.size()));
        const int displayRows = std::max(1, maxRows());
        const double gap = 8.0;
        const double cellW = std::max(28.0, (area.width() - gap * (displayCols - 1)) / displayCols);
        const double cellH = std::max(22.0, (area.height() - gap * (displayRows - 1)) / displayRows);
        const double boundaryBand = std::min(30.0, std::max(12.0, cellW * 0.16));

        if (pos.x() <= area.left() + boundaryBand) {
            return {0, -1};
        }
        for (int c = 0; c < columns_.size() - 1; ++c) {
            const double boundary = area.left() + c * (cellW + gap) + cellW + gap * 0.5;
            if (std::abs(pos.x() - boundary) <= boundaryBand) {
                return {c + 1, -1};
            }
        }
        const double afterLast = area.left() + (columns_.size() - 1) * (cellW + gap) + cellW;
        if (pos.x() >= afterLast - boundaryBand) {
            return {static_cast<int>(columns_.size()), -1};
        }

        for (int c = 0; c < columns_.size(); ++c) {
            const QRectF columnRect(area.left() + c * (cellW + gap), area.top(), cellW, area.height());
            if (columnRect.adjusted(boundaryBand * 0.25, 0, -boundaryBand * 0.25, 0).contains(pos)) {
                const int row = std::clamp(static_cast<int>((pos.y() - area.top()) / (cellH + gap) + 0.5),
                                           0,
                                           static_cast<int>(columns_[c].size()));
                return {c, row};
            }
        }
        int newColumn = 0;
        for (int c = 0; c < columns_.size(); ++c) {
            const double centerX = area.left() + c * (cellW + gap) + cellW * 0.5;
            if (pos.x() > centerX) {
                newColumn = c + 1;
            }
        }
        return {std::clamp(newColumn, 0, static_cast<int>(columns_.size())), -1};
    }

    void moveSelectedNewTo(const QPointF& pos)
    {
        int sourceCol = -1;
        int sourceRow = -1;
        for (int c = 0; c < columns_.size(); ++c) {
            for (int r = 0; r < columns_[c].size(); ++r) {
                if (columns_[c][r].isNew && columns_[c][r].selected) {
                    sourceCol = c;
                    sourceRow = r;
                    break;
                }
            }
            if (sourceCol >= 0) {
                break;
            }
        }
        if (sourceCol < 0) {
            return;
        }
        Item item = columns_[sourceCol].takeAt(sourceRow);
        if (columns_[sourceCol].isEmpty() && columns_.size() > 1) {
            columns_.removeAt(sourceCol);
        }

        QPair<int, int> target = targetForPosition(pos);
        if (target.second < 0) {
            target.first = std::clamp(target.first, 0, static_cast<int>(columns_.size()));
            columns_.insert(target.first, QVector<Item>{item});
        } else {
            target.first = std::clamp(target.first, 0, std::max(0, static_cast<int>(columns_.size()) - 1));
            target.second = std::clamp(target.second, 0, static_cast<int>(columns_[target.first].size()));
            columns_[target.first].insert(target.second, item);
        }
        update();
    }

    static QRectF cellRect(const QRectF& area, int column, int row, double cellW, double cellH, double gap)
    {
        return QRectF(area.left() + column * (cellW + gap), area.top() + row * (cellH + gap), cellW, cellH);
    }

    static void drawCell(QPainter& painter, const QRectF& rect, const QColor& color)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(rect, 7, 7);
    }

    QVector<QVector<Item>> columns_;
    QVector<QVector<Item>> initialColumns_;
    bool editable_ = true;
    bool draggingNew_ = false;
    bool dragMoved_ = false;
    bool pressWasSelected_ = false;
    QPointF dragStart_;
    QPair<int, int> lastDragTarget_ = {-1, -1};
};

class LayoutSetupDialog final : public QDialog {
public:
    explicit LayoutSetupDialog(const LayoutConfig& config, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Layout Setup");
        resize(900, 560);
        auto* mainLayout = new QHBoxLayout(this);
        canvas_ = new LayoutCanvas(config, this);
        mainLayout->addWidget(canvas_, 1);

        auto* side = new QWidget(this);
        auto* sideLayout = new QVBoxLayout(side);
        auto* addPanelButton = new QPushButton("Add new panel", side);
        auto* deleteButton = new QPushButton("Delete selected", side);
        auto* resetButton = new QPushButton("Reset", side);
        auto* applyButton = new QPushButton("Apply", side);
        auto* cancelButton = new QPushButton("Cancel", side);
        sideLayout->addWidget(addPanelButton);
        sideLayout->addSpacing(10);
        sideLayout->addWidget(deleteButton);
        sideLayout->addWidget(resetButton);
        sideLayout->addStretch(1);
        sideLayout->addWidget(applyButton);
        sideLayout->addWidget(cancelButton);
        mainLayout->addWidget(side);

        connect(addPanelButton, &QPushButton::clicked, canvas_, &LayoutCanvas::createPendingPanelAtRight);
        connect(deleteButton, &QPushButton::clicked, canvas_, &LayoutCanvas::deleteSelected);
        connect(resetButton, &QPushButton::clicked, canvas_, &LayoutCanvas::reset);
        connect(applyButton, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    }

    QVector<QVector<LayoutCanvas::Item>> layoutItems() const { return canvas_->layoutItems(); }

private:
    LayoutCanvas* canvas_ = nullptr;
};

class ExportDataDialog final : public QDialog {
public:
    explicit ExportDataDialog(const LayoutConfig& config,
                              const QString& defaultDir,
                              ExportFormat defaultFormat,
                              QWidget* parent = nullptr,
                              const PlotSpec* signalPlot = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Export Data");
        resize(900, 560);
        auto* mainLayout = new QHBoxLayout(this);
        if (signalPlot) {
            signalMode_ = true;
            signalList_ = new QListWidget(this);
            signalList_->setSelectionMode(QAbstractItemView::NoSelection);
            signalList_->setMinimumWidth(320);
            signalList_->setMaximumWidth(420);
            signalList_->setStyleSheet("QListWidget::indicator { width: 18px; height: 18px; }"
                                       "QListWidget::item { padding: 6px 4px; }");
            int visibleIndex = 0;
            for (int i = 0; i < signalPlot->signalSpecs.size(); ++i) {
                const SignalSpec& sig = signalPlot->signalSpecs[i];
                if (sig.hidden) {
                    continue;
                }
                QString label = normalizedMdsSignal(sig.yExpr);
                if (label.isEmpty()) {
                    label = QString("Signal %1").arg(++visibleIndex);
                } else {
                    ++visibleIndex;
                }
                const QString shot = effectiveSignalShot(*signalPlot, sig);
                if (!shot.isEmpty()) {
                    label += QString("  [%1]").arg(shot);
                }
                auto* item = new QListWidgetItem(label, signalList_);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Checked);
                item->setData(Qt::UserRole, i);
                item->setSizeHint(QSize(0, 34));
            }
            mainLayout->addWidget(signalList_);
        } else {
            canvas_ = new LayoutCanvas(config, this, false);
            mainLayout->addWidget(canvas_, 1);
        }

        auto* side = new QWidget(this);
        auto* sideLayout = new QVBoxLayout(side);
        auto* selectAll = new QPushButton("Select All", side);
        auto* clear = new QPushButton("Clear", side);
        sideLayout->addWidget(selectAll);
        sideLayout->addWidget(clear);
        sideLayout->addSpacing(10);

        auto* row = new QVBoxLayout;
        outputDir_ = new QLineEdit(defaultDir, this);
        auto* browse = new QPushButton("Browse", side);
        row->addWidget(new QLabel("Base dir", side));
        row->addWidget(outputDir_);
        row->addWidget(browse);
        sideLayout->addLayout(row);

        format_ = new QComboBox(side);
        format_->addItem("Text (*.txt)", static_cast<int>(ExportFormat::Text));
        format_->addItem("CSV (*.csv)", static_cast<int>(ExportFormat::Csv));
        format_->addItem("TSV (*.tsv)", static_cast<int>(ExportFormat::Tsv));
        format_->addItem("JSON (*.json)", static_cast<int>(ExportFormat::Json));
        const int defaultFormatIndex = format_->findData(static_cast<int>(defaultFormat));
        format_->setCurrentIndex(defaultFormatIndex >= 0 ? defaultFormatIndex : 0);
        sideLayout->addWidget(new QLabel("Format", side));
        sideLayout->addWidget(format_);
        sideLayout->addSpacing(10);

        range_ = new QComboBox(side);
        range_->addItem("All data", static_cast<int>(ExportRange::AllData));
        range_->addItem("Current view", static_cast<int>(ExportRange::CurrentView));
        range_->addItem("Custom X range", static_cast<int>(ExportRange::CustomXRange));
        range_->setCurrentIndex(0);
        sideLayout->addWidget(new QLabel("Range", side));
        sideLayout->addWidget(range_);
        auto* xRangeLayout = new QHBoxLayout;
        xmin_ = new QLineEdit(side);
        xmax_ = new QLineEdit(side);
        xmin_->setPlaceholderText("X min");
        xmax_->setPlaceholderText("X max");
        xRangeLayout->addWidget(xmin_);
        xRangeLayout->addWidget(xmax_);
        sideLayout->addLayout(xRangeLayout);
        sideLayout->addSpacing(10);

        auto* exportButton = new QPushButton("Export", side);
        auto* cancel = new QPushButton("Cancel", side);
        sideLayout->addStretch(1);
        sideLayout->addWidget(exportButton);
        sideLayout->addWidget(cancel);
        mainLayout->addWidget(side);

        connect(browse, &QPushButton::clicked, this, [this] {
            const QString dir = QFileDialog::getExistingDirectory(this, "Export Base Directory", outputDir_->text().trimmed());
            if (!dir.isEmpty()) {
                outputDir_->setText(dir);
            }
        });
        connect(selectAll, &QPushButton::clicked, this, [this] {
            if (signalMode_) {
                for (int i = 0; i < signalList_->count(); ++i) {
                    signalList_->item(i)->setCheckState(Qt::Checked);
                }
            } else if (canvas_) {
                canvas_->selectAllOriginalPanels();
            }
        });
        connect(clear, &QPushButton::clicked, this, [this] {
            if (signalMode_) {
                for (int i = 0; i < signalList_->count(); ++i) {
                    signalList_->item(i)->setCheckState(Qt::Unchecked);
                }
            } else if (canvas_) {
                canvas_->clearSelectedPanels();
            }
        });
        auto updateCustomRangeEnabled = [this] {
            const bool custom = exportRange() == ExportRange::CustomXRange;
            xmin_->setEnabled(custom);
            xmax_->setEnabled(custom);
        };
        connect(range_, &QComboBox::currentIndexChanged, this, updateCustomRangeEnabled);
        updateCustomRangeEnabled();
        connect(exportButton, &QPushButton::clicked, this, [this] {
            if (signalMode_ ? selectedSignals().isEmpty() : selectedPanels().isEmpty()) {
                QMessageBox::warning(this, "Export Data", signalMode_ ? "Select at least one signal." : "Select at least one panel.");
                return;
            }
            if (outputBaseDir().isEmpty()) {
                QMessageBox::warning(this, "Export Data", "Choose an output directory.");
                return;
            }
            if (exportRange() == ExportRange::CustomXRange && !customRangeValid()) {
                QMessageBox::warning(this, "Export Data", "Enter a valid custom X range.");
                return;
            }
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QVector<QPair<int, int>> selectedPanels() const
    {
        if (!canvas_) {
            return {};
        }
        return canvas_->selectedOriginalPanels();
    }

    QVector<int> selectedSignals() const
    {
        QVector<int> selectedSignalIndexes;
        if (!signalList_) {
            return selectedSignalIndexes;
        }
        for (int i = 0; i < signalList_->count(); ++i) {
            const QListWidgetItem* item = signalList_->item(i);
            if (item->checkState() == Qt::Checked) {
                selectedSignalIndexes.push_back(item->data(Qt::UserRole).toInt());
            }
        }
        return selectedSignalIndexes;
    }

    QString outputBaseDir() const
    {
        return outputDir_->text().trimmed();
    }

    ExportFormat exportFormat() const
    {
        return static_cast<ExportFormat>(format_->currentData().toInt());
    }

    ExportRange exportRange() const
    {
        return static_cast<ExportRange>(range_->currentData().toInt());
    }

    double customXMin() const
    {
        return xmin_->text().trimmed().toDouble();
    }

    double customXMax() const
    {
        return xmax_->text().trimmed().toDouble();
    }

private:
    bool customRangeValid() const
    {
        bool minOk = false;
        bool maxOk = false;
        const double xmin = xmin_->text().trimmed().toDouble(&minOk);
        const double xmax = xmax_->text().trimmed().toDouble(&maxOk);
        return minOk && maxOk && std::isfinite(xmin) && std::isfinite(xmax) && xmin != xmax;
    }

    bool signalMode_ = false;
    LayoutCanvas* canvas_ = nullptr;
    QListWidget* signalList_ = nullptr;
    QLineEdit* outputDir_ = nullptr;
    QComboBox* format_ = nullptr;
    QComboBox* range_ = nullptr;
    QLineEdit* xmin_ = nullptr;
    QLineEdit* xmax_ = nullptr;
};

bool layoutItemsMatchConfig(const QVector<QVector<LayoutCanvas::Item>>& layout, const LayoutConfig& config)
{
    if (layout.size() != config.columns.size()) {
        return false;
    }
    for (int c = 0; c < layout.size(); ++c) {
        if (layout[c].size() != config.columns[c].size()) {
            return false;
        }
        for (int r = 0; r < layout[c].size(); ++r) {
            const LayoutCanvas::Item& item = layout[c][r];
            if (item.isNew || item.originalColumn != c || item.originalRow != r) {
                return false;
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

QString htmlLink(const QString& label, const QString& url)
{
    return QStringLiteral("<a style=\"color:#ff8a65; text-decoration:none; font-weight:700;\" href=\"%1\">%2</a>")
        .arg(url.toHtmlEscaped(), label.toHtmlEscaped());
}

void openExternalUrlQuietly(const QString& urlText)
{
    const QUrl url(urlText);
    if (!url.isValid()) {
        return;
    }

#if defined(Q_OS_LINUX)
    QProcess opener;
    opener.setProgram(QStringLiteral("xdg-open"));
    opener.setArguments({url.toString()});
    opener.setStandardOutputFile(QProcess::nullDevice());
    opener.setStandardErrorFile(QProcess::nullDevice());
    if (opener.startDetached()) {
        return;
    }
#endif

    QDesktopServices::openUrl(url);
}

struct ParsedVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    bool valid = false;
};

ParsedVersion parseVersionTag(QString text)
{
    text = text.trimmed();
    if (text.startsWith('v', Qt::CaseInsensitive)) {
        text.remove(0, 1);
    }

    const QRegularExpression re(QStringLiteral(R"(^(\d+)(?:\.(\d+))?(?:\.(\d+))?$)"));
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch()) {
        return {};
    }

    ParsedVersion version;
    version.major = match.captured(1).toInt();
    version.minor = match.captured(2).isEmpty() ? 0 : match.captured(2).toInt();
    version.patch = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt();
    version.valid = true;
    return version;
}

int compareVersions(const ParsedVersion& lhs, const ParsedVersion& rhs)
{
    if (lhs.major != rhs.major) {
        return lhs.major < rhs.major ? -1 : 1;
    }
    if (lhs.minor != rhs.minor) {
        return lhs.minor < rhs.minor ? -1 : 1;
    }
    if (lhs.patch != rhs.patch) {
        return lhs.patch < rhs.patch ? -1 : 1;
    }
    return 0;
}

struct AboutColors {
    QString window;
    QString panel;
    QString panelBorder;
    QString iconPanel;
    QString separator;
    QString text;
    QString title;
    QString subtle;
    QString button;
    QString buttonBorder;
    QString buttonHover;
    QString buttonHoverBorder;
    QString disabledButton;
    QString disabledBorder;
    QString disabledText;
    QString status;
};

AboutColors aboutColors()
{
    const QPalette pal = QApplication::palette();
    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    if (!dark) {
        return AboutColors{
            QStringLiteral("#f8fafc"),
            QStringLiteral("#f3f4f6"),
            QStringLiteral("#d1d5db"),
            QStringLiteral("#e5e7eb"),
            QStringLiteral("#d1d5db"),
            QStringLiteral("#111827"),
            QStringLiteral("#0f172a"),
            QStringLiteral("#64748b"),
            QStringLiteral("#f3f4f6"),
            QStringLiteral("#cbd5e1"),
            QStringLiteral("#e5e7eb"),
            QStringLiteral("#94a3b8"),
            QStringLiteral("#e5e7eb"),
            QStringLiteral("#cbd5e1"),
            QStringLiteral("#64748b"),
            QStringLiteral("#475569"),
        };
    }

    return AboutColors{
        pal.color(QPalette::Window).name(QColor::HexRgb),
        pal.color(QPalette::AlternateBase).name(QColor::HexRgb),
        pal.color(QPalette::Mid).name(QColor::HexRgb),
        pal.color(QPalette::Button).name(QColor::HexRgb),
        pal.color(QPalette::Mid).name(QColor::HexRgb),
        pal.color(QPalette::WindowText).name(QColor::HexRgb),
        pal.color(QPalette::Text).name(QColor::HexRgb),
        pal.color(QPalette::Disabled, QPalette::WindowText).name(QColor::HexRgb),
        pal.color(QPalette::Button).name(QColor::HexRgb),
        pal.color(QPalette::Mid).name(QColor::HexRgb),
        pal.color(QPalette::Midlight).name(QColor::HexRgb),
        pal.color(QPalette::Highlight).name(QColor::HexRgb),
        pal.color(QPalette::AlternateBase).name(QColor::HexRgb),
        pal.color(QPalette::Mid).name(QColor::HexRgb),
        pal.color(QPalette::Disabled, QPalette::ButtonText).name(QColor::HexRgb),
        pal.color(QPalette::Disabled, QPalette::WindowText).name(QColor::HexRgb),
    };
}

QString aboutDialogStyleSheet()
{
    const AboutColors c = aboutColors();
    return QStringLiteral(
        "QDialog {"
        "  background: %1;"
        "  color: %6;"
        "}"
        "QLabel {"
        "  background: transparent;"
        "}"
        "QWidget#aboutHeader {"
        "  background: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 10px;"
        "}"
        "QWidget#aboutHeader QWidget {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QWidget#aboutIconBadge {"
        "  background: %4;"
        "  border: 1px solid %3;"
        "  border-radius: 12px;"
        "}"
        "QWidget#aboutCard {"
        "  background: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 10px;"
        "}"
        "QWidget#aboutCard QWidget {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QWidget#aboutRow {"
        "  background: transparent;"
        "}"
        "QLabel#aboutTitle {"
        "  font-weight: 700;"
        "  color: %7;"
        "}"
        "QLabel#aboutSubtitle {"
        "  color: %8;"
        "}"
        "QLabel#aboutKey {"
        "  color: %6;"
        "}"
        "QLabel#aboutValue {"
        "  color: %7;"
        "  font-weight: 700;"
        "}"
        "QFrame#aboutSeparator {"
        "  color: %5;"
        "  background: %5;"
        "  max-height: 1px;"
        "}"
        "QPushButton#aboutCloseButton, QPushButton#aboutUpdateButton {"
        "  background: %9;"
        "  border: 1px solid %10;"
        "  color: %7;"
        "  font-weight: 700;"
        "  padding: 7px 20px;"
        "  border-radius: 6px;"
        "}"
        "QPushButton#aboutCloseButton:hover, QPushButton#aboutUpdateButton:hover {"
        "  background: %11;"
        "  border-color: %12;"
        "}"
        "QPushButton#aboutUpdateButton:disabled {"
        "  background: %13;"
        "  border-color: %14;"
        "  color: %15;"
        "}"
        "QLabel#aboutUpdateStatus {"
        "  color: %16;"
        "  padding-left: 8px;"
        "}")
        .arg(c.window,
             c.panel,
             c.panelBorder,
             c.iconPanel,
             c.separator,
             c.text,
             c.title,
             c.subtle,
             c.button,
             c.buttonBorder,
             c.buttonHover,
             c.buttonHoverBorder,
             c.disabledButton,
             c.disabledBorder,
             c.disabledText,
             c.status);
}

QString aboutMessageBoxStyleSheet()
{
    const AboutColors c = aboutColors();
    return QStringLiteral(
        "QMessageBox {"
        "  background: %1;"
        "  color: %2;"
        "}"
        "QLabel {"
        "  color: %2;"
        "}"
        "QPushButton {"
        "  background: %3;"
        "  border: 1px solid %4;"
        "  color: %5;"
        "  font-weight: 700;"
        "  padding: 7px 18px;"
        "  border-radius: 6px;"
        "}"
        "QPushButton:hover {"
        "  background: %6;"
        "  border-color: %7;"
        "}")
        .arg(c.window,
             c.text,
             c.button,
             c.buttonBorder,
             c.title,
             c.buttonHover,
             c.buttonHoverBorder);
}

QMessageBox* makeAboutMessageBox(QWidget* parent,
                                 QMessageBox::Icon icon,
                                 const QString& title,
                                 const QString& text,
                                 const QString& informativeText)
{
    auto* message = new QMessageBox(parent);
    message->setWindowTitle(title);
    message->setIcon(icon);
    message->setText(text);
    message->setInformativeText(informativeText);
    message->setStyleSheet(aboutMessageBoxStyleSheet());
    return message;
}

class AboutDialog final : public QDialog {
public:
    explicit AboutDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("About MdsScope");
        setWindowIcon(appIcon());
        setModal(true);
        setSizeGripEnabled(false);
        networkManager_ = new QNetworkAccessManager(this);

        const FontSettings& fonts = fontSettings();
        const QFont baseFont(fonts.family, fonts.uiSize);
        const QFontMetrics baseMetrics(baseFont);
        setFont(baseFont);
        setFixedWidth(std::max(620, baseMetrics.horizontalAdvance(QStringLiteral("Git Version 3.0.r000.g000000000.dirty")) + 210));
        QFont titleFont = baseFont;
        titleFont.setBold(true);
        titleFont.setPointSize(std::max(18, baseFont.pointSize() + 6));
        QFont labelFont = baseFont;
        QFont valueFont = baseFont;
        valueFont.setBold(true);

        QString systemText = QSysInfo::prettyProductName().trimmed();
        if (systemText.isEmpty()) {
            systemText = QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion();
        }
        const QString arch = QSysInfo::currentCpuArchitecture();
        if (!arch.isEmpty()) {
            systemText += QStringLiteral(" (%1)").arg(arch);
        }

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 18, 20, 18);
        layout->setSpacing(12);

        auto* header = new QWidget(this);
        header->setObjectName("aboutHeader");
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(18, 16, 18, 16);
        headerLayout->setSpacing(18);

        auto* iconBadge = new QWidget(header);
        iconBadge->setObjectName("aboutIconBadge");
        iconBadge->setFixedSize(62, 62);
        auto* iconLayout = new QVBoxLayout(iconBadge);
        iconLayout->setContentsMargins(0, 0, 0, 0);
        auto* icon = new QLabel(iconBadge);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(appIcon().pixmap(46, 46));
        iconLayout->addWidget(icon);

        auto* leftBlock = new QWidget(header);
        auto* leftLayout = new QHBoxLayout(leftBlock);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->setSpacing(14);
        leftLayout->addWidget(iconBadge);
        auto* title = new QLabel("MdsScope", leftBlock);
        title->setObjectName("aboutTitle");
        title->setFont(titleFont);
        title->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        leftLayout->addWidget(title);
        headerLayout->addWidget(leftBlock, 0, Qt::AlignLeft | Qt::AlignVCenter);

        auto* subtitle = new QLabel("Signal data plotting for MDSplus experiments.", header);
        subtitle->setObjectName("aboutSubtitle");
        subtitle->setFont(baseFont);
        subtitle->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        subtitle->setWordWrap(true);
        subtitle->setMinimumWidth(230);
        headerLayout->addStretch(1);
        headerLayout->addWidget(subtitle, 1);
        layout->addWidget(header);

        auto* card = new QWidget(this);
        card->setObjectName("aboutCard");
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 6, 0, 6);
        cardLayout->setSpacing(0);

        auto addRow = [cardLayout, card, labelFont, valueFont](const QString& name, const QString& value, bool rich = false, bool separator = true) {
            auto* rowWidget = new QWidget(card);
            rowWidget->setObjectName("aboutRow");
            auto* rowLayout = new QHBoxLayout(rowWidget);
            rowLayout->setContentsMargins(16, 10, 16, 10);
            rowLayout->setSpacing(18);

            auto* nameLabel = new QLabel(name, rowWidget);
            nameLabel->setObjectName("aboutKey");
            nameLabel->setFont(labelFont);
            auto* valueLabel = new QLabel(value, rowWidget);
            valueLabel->setObjectName("aboutValue");
            valueLabel->setFont(valueFont);
            valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            valueLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
            valueLabel->setFocusPolicy(Qt::NoFocus);
            valueLabel->setOpenExternalLinks(false);
            valueLabel->setTextFormat(rich ? Qt::RichText : Qt::PlainText);
            valueLabel->setWordWrap(true);
            valueLabel->setMinimumWidth(300);
            if (rich) {
                connect(valueLabel, &QLabel::linkActivated, valueLabel, [](const QString& url) {
                    openExternalUrlQuietly(url);
                    if (QWidget* focus = QApplication::focusWidget()) {
                        focus->clearFocus();
                    }
                });
            }
            rowLayout->addWidget(nameLabel);
            rowLayout->addWidget(valueLabel, 1);
            cardLayout->addWidget(rowWidget);
            if (separator) {
                auto* line = new QFrame(card);
                line->setObjectName("aboutSeparator");
                line->setFrameShape(QFrame::HLine);
                line->setFrameShadow(QFrame::Plain);
                cardLayout->addWidget(line);
            }
        };

        addRow("MdsScope Version", QStringLiteral(MDSSCOPE_VERSION));
        addRow("Git Version", QStringLiteral(MDSSCOPE_GIT_VERSION));
        addRow("Qt Version", QString::fromLatin1(qVersion()));
        addRow("System", systemText);
        addRow("Copyright", QStringLiteral("Copyright (C) 2026 ") + htmlLink("Weikang Wang", "https://github.com/wwktz"), true);
        addRow("License", htmlLink("GPL-3.0-or-later", "https://www.gnu.org/licenses/gpl-3.0.html"), true);
        addRow("Source", htmlLink("GitHub", "https://github.com/wwktz/MdsScope"), true, false);
        layout->addWidget(card);

        auto* close = new QPushButton("Close", this);
        close->setObjectName("aboutCloseButton");
        close->setDefault(true);
        updateButton_ = new QPushButton("Update", this);
        updateButton_->setObjectName("aboutUpdateButton");
        updateStatus_ = new QLabel(this);
        updateStatus_->setObjectName("aboutUpdateStatus");
        auto* footer = new QHBoxLayout;
        footer->addWidget(updateButton_);
        footer->addWidget(updateStatus_, 1);
        footer->addStretch(1);
        footer->addWidget(close);
        layout->addLayout(footer);

        setStyleSheet(aboutDialogStyleSheet());

        adjustSize();
        setFixedHeight(sizeHint().height());
        connect(updateButton_, &QPushButton::clicked, this, [this] {
            checkForUpdate();
        });
        connect(close, &QPushButton::clicked, this, &QDialog::accept);
    }

private:
    void setUpdateBusy(bool busy, const QString& status)
    {
        if (updateButton_) {
            updateButton_->setEnabled(!busy);
        }
        if (updateStatus_) {
            updateStatus_->setText(status);
        }
    }

    QNetworkRequest updateRequest(const QUrl& url) const
    {
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "MdsScope");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        return request;
    }

    void checkForUpdate()
    {
        setUpdateBusy(true, "Checking...");
        QNetworkReply* reply = networkManager_->get(updateRequest(QUrl(QStringLiteral("https://api.github.com/repos/wwktz/MdsScope/releases/latest"))));
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            handleLatestRelease(reply);
            reply->deleteLater();
        });
    }

    void handleLatestRelease(QNetworkReply* reply)
    {
        if (reply->error() != QNetworkReply::NoError) {
            setUpdateBusy(false, "Check failed");
            auto* message = makeAboutMessageBox(this,
                                                QMessageBox::Warning,
                                                "Update",
                                                "Could not check for updates.",
                                                reply->errorString());
            message->exec();
            message->deleteLater();
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            setUpdateBusy(false, "Check failed");
            auto* message = makeAboutMessageBox(this,
                                                QMessageBox::Warning,
                                                "Update",
                                                "Could not check for updates.",
                                                "GitHub returned an invalid release response.");
            message->exec();
            message->deleteLater();
            return;
        }

        const QJsonObject root = document.object();
        const QString tagName = root.value(QStringLiteral("tag_name")).toString().trimmed();
        const QString releaseUrl = root.value(QStringLiteral("html_url")).toString(QStringLiteral("https://github.com/wwktz/MdsScope/releases"));
        const ParsedVersion current = parseVersionTag(QStringLiteral(MDSSCOPE_VERSION));
        const ParsedVersion latest = parseVersionTag(tagName);
        if (!current.valid || !latest.valid) {
            setUpdateBusy(false, "Check failed");
            auto* message = makeAboutMessageBox(this,
                                                QMessageBox::Warning,
                                                "Update",
                                                "Could not compare release versions.",
                                                QStringLiteral("Current: %1\nLatest: %2").arg(QStringLiteral(MDSSCOPE_VERSION), tagName));
            message->exec();
            message->deleteLater();
            return;
        }

        if (compareVersions(latest, current) <= 0) {
            setUpdateBusy(false, "Up to date");
            auto* message = makeAboutMessageBox(this,
                                                QMessageBox::Information,
                                                "Update",
                                                QStringLiteral("MdsScope %1 is up to date.").arg(QStringLiteral(MDSSCOPE_VERSION)),
                                                {});
            message->exec();
            message->deleteLater();
            return;
        }

        setUpdateBusy(false, "Update available");
        auto* message = makeAboutMessageBox(this,
                                            QMessageBox::Information,
                                            "Update",
                                            QStringLiteral("MdsScope %1 is available.").arg(tagName),
                                            "Open the GitHub release page to download it?");
        QPushButton* openRelease = message->addButton("Open Release", QMessageBox::AcceptRole);
        message->addButton(QMessageBox::Cancel);
        message->exec();
        if (message->clickedButton() == openRelease) {
            openExternalUrlQuietly(releaseUrl);
        }
        message->deleteLater();
    }

    QNetworkAccessManager* networkManager_ = nullptr;
    QPushButton* updateButton_ = nullptr;
    QLabel* updateStatus_ = nullptr;
};
}

MainWindow::MainWindow(QString rootPath, QWidget* parent)
    : QMainWindow(parent), rootPath_(std::move(rootPath))
{
    setWindowIcon(appIcon());
    environmentPath_ = appEnvironmentDir(rootPath_);
    ensureSourceIndexCache(rootPath_);
    exportBasePath_ = QSettings(uiSettingsPath(rootPath_), QSettings::IniFormat)
                          .value("export/base_dir", defaultExportBaseDir())
                          .toString();
    loadFontSettings(rootPath_);
    buildUi();
    applyUiFont();
    connect(&panelWatcher_, &QFutureWatcher<QVector<LoadedSignal>>::finished, this, [this] {
        if (!activePanelRefreshKey_.isEmpty()) {
            applyPanelLoadedSignals(panelWatcher_.result());
            activePanelRefreshKey_.clear();
        }
        if (pendingPanelRefresh_) {
            const int column = pendingPanelColumn_;
            const int row = pendingPanelRow_;
            const int signal = pendingPanelSignal_;
            pendingPanelRefresh_ = false;
            pendingPanelColumn_ = -1;
            pendingPanelRow_ = -1;
            pendingPanelSignal_ = -1;
            queuedPanelRefreshKey_.clear();
            refreshOne(column, row, signal);
        }
    });
    connect(&warmWatcher_, &QFutureWatcher<void>::finished, this, [this] {
        if (runningDataFetches_ <= 0 && activeRefreshKey_.isEmpty()) {
            setStatus("MDS connections ready");
        }
    });
    loadDefaultEnvironment(true);
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange && aboutButton_) {
        aboutButton_->setIcon(infoIcon());
    }
    if (event->type() == QEvent::PaletteChange && recentEnvironmentButton_) {
        recentEnvironmentButton_->setIcon(recentArrowIcon());
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::schedulePointSync(PlotWidget* source, double x)
{
    pendingPointX_ = x;
    pointSyncSource_ = source;
    const int generation = pointSyncGeneration_;
    if (pointSyncQueued_) {
        return;
    }
    pointSyncQueued_ = true;
    QTimer::singleShot(16, this, [this, generation] {
        if (generation != pointSyncGeneration_) {
            return;
        }
        pointSyncQueued_ = false;
        if (!(pointButton_ && pointButton_->isChecked()) || !std::isfinite(pendingPointX_)) {
            return;
        }
        PlotWidget* source = pointSyncSource_;
        const double x = pendingPointX_;
        QRect visibleArea = gridHost_->rect();
        if (scrollArea_ && scrollArea_->viewport()) {
            visibleArea = QRect(gridHost_->mapFrom(scrollArea_->viewport(), QPoint(0, 0)),
                                scrollArea_->viewport()->size()).intersected(gridHost_->rect());
        }
        for (const auto& col : plotWidgets_) {
            for (PlotWidget* plot : col) {
                if (!plot || !plot->isVisible()) {
                    continue;
                }
                const QRect plotGeometry = QRect(plot->mapTo(gridHost_, QPoint(0, 0)), plot->size());
                if (!visibleArea.intersects(plotGeometry)) {
                    continue;
                }
                const int seriesIndex = plot == source ? plot->activePointSeriesIndex() : 0;
                plot->setSyncedPointX(x, seriesIndex);
            }
        }
        if (globalPointOverlay_) {
            globalPointOverlay_->clearReadouts();
        }
    });
}

void MainWindow::buildUi()
{
    setWindowTitle("MdsScope");
    menuBar()->hide();
    QAction* zoomModeAction = new QAction("Zoom / Move", this);
    QAction* pointModeAction = new QAction("Point", this);
    connect(zoomModeAction, &QAction::triggered, this, [this] { setInteractionMode(InteractionMode::Zoom); });
    connect(pointModeAction, &QAction::triggered, this, [this] { setInteractionMode(InteractionMode::Point); });
    zoomModeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
    pointModeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    addAction(zoomModeAction);
    addAction(pointModeAction);

    auto* toolbar = addToolBar("Tools");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setStyleSheet(
        "QToolBar { spacing: 5px; padding: 2px 4px; border: 0px; }"
        "QToolButton { margin: 0px; padding: 3px; min-width: 30px; min-height: 30px; }");
    QAction* openAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Open configure file", this, &MainWindow::openEnvironmentFile);
    openButton_ = qobject_cast<QToolButton*>(toolbar->widgetForAction(openAction));
    if (openButton_) {
        openButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    auto* recentEnvironmentSpace = new QWidget(toolbar);
    recentEnvironmentSpace->setFixedSize(7, 30);
    toolbar->addWidget(recentEnvironmentSpace);
    recentEnvironmentButton_ = new QToolButton(toolbar);
    recentEnvironmentButton_->setObjectName("recentEnvironmentButton");
    recentEnvironmentButton_->setIcon(recentArrowIcon());
    recentEnvironmentButton_->setIconSize(QSize(12, 30));
    recentEnvironmentButton_->setToolTip("Recent configure files");
    recentEnvironmentButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    recentEnvironmentButton_->setAutoRaise(true);
    recentEnvironmentButton_->setFixedSize(12, 30);
    recentEnvironmentButton_->setStyleSheet(
        "QToolButton#recentEnvironmentButton {"
        "  background: transparent;"
        "  color: palette(buttonText);"
        "  border: 0px;"
        "  padding: 0px;"
        "  margin: 0px;"
        "  min-width: 12px;"
        "  max-width: 12px;"
        "  min-height: 30px;"
        "  max-height: 30px;"
        "}");
    auto positionRecentEnvironmentButton = [toolbar, this] {
        if (openButton_ && recentEnvironmentButton_) {
            recentEnvironmentButton_->move(openButton_->mapTo(toolbar, QPoint(openButton_->width() - 4, 0)));
            recentEnvironmentButton_->raise();
        }
    };
    if (openButton_) {
        positionRecentEnvironmentButton();
        QTimer::singleShot(0, this, positionRecentEnvironmentButton);
        recentEnvironmentButton_->show();
    }
    connect(recentEnvironmentButton_, &QToolButton::clicked, this, &MainWindow::showRecentEnvironmentMenu);
    QWidget* recentMenuParent = openButton_ ? static_cast<QWidget*>(openButton_) : static_cast<QWidget*>(toolbar);
    recentEnvironmentMenu_ = new QMenu(recentMenuParent);
    connect(recentEnvironmentMenu_, &QMenu::aboutToShow, this, &MainWindow::refreshRecentEnvironmentMenu);
    refreshRecentEnvironmentMenu();
    QAction* saveAction = toolbar->addAction(saveIcon(), "Save", this, &MainWindow::saveCurrentEnvironment);
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(saveAction);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "Export data", this, &MainWindow::openExportDataDialog);
    toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Refresh", this, &MainWindow::refreshData);
    loginAction_ = toolbar->addAction(loginIcon(false), "Login", this, &MainWindow::openLoginDialog);
    updateLoginActionIcon();
    toolbar->addAction(gearIcon(), "Layout setup", this, &MainWindow::openLayoutSetupDialog);
    toolbar->addAction(fontIcon(), "Customize fonts", this, &MainWindow::openCustomizeDialog);

    gridHost_ = new QWidget(this);
    gridHost_->setFocusPolicy(Qt::StrongFocus);
    gridHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    gridLayout_ = new QGridLayout(gridHost_);
    gridLayout_->setContentsMargins(0, 0, 0, 0);
    gridLayout_->setSpacing(0);
    setCentralWidget(gridHost_);
    globalPointOverlay_ = new GlobalPointOverlay(gridHost_);
    globalPointOverlay_->setGeometry(gridHost_->rect());
    globalPointOverlay_->raise();

    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet("color: palette(highlight);");

    toolbar->addSeparator();
    auto* topControls = new QWidget(toolbar);
    topControls->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* topLayout = new QHBoxLayout(topControls);
    topLayout->setContentsMargins(2, 0, 2, 0);
    topLayout->setSpacing(3);
    topControls->setStyleSheet(
        "QPushButton {"
        "  padding: 1px 8px;"
        "  min-height: 18px;"
        "}"
        "QLineEdit, QComboBox {"
        "  min-height: 18px;"
        "  padding: 0px 2px;"
        "}"
        "QLabel { margin-left: 2px; margin-right: 2px; }"
        "QToolButton#aboutButton {"
        "  border: 1px solid transparent;"
        "  border-radius: 15px;"
        "  background: transparent;"
        "  padding: 0px;"
        "  margin-left: 8px;"
        "}");
    topLayout->addWidget(new QLabel("Rate", topControls));
    dataModeCombo_ = new QComboBox(topControls);
    dataModeCombo_->addItem("Thin", static_cast<int>(DataReadMode::Thin));
    dataModeCombo_->addItem("Medium", static_cast<int>(DataReadMode::Medium));
    dataModeCombo_->addItem("Full", static_cast<int>(DataReadMode::Full));
    dataModeCombo_->setCurrentIndex(0);
    dataModeCombo_->setFixedWidth(90);
    topLayout->addWidget(dataModeCombo_);

    topInfoLabel_ = new QLabel("Shot: --", topControls);
    ipInfoLabel_ = new QLabel("Ip: --", topControls);
    pulseInfoLabel_ = new QLabel("Pulse: --", topControls);
    itInfoLabel_ = new QLabel("It: --", topControls);
    timeInfoLabel_ = new QLabel("Time: --", topControls);
    for (QLabel* label : {topInfoLabel_, ipInfoLabel_, pulseInfoLabel_, itInfoLabel_, timeInfoLabel_}) {
        label->setStyleSheet("color: palette(highlight);");
        topLayout->addWidget(label);
    }
    topLayout->addStretch(1);
    topLayout->addWidget(new ThemeModeButton(topControls));
    aboutButton_ = new QToolButton(topControls);
    aboutButton_->setObjectName("aboutButton");
    aboutButton_->setIcon(infoIcon());
    aboutButton_->setIconSize(QSize(28, 28));
    aboutButton_->setFixedSize(34, 34);
    aboutButton_->setToolTip("About MdsScope");
    topLayout->addWidget(aboutButton_);
    toolbar->addWidget(topControls);

    auto* bottom = new QWidget(this);
    auto* bottomLayout = new QHBoxLayout(bottom);
    bottomLayout->setContentsMargins(4, 1, 4, 1);
    bottomLayout->setSpacing(5);
    bottom->setMaximumHeight(34);
    bottom->setStyleSheet(
        "QPushButton {"
        "  padding: 1px 8px;"
        "  min-height: 18px;"
        "}"
        "QLineEdit, QComboBox {"
        "  min-height: 18px;"
        "  padding: 0px 2px;"
        "}"
        "QToolButton { margin: 0px; padding: 1px; min-width: 30px; min-height: 28px; }");
    zoomButton_ = new QToolButton(bottom);
    pointButton_ = new QToolButton(bottom);
    for (QToolButton* button : {zoomButton_, pointButton_}) {
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setIconSize(QSize(24, 24));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    zoomButton_->setToolTip("Zoom / Move (Ctrl+Z): drag to zoom, middle-drag or Shift-drag to move");
    pointButton_->setToolTip("Point (Ctrl+P): click to activate, Esc to exit");
    pointButton_->setChecked(true);
    bottomLayout->addWidget(zoomButton_);
    bottomLayout->addWidget(pointButton_);
    bottomLayout->addWidget(new QLabel("Shot", bottom));
    shotCombo_ = new QComboBox(bottom);
    shotCombo_->setEditable(true);
    shotCombo_->setInsertPolicy(QComboBox::NoInsert);
    shotCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    shotCombo_->setMaxVisibleItems(10);
    shotCombo_->view()->setTextElideMode(Qt::ElideMiddle);
    shotCombo_->view()->setMinimumWidth(260);
    shotCombo_->view()->setMaximumWidth(520);
    shotEdit_ = shotCombo_->lineEdit();
    refreshShotHistory();
    auto resizeShotEdit = [this] {
        if (!shotEdit_ || !shotCombo_) {
            return;
        }
        const QFontMetrics fm(shotEdit_->font());
        const int textWidth = fm.horizontalAdvance(shotEdit_->text().trimmed().isEmpty()
                                                       ? QStringLiteral("143850-143858")
                                                       : shotEdit_->text().trimmed());
        shotCombo_->setFixedWidth(std::clamp(textWidth + 52, 120, 620));
    };
    resizeShotEdit();
    bottomLayout->addWidget(shotCombo_);
    auto* apply = new QPushButton("Apply", bottom);
    auto* prev = new QPushButton("Prev", bottom);
    auto* next = new QPushButton("Next", bottom);
    auto* stop = new QPushButton("Stop", bottom);
    auto* latest = new QPushButton("Latest", bottom);
    stopButton_ = stop;
    // Fix the width to the wider "Continue" label (plus padding) so toggling
    // the text never clips it or shifts the buttons to its right.
    stop->setText("Continue");
    stop->setFixedWidth(stop->sizeHint().width() + 16);
    stop->setText("Stop");
    bottomLayout->addWidget(apply);
    bottomLayout->addWidget(prev);
    bottomLayout->addWidget(next);
    bottomLayout->addWidget(stop);
    bottomLayout->addWidget(latest);
    bottomLayout->addWidget(statusLabel_, 1);
    statusBar()->addWidget(bottom, 1);
    setInteractionMode(InteractionMode::Point);

    connect(zoomButton_, &QToolButton::clicked, this, [this] { setInteractionMode(InteractionMode::Zoom); });
    connect(pointButton_, &QToolButton::clicked, this, [this] { setInteractionMode(InteractionMode::Point); });
    connect(apply, &QPushButton::clicked, this, &MainWindow::applyShot);
    connect(prev, &QPushButton::clicked, this, [this] { stepShot(-1); });
    connect(next, &QPushButton::clicked, this, [this] { stepShot(1); });
    connect(latest, &QPushButton::clicked, this, &MainWindow::latestShot);
    connect(stop, &QPushButton::clicked, this, &MainWindow::onStopOrContinue);
    connect(shotEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyShot);
    connect(shotEdit_, &QLineEdit::textChanged, this, [resizeShotEdit] { resizeShotEdit(); });
    connect(shotCombo_, &QComboBox::activated, this, [this](int index) {
        const QString shot = shotCombo_->itemData(index).toString();
        if (!shot.isEmpty()) {
            shotCombo_->setEditText(shot);
        }
        applyShot();
    });
    connect(dataModeCombo_, &QComboBox::currentIndexChanged, this, [this] { refreshData(); });
    connect(aboutButton_, &QToolButton::clicked, this, &MainWindow::openAboutDialog);
}

void MainWindow::loadDefaultEnvironment(bool useLatestWhenNoCurrentShot)
{
    const QString defaultTomlConfig = QDir(environmentPath_).filePath("init.toml");
    const QString defaultWebscpConfig = QDir(environmentPath_).filePath("init.webscp");
    if (QFileInfo::exists(defaultTomlConfig)) {
        loadEnvironmentFile(defaultTomlConfig, useLatestWhenNoCurrentShot, false, true);
    } else if (QFileInfo::exists(defaultWebscpConfig)) {
        loadEnvironmentFile(defaultWebscpConfig, useLatestWhenNoCurrentShot, false, true);
    } else {
        loadEnvironmentList(useLatestWhenNoCurrentShot);
    }
}

void MainWindow::loadEnvironmentList(bool useLatestWhenNoCurrentShot)
{
    QDir dir(environmentPath_);
    const auto files = dir.entryInfoList({"*.toml", "*.webscp"}, QDir::Files, QDir::Name);
    if (!files.isEmpty()) {
        loadEnvironmentFile(files.first().absoluteFilePath(), useLatestWhenNoCurrentShot, false, true);
        return;
    }
    setStatus("No environment files found");
}

void MainWindow::loadSelectedEnvironment()
{
    openEnvironmentFile();
}

QString MainWindow::rememberedFileDialogDir() const
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    const QString savedPath = settings.value("files/last_dir").toString().trimmed();
    return !savedPath.isEmpty() && QDir(savedPath).exists()
               ? QDir(savedPath).absolutePath()
               : environmentPath_;
}

void MainWindow::rememberFileDialogDir(const QString& path)
{
    QFileInfo info(path);
    const QString dirPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (dirPath.isEmpty()) {
        return;
    }
    const QString selectedPath = QDir(dirPath).absolutePath();
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    settings.setValue("files/last_dir", selectedPath);
}

QStringList MainWindow::recentEnvironmentFiles() const
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    QStringList files = settings.value("files/recent").toStringList();
    QStringList cleaned;
    for (const QString& file : files) {
        const QString path = QFileInfo(file).absoluteFilePath();
        if (!path.isEmpty() && QFileInfo::exists(path) && !cleaned.contains(path)) {
            cleaned.push_back(path);
        }
        if (cleaned.size() >= 10) {
            break;
        }
    }
    return cleaned;
}

void MainWindow::rememberRecentEnvironmentFile(const QString& path)
{
    const QString filePath = QFileInfo(path).absoluteFilePath();
    if (filePath.isEmpty()) {
        return;
    }
    QStringList files = recentEnvironmentFiles();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > 10) {
        files.removeLast();
    }
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    settings.setValue("files/recent", files);
}

void MainWindow::clearRecentEnvironmentFiles()
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    settings.remove("files/recent");
    refreshRecentEnvironmentMenu();
}

void MainWindow::openEnvironmentFile()
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    const QString allFilter = "All MdsScope Config (*.toml *.webscp)";
    const QString tomlFilter = "MdsScope TOML (*.toml)";
    const QString webscpFilter = "Legacy WebScope Config (*.webscp)";
    const QString filters = allFilter + ";;" + tomlFilter + ";;" + webscpFilter + ";;All Files (*)";
    QString selectedFilter = settings.value("files/open_filter", allFilter).toString();
    const QString path = QFileDialog::getOpenFileName(this, "Open MdsScope Config", rememberedFileDialogDir(), filters, &selectedFilter);
    if (!path.isEmpty()) {
        settings.setValue("files/open_filter", selectedFilter);
        rememberFileDialogDir(path);
        loadEnvironmentFile(path);
    }
}

void MainWindow::openRecentEnvironmentFile(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, "Open MdsScope Config", "Recent file no longer exists:\n" + path);
        refreshRecentEnvironmentMenu();
        return;
    }
    rememberFileDialogDir(path);
    loadEnvironmentFile(path);
}

void MainWindow::refreshRecentEnvironmentMenu()
{
    if (!recentEnvironmentMenu_) {
        return;
    }
    recentEnvironmentMenu_->clear();
    const QStringList files = recentEnvironmentFiles();
    if (files.isEmpty()) {
        QAction* empty = recentEnvironmentMenu_->addAction("No Recent Files");
        empty->setEnabled(false);
        return;
    }

    const QFontMetrics fm(recentEnvironmentMenu_->font());
    int menuTextWidth = fm.horizontalAdvance("Clear Recent Files");
    for (const QString& path : files) {
        menuTextWidth = std::max(menuTextWidth, fm.horizontalAdvance(QFileInfo(path).fileName()));
    }
    const int menuWidth = std::clamp(menuTextWidth + 56, 220, 520);
    recentEnvironmentMenu_->setMinimumWidth(menuWidth);
    for (const QString& path : files) {
        const QFileInfo info(path);
        QAction* action = recentEnvironmentMenu_->addAction(fm.elidedText(info.fileName(), Qt::ElideMiddle, menuWidth - 36));
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path] { openRecentEnvironmentFile(path); });
    }
    recentEnvironmentMenu_->addSeparator();
    QAction* clearAction = recentEnvironmentMenu_->addAction("Clear Recent Files");
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearRecentEnvironmentFiles);
}

void MainWindow::showRecentEnvironmentMenu()
{
    if (!openButton_ || !recentEnvironmentMenu_) {
        return;
    }
    refreshRecentEnvironmentMenu();
    recentEnvironmentMenu_->popup(openButton_->mapToGlobal(QPoint(0, openButton_->height())));
}

QStringList MainWindow::recentShotExpressions() const
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    QStringList shots = settings.value("shot/recent").toStringList();
    QStringList cleaned;
    for (const QString& shot : shots) {
        const QString value = shot.trimmed();
        if (!value.isEmpty() && !cleaned.contains(value)) {
            cleaned.push_back(value);
        }
        if (cleaned.size() >= 10) {
            break;
        }
    }
    return cleaned;
}

void MainWindow::rememberShotExpression(const QString& shot)
{
    const QString value = shot.trimmed();
    if (value.isEmpty()) {
        return;
    }
    QStringList shots = recentShotExpressions();
    shots.removeAll(value);
    shots.prepend(value);
    while (shots.size() > 10) {
        shots.removeLast();
    }
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    settings.setValue("shot/recent", shots);
    refreshShotHistory();
}

void MainWindow::refreshShotHistory()
{
    if (!shotCombo_ || !shotEdit_) {
        return;
    }
    const QString current = shotEdit_->text();
    QSignalBlocker comboBlocker(shotCombo_);
    QSignalBlocker editBlocker(shotEdit_);
    shotCombo_->clear();
    const QFontMetrics fm(shotCombo_->font());
    for (const QString& shot : recentShotExpressions()) {
        shotCombo_->addItem(fm.elidedText(shot, Qt::ElideMiddle, 300), shot);
        shotCombo_->setItemData(shotCombo_->count() - 1, shot, Qt::ToolTipRole);
    }
    shotCombo_->setEditText(current);
}

bool MainWindow::loadEnvironmentFile(const QString& path,
                                     bool useLatestWhenNoCurrentShot,
                                     bool rememberRecent,
                                     bool prewarmBeforeRefresh)
{
    const QString previousShot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    const bool shouldFetchLatest = useLatestWhenNoCurrentShot && previousShot.isEmpty();
    cancelPrewarmConnections();
    startupPrewarmPending_ = false;
    activeRefreshKey_.clear();
    activePanelRefreshKey_.clear();
    pendingRefresh_ = false;
    pendingPanelRefresh_ = false;
    queuedRefreshKey_.clear();
    queuedPanelRefreshKey_.clear();
    queuedLoadedSignals_.clear();
    queuedLoadedSignalApply_ = false;
    config_ = parseEnvironment(path);
    if (!shouldFetchLatest) {
        updateShotControlsFromConfig(previousShot);
    }
    selectedColumn_ = -1;
    selectedRow_ = -1;
    rebuildGrid();
    if (shouldFetchLatest) {
        setLabelTextIfChanged(topInfoLabel_, QStringLiteral("Shot: --"));
        setLabelTextIfChanged(ipInfoLabel_, QStringLiteral("Ip: --"));
        setLabelTextIfChanged(pulseInfoLabel_, QStringLiteral("Pulse: --"));
        setLabelTextIfChanged(itInfoLabel_, QStringLiteral("It: --"));
        setLabelTextIfChanged(timeInfoLabel_, QStringLiteral("Time: --"));
    } else {
        updateTopInfoLabels();
    }
    setStatus(QString("Loaded %1").arg(QFileInfo(path).fileName()));
    if (rememberRecent) {
        rememberRecentEnvironmentFile(path);
    }
    if (prewarmBeforeRefresh) {
        startupPrewarmPending_ = true;
        QTimer::singleShot(1000, this, [this] {
            if (startupPrewarmPending_ && runningDataFetches_ <= 0 && activeRefreshKey_.isEmpty()) {
                startupPrewarmPending_ = false;
                prewarmConnections();
            }
        });
        if (!shouldFetchLatest) {
            refreshData();
        }
    } else if (!shouldFetchLatest) {
        refreshData();
    }
    if (shouldFetchLatest) {
        fetchLatestShotAsync();
    }
    QTimer::singleShot(0, this, [this] {
        if (shotEdit_) {
            shotEdit_->clearFocus();
        }
        if (gridHost_) {
            gridHost_->setFocus(Qt::OtherFocusReason);
        }
    });
    return true;
}

void MainWindow::rebuildGrid()
{
    const int previousRows = gridLayout_->rowCount();
    const int previousColumns = gridLayout_->columnCount();
    while (QLayoutItem* item = gridLayout_->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    for (int r = 0; r < previousRows; ++r) {
        gridLayout_->setRowStretch(r, 0);
    }
    for (int c = 0; c < previousColumns; ++c) {
        gridLayout_->setColumnStretch(c, 0);
    }
    activePointPlot_ = nullptr;
    pointSyncSource_ = nullptr;
    pointSyncQueued_ = false;
    singlePanelMaximized_ = false;
    maximizedColumn_ = -1;
    maximizedRow_ = -1;
    if (globalPointOverlay_) {
        globalPointOverlay_->clearReadouts();
    }
    syncDisplayConfig();
    plotWidgets_.clear();
    plotWidgets_.resize(displayConfig_.columns.size());

    for (int c = 0; c < displayConfig_.columns.size(); ++c) {
        auto* columnHost = new QWidget(gridHost_);
        columnHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto* columnLayout = new QVBoxLayout(columnHost);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(0);
        plotWidgets_[c].resize(displayConfig_.columns[c].size());
        for (int r = 0; r < displayConfig_.columns[c].size(); ++r) {
            auto* plot = new PlotWidget(columnHost);
            plot->setSpec(displayConfig_.columns[c][r]);
            plot->setLargeDisplayMode(false);
            if (zoomButton_ && zoomButton_->isChecked()) {
                plot->setInteractionMode(InteractionMode::Zoom);
            } else {
                plot->setInteractionMode(InteractionMode::Point);
            }
            plot->setContextMenuPolicy(Qt::CustomContextMenu);
            plotWidgets_[c][r] = plot;
            columnLayout->addWidget(plot, 1);
            connect(plot, &QWidget::customContextMenuRequested, this, [this, plot, c, r](const QPoint& pos) {
                showPanelContextMenu(plot, c, r, pos);
            });
            connect(plot, &PlotWidget::selected, this, [this, plot, c, r] {
                selectPlot(c, r);
                if (pointButton_ && pointButton_->isChecked()) {
                    activePointPlot_ = plot;
                }
            });
            connect(plot, &PlotWidget::pointTrackingStopped, this, [this, plot] {
                if (activePointPlot_ == plot) {
                    activePointPlot_ = nullptr;
                }
                pointSyncSource_ = nullptr;
                pointSyncQueued_ = false;
                pendingPointX_ = qQNaN();
                ++pointSyncGeneration_;
            });
            connect(plot, &PlotWidget::pointXChanged, this, [this, plot](double x) {
                if (!(pointButton_ && pointButton_->isChecked())) {
                    return;
                }
                if (!std::isfinite(x)) {
                    if (activePointPlot_ == plot) {
                        activePointPlot_ = nullptr;
                    }
                    pointSyncSource_ = nullptr;
                    pointSyncQueued_ = false;
                    pendingPointX_ = qQNaN();
                    for (auto& col : plotWidgets_) {
                        for (PlotWidget* other : col) {
                            if (other) {
                                other->clearSyncedPoint();
                            }
                        }
                    }
                    return;
                }
                activePointPlot_ = plot;
                schedulePointSync(plot, x);
            });
        }
        gridLayout_->addWidget(columnHost, 0, c);
        gridLayout_->setColumnStretch(c, 1);
    }
    gridLayout_->setRowStretch(0, 1);
    gridHost_->updateGeometry();
}

void MainWindow::syncDisplayConfig()
{
    displayConfig_ = expandedShotLayout(config_);
    for (int c = 0; c < plotWidgets_.size() && c < displayConfig_.columns.size(); ++c) {
        for (int r = 0; r < plotWidgets_[c].size() && r < displayConfig_.columns[c].size(); ++r) {
            if (plotWidgets_[c][r]) {
                plotWidgets_[c][r]->setSpec(displayConfig_.columns[c][r]);
            }
        }
    }
}

void MainWindow::selectPlot(int column, int row)
{
    if (column < 0 || row < 0 || column >= plotWidgets_.size() || row >= plotWidgets_[column].size()) {
        return;
    }
    selectedColumn_ = column;
    selectedRow_ = row;
    for (int c = 0; c < plotWidgets_.size(); ++c) {
        for (int r = 0; r < plotWidgets_[c].size(); ++r) {
            plotWidgets_[c][r]->setSelected(c == column && r == row);
        }
    }
    const auto& plot = config_.columns[column][row];
    updateTopInfoLabels();
    setStatus(QString("Selected col %1 row %2: %3").arg(column + 1).arg(row + 1).arg(plot.title.isEmpty() ? plot.shot : plot.title));
}

void MainWindow::showPanelContextMenu(PlotWidget* plot, int column, int row, const QPoint& pos)
{
    if (!plot) {
        return;
    }
    selectPlot(column, row);

    QMenu menu(this);
    QAction* maxAction = menu.addAction("Max");
    QAction* showAllAction = menu.addAction("Show All Panels");
    showAllAction->setEnabled(singlePanelMaximized_);
    menu.addSeparator();
    QAction* panelSetupAction = menu.addAction("Panel Setup");
    QAction* dataSourceAction = menu.addAction("Data Source Setup");
    QAction* exportDataAction = menu.addAction("Export Data");
    menu.addSeparator();
    QAction* resetCurrentAction = menu.addAction("Reset Current Scale");
    QAction* resetAllAction = menu.addAction("Reset All Panels");
    QAction* sameXAction = menu.addAction("All Same X Scale");
    QAction* sameYAction = menu.addAction("All Same Y Scale");

    const QAction* chosen = menu.exec(plot->mapToGlobal(pos));
    if (!chosen) {
        return;
    }
    if (chosen == maxAction) {
        maximizeCurrentPanel();
    } else if (chosen == showAllAction) {
        showAllPanels();
    } else if (chosen == panelSetupAction) {
        panelSetupForCurrentPanel();
    } else if (chosen == dataSourceAction) {
        dataSourceSetupForCurrentPanel();
    } else if (chosen == exportDataAction) {
        exportCurrentPanelData();
    } else if (chosen == resetCurrentAction) {
        resetCurrentScale();
    } else if (chosen == resetAllAction) {
        resetScales();
    } else if (chosen == sameXAction) {
        applyScaleToAll();
    } else if (chosen == sameYAction) {
        applyYScaleToAll();
    }
}

void MainWindow::openLayoutSetupDialog()
{
    if (config_.columns.isEmpty()) {
        config_.columns.resize(1);
    }

    LayoutSetupDialog dialog(config_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    auto makePanel = [this] {
        PlotSpec plot = defaultPlotFromSelection();
        plot.title.clear();
        plot.signalSpecs.clear();
        if (shotEdit_ && !shotEdit_->text().trimmed().isEmpty()) {
            plot.shot = shotEdit_->text().trimmed();
        }
        plot.customXRange = false;
        plot.customYRange = false;
        plot.xmin = qQNaN();
        plot.xmax = qQNaN();
        plot.ymin = qQNaN();
        plot.ymax = qQNaN();
        return plot;
    };

    const QVector<QVector<LayoutCanvas::Item>> layout = dialog.layoutItems();
    if (layoutItemsMatchConfig(layout, config_)) {
        setStatus("Layout unchanged");
        return;
    }

    LayoutConfig next = config_;
    next.columns.clear();
    int selectColumn = -1;
    int selectRow = -1;
    int newPanels = 0;
    int keptPanels = 0;

    for (const auto& column : layout) {
        QVector<PlotSpec> nextColumn;
        for (const LayoutCanvas::Item& item : column) {
            if (item.isNew) {
                nextColumn.push_back(makePanel());
                if (selectColumn < 0) {
                    selectColumn = next.columns.size();
                    selectRow = nextColumn.size() - 1;
                }
                ++newPanels;
                continue;
            }
            if (item.originalColumn >= 0
                && item.originalColumn < config_.columns.size()
                && item.originalRow >= 0
                && item.originalRow < config_.columns[item.originalColumn].size()) {
                nextColumn.push_back(config_.columns[item.originalColumn][item.originalRow]);
                ++keptPanels;
            }
        }
        if (!nextColumn.isEmpty()) {
            next.columns.push_back(std::move(nextColumn));
        }
    }

    if (next.columns.isEmpty()) {
        next.columns.resize(1);
    }
    if (selectColumn < 0) {
        selectColumn = selectedColumn_ >= 0 ? selectedColumn_ : 0;
        selectColumn = std::clamp(selectColumn, 0, std::max(0, static_cast<int>(next.columns.size()) - 1));
        selectRow = next.columns[selectColumn].isEmpty()
            ? -1
            : std::clamp(selectedRow_ >= 0 ? selectedRow_ : 0,
                         0,
                         static_cast<int>(next.columns[selectColumn].size()) - 1);
    }
    const int oldPanels = std::accumulate(config_.columns.begin(), config_.columns.end(), 0, [](int total, const QVector<PlotSpec>& column) {
        return total + column.size();
    });
    const int deletedPanels = std::max(0, oldPanels - keptPanels);
    config_ = std::move(next);
    setStatus(QString("Layout updated: %1 deleted, %2 inserted").arg(deletedPanels).arg(newPanels));

    rebuildGrid();
    if (selectColumn >= 0 && selectColumn < plotWidgets_.size()
        && selectRow >= 0 && selectRow < plotWidgets_[selectColumn].size()) {
        selectPlot(selectColumn, selectRow);
    }
    const QString currentShot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    if (!currentShot.isEmpty()) {
        setAllPlotShots(currentShot);
    }
    refreshData();
}

void MainWindow::openCustomizeDialog()
{
    FontSettings& fonts = fontSettings();
    QDialog dialog(this);
    dialog.setWindowTitle("Customize Fonts");
    if (QApplication::palette().color(QPalette::Window).lightness() >= 128) {
        dialog.setStyleSheet(
            "QDialog { background: #f6f6f6; color: #111827; }"
            "QLabel { background: transparent; color: #111827; }"
            "QSpinBox, QFontComboBox {"
            "  background: #ffffff;"
            "  color: #111827;"
            "  border: 1px solid #cbd5e1;"
            "  border-radius: 3px;"
            "  padding: 3px 6px;"
            "  selection-background-color: #2563eb;"
            "  selection-color: #ffffff;"
            "}"
            "QSpinBox:focus, QFontComboBox:focus { border-color: #2563eb; }"
            "QSpinBox::up-button, QSpinBox::down-button { background: transparent; border: none; width: 16px; }");
    }
    auto* layout = new QFormLayout(&dialog);
    auto* family = new QFontComboBox(&dialog);
    family->setCurrentFont(QFont(fonts.family));
    auto* legendSize = new QSpinBox(&dialog);
    auto* axisSize = new QSpinBox(&dialog);
    auto* unitSize = new QSpinBox(&dialog);
    auto* uiSize = new QSpinBox(&dialog);
    for (QSpinBox* box : {legendSize, axisSize, unitSize, uiSize}) {
        box->setRange(6, 28);
        box->setSingleStep(1);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    }
    legendSize->setValue(fonts.legendSize);
    axisSize->setValue(fonts.axisSize);
    unitSize->setValue(fonts.unitSize);
    uiSize->setValue(fonts.uiSize);
    layout->addRow("Font", family);
    layout->addRow("Legend size", legendSize);
    layout->addRow("Axis size", axisSize);
    layout->addRow("Unit size", unitSize);
    layout->addRow("UI size", uiSize);
    auto* buttons = new QHBoxLayout();
    auto* ok = new QPushButton("OK", &dialog);
    auto* cancel = new QPushButton("Cancel", &dialog);
    buttons->addStretch(1);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    layout->addRow(buttons);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    fonts.family = family->currentFont().family();
    fonts.legendSize = legendSize->value();
    fonts.axisSize = axisSize->value();
    fonts.unitSize = unitSize->value();
    fonts.uiSize = uiSize->value();
    saveFontSettings(rootPath_);
    applyUiFont();
    refreshPlotFonts();
}

void MainWindow::applyUiFont()
{
    const FontSettings& fonts = fontSettings();
    QFont uiFont(fonts.family, fonts.uiSize);
    if (QApplication::font() != uiFont) {
        QApplication::setFont(uiFont);
    }
    if (font() != uiFont) {
        setFont(uiFont);
    }
    for (QWidget* widget : findChildren<QWidget*>()) {
        if (!qobject_cast<PlotWidget*>(widget) && widget->font() != uiFont) {
            widget->setFont(uiFont);
        }
    }
    if (statusLabel_ && statusLabel_->font() != uiFont) {
        statusLabel_->setFont(uiFont);
    }
    for (QLabel* label : {topInfoLabel_, ipInfoLabel_, pulseInfoLabel_, itInfoLabel_, timeInfoLabel_}) {
        if (label && label->font() != uiFont) {
            label->setFont(uiFont);
        }
    }
}

void MainWindow::refreshPlotFonts()
{
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot) {
                plot->refreshStyle();
            }
        }
    }
}

void MainWindow::setInteractionMode(InteractionMode mode)
{
    if (currentInteractionMode_ == mode
        && zoomButton_ && zoomButton_->isChecked() == (mode == InteractionMode::Zoom)
        && pointButton_ && pointButton_->isChecked() == (mode == InteractionMode::Point)) {
        return;
    }
    currentInteractionMode_ = mode;
    if (zoomButton_) {
        zoomButton_->setChecked(mode == InteractionMode::Zoom);
        pointButton_->setChecked(mode == InteractionMode::Point);
        zoomButton_->setIcon(modeIcon(InteractionMode::Zoom, mode == InteractionMode::Zoom));
        pointButton_->setIcon(modeIcon(InteractionMode::Point, mode == InteractionMode::Point));
    }
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            plot->setInteractionMode(mode);
        }
    }
    if (mode != InteractionMode::Point && globalPointOverlay_) {
        globalPointOverlay_->clearReadouts();
    }
}

void MainWindow::applyShot()
{
    if (!shotEdit_) {
        return;
    }
    const QString shot = shotEdit_->text().trimmed();
    if (shot.isEmpty()) {
        return;
    }
    if (shotEdit_->text() != shot) {
        shotEdit_->setText(shot);
    }
    rememberShotExpression(shot);
    ++latestShotGeneration_;
    setAllPlotShots(shot);
    refreshData();
}

void MainWindow::stepShot(int delta)
{
    if (!shotEdit_) {
        return;
    }
    bool ok = false;
    const int shot = shotEdit_->text().trimmed().toInt(&ok);
    if (!ok) {
        return;
    }
    const QString nextShot = QString::number(std::max(0, shot + delta));
    if (shotEdit_->text() != nextShot) {
        shotEdit_->setText(nextShot);
    }
    applyShot();
}

void MainWindow::latestShot()
{
    if (!latestShot_.isEmpty()) {
        if (shotEdit_->text() != latestShot_) {
            shotEdit_->setText(latestShot_);
        }
        applyShot();
        return;
    }
    fetchLatestShotAsync();
}

void MainWindow::openLoginDialog()
{
    LoginDialog dialog(rootPath_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    applyLoginSuccessStatus("Login token saved");
}

void MainWindow::openAboutDialog()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::applyLoginSuccessStatus(const QString& statusText)
{
    updateLoginActionIcon();
    latestShot_.clear();
    pendingTopSummaryShot_.clear();
    topSummaryShot_.clear();
    topSummaryIp_.clear();
    topSummaryPulse_.clear();
    topSummaryIt_.clear();
    topSummaryTime_.clear();
    ++topSummaryGeneration_;
    updateTopInfoLabels();
    setStatus(statusText);
}

void MainWindow::updateLoginActionIcon()
{
    if (!loginAction_) {
        return;
    }

    CachedAuth auth;
    const bool loggedIn = loadCachedAuth(&auth)
        && !auth.token.trimmed().isEmpty()
        && !tokenExpiresSoon(auth.token);
    loginAction_->setIcon(loginIcon(loggedIn));
}

void MainWindow::fetchLatestShotAsync()
{
    if (latestShotFetchRunning_) {
        return;
    }
    latestShotFetchRunning_ = true;
    const int generation = ++latestShotGeneration_;
    setStatus("Fetching latest shot...");
    QThreadPool::globalInstance()->start([this, generation] {
        const QString latest = latestShotFromApi();
        QMetaObject::invokeMethod(this, [this, latest, generation] {
            if (generation != latestShotGeneration_) {
                latestShotFetchRunning_ = false;
                return;
            }
            latestShotFetchRunning_ = false;
            if (latest.isEmpty()) {
                setStatus("Latest shot unavailable");
                return;
            }
            latestShot_ = latest;
            if (shotEdit_ && shotEdit_->text() != latestShot_) {
                shotEdit_->setText(latestShot_);
            }
            applyShot();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::updateShotControlsFromConfig(const QString& preferredShot)
{
    QString shot = preferredShot.trimmed();
    if (shot.isEmpty()) {
        shot = maxShotInConfig();
    }
    if (!shot.isEmpty()) {
        if (shotEdit_) {
            shotEdit_->setText(shot);
        }
        ++latestShotGeneration_;
        setAllPlotShots(shot);
    }
}

void MainWindow::setAllPlotShots(const QString& shot)
{
    bool changed = false;
    for (auto& col : config_.columns) {
        for (PlotSpec& plot : col) {
            if (plot.shot != shot) {
                plot.shot = shot;
                changed = true;
            }
        }
    }
    if (changed) {
        syncDisplayConfig();
    }
    updateTopInfoLabels();
    setStatus(QString("ShotNo:%1").arg(shot));
}

QString MainWindow::maxShotInConfig() const
{
    int best = -1;
    QString bestText;
    for (const auto& col : config_.columns) {
        for (const PlotSpec& plot : col) {
            bool ok = false;
            const int shot = plot.shot.trimmed().toInt(&ok);
            if (ok && shot > best) {
                best = shot;
                bestText = plot.shot.trimmed();
            }
        }
    }
    return bestText;
}

QString MainWindow::latestShotFromApi() const
{
    const auto properties = readApiSettings(rootPath_);
    const QString api = properties.value("ApiUrl");
    const QString token = properties.value("Token");
    const QString prefix = properties.value("Authorization_Prefix", properties.value("Init_Prefix", "Bearer"));
    const QString charset = properties.value("Charset", "UTF-8");
    if (api.isEmpty() || token.isEmpty()) {
        return {};
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(api + "/treeShot"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=" + charset);
    request.setRawHeader("Authorization", (prefix + " " + token).toUtf8());
    request.setRawHeader("User-Agent", "MdsScope/0.1");
    request.setTransferTimeout(4000);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply = manager.post(request, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(4000);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        return {};
    }

    const QByteArray body = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error != QNetworkReply::NoError || body.isEmpty()) {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        QString shot = firstShotFromJsonValue(obj.value("data"));
        if (!shot.isEmpty()) {
            return shot;
        }
        shot = firstShotFromJsonValue(obj);
        if (!shot.isEmpty()) {
            return shot;
        }
    } else if (doc.isArray()) {
        const QString shot = firstShotFromJsonValue(doc.array());
        if (!shot.isEmpty()) {
            return shot;
        }
    }
    return firstShotLikeText(QString::fromUtf8(body));
}

bool MainWindow::loadShotSummaryFromApi(const QString& shot,
                                        QString* ip,
                                        QString* pulse,
                                        QString* it,
                                        QString* time) const
{
    const auto properties = readApiSettings(rootPath_);
    const QString api = properties.value("ApiUrl");
    const QString token = properties.value("Token");
    const QString prefix = properties.value("Authorization_Prefix", properties.value("Init_Prefix", "Bearer"));
    const QString charset = properties.value("Charset", "UTF-8");
    if (api.isEmpty() || token.isEmpty() || shot.trimmed().isEmpty()) {
        return false;
    }

    bool shotOk = false;
    const int shotNumber = shot.trimmed().toInt(&shotOk);
    if (!shotOk) {
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl(api + "/pcsEastTree"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=" + charset);
    request.setRawHeader("Authorization", (prefix + " " + token).toUtf8());
    request.setRawHeader("User-Agent", "MdsScope/0.1");
    request.setTransferTimeout(2500);

    QJsonObject payload;
    payload.insert("treeshot", shotNumber);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(2500);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        return false;
    }

    const QByteArray body = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error != QNetworkReply::NoError || body.isEmpty()) {
        return false;
    }

    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QString code = root.value("code").isString()
        ? root.value("code").toString()
        : QString::number(root.value("code").toInt());
    if (code != "20000") {
        return false;
    }
    const QJsonObject data = root.value("data").toObject();
    if (data.isEmpty()) {
        return false;
    }

    auto scalarText = [](const QJsonValue& value) {
        if (value.isString()) {
            return value.toString().trimmed();
        }
        if (value.isDouble()) {
            return QString::number(value.toDouble(), 'g', 8);
        }
        if (value.isBool()) {
            return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        }
        return QString();
    };

    if (ip) {
        *ip = scalarText(data.value("pcrl01"));
    }
    if (pulse) {
        *pulse = scalarText(data.value("shot_len"));
    }
    if (it) {
        *it = scalarText(data.value("iv"));
    }
    if (time) {
        *time = scalarText(data.value("curr_time"));
    }
    return true;
}

namespace {
QString signalKey(int column, int row, int signal)
{
    return QString::number(column) + ',' + QString::number(row) + ',' + QString::number(signal);
}
}

void MainWindow::refreshData()
{
    cancelPrewarmConnections();
    // Any normal refresh invalidates a pending "Continue": the shot, config or
    // read mode may have changed, so resuming the old fetch no longer applies.
    clearDataPause();
    syncDisplayConfig();
    const DataReadMode readMode = dataModeCombo_
                                      ? static_cast<DataReadMode>(dataModeCombo_->currentData().toInt())
                                      : DataReadMode::Thin;
    const QString key = refreshKey(readMode);
    if (runningDataFetches_ > 0) {
        if (key == activeRefreshKey_ || key == queuedRefreshKey_) {
            setStatus("Data refresh already running for current shot");
            return;
        }
        cancelDataFetch();
        pendingRefresh_ = false;
        queuedRefreshKey_.clear();
        queuedLoadedSignals_.clear();
        queuedLoadedSignalApply_ = false;
        for (auto& col : plotWidgets_) {
            for (PlotWidget* plot : col) {
                plot->clearSeries();
            }
        }
        attemptedSignals_.clear();
        streamedOk_ = 0;
        streamedFailed_ = 0;
        launchDataFetch(displayConfig_, readMode, key);
        return;
    }
    pendingRefresh_ = false;
    queuedLoadedSignals_.clear();
    queuedLoadedSignalApply_ = false;
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            plot->clearSeries();
        }
    }
    attemptedSignals_.clear();
    streamedOk_ = 0;
    streamedFailed_ = 0;
    launchDataFetch(displayConfig_, readMode, key);
}

void MainWindow::launchDataFetch(const LayoutConfig& snapshot, DataReadMode readMode, const QString& key)
{
    activeFetchSnapshot_ = snapshot;
    activeFetchReadMode_ = readMode;
    activeRefreshKey_ = key;
    queuedRefreshKey_.clear();
    const int generation = ++activeDataFetchGeneration_;
    dataCancel_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = dataCancel_;
    setStatus(readMode == DataReadMode::Full ? "Fetching MDS data (full)..."
                  : readMode == DataReadMode::Medium ? "Fetching MDS data (medium)..."
                  : "Fetching MDS data (thin)...");
    auto* watcher = new QFutureWatcher<QVector<LoadedSignal>>(this);
    ++runningDataFetches_;
    connect(watcher, &QFutureWatcher<QVector<LoadedSignal>>::finished, this, [this, watcher, key, generation] {
        const QVector<LoadedSignal> loaded = watcher->result();
        watcher->deleteLater();
        runningDataFetches_ = std::max(0, runningDataFetches_ - 1);
        if (generation != activeDataFetchGeneration_ || key != activeRefreshKey_) {
            return;
        }
        applyLoadedSignals(loaded);
    });
    watcher->setFuture(QtConcurrent::run([this, snapshot, readMode, key, cancel] {
        auto loaded = fetchMdsSignals(snapshot, readMode, [this, key, cancel](const LoadedSignal& item) {
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                return;
            }
            QMetaObject::invokeMethod(this, [this, key, item, cancel] {
                if ((!cancel || !cancel->load(std::memory_order_relaxed)) && key == activeRefreshKey_) {
                    queueLoadedSignal(item);
                }
            }, Qt::QueuedConnection);
        }, cancel);
        return loaded;
    }));
}

void MainWindow::onStopOrContinue()
{
    if (dataRefreshPaused_) {
        resumeDataRefresh();
    } else {
        stopDataRefresh();
    }
}

int MainWindow::countRemainingSignals(const LayoutConfig& snapshot) const
{
    int remaining = 0;
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            const PlotSpec& plot = snapshot.columns[c][r];
            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                if (plot.signalSpecs[s].hidden) {
                    continue;
                }
                if (!attemptedSignals_.contains(signalKey(c, r, s))) {
                    ++remaining;
                }
            }
        }
    }
    return remaining;
}

void MainWindow::stopDataRefresh()
{
    const bool wasRunning = runningDataFetches_ > 0;
    cancelDataFetch();
    cancelPanelFetch();
    const int remaining = countRemainingSignals(activeFetchSnapshot_);
    const QString activeKey = activeRefreshKey_;
    const DataReadMode activeMode = activeFetchReadMode_;
    activeRefreshKey_.clear();
    queuedRefreshKey_.clear();
    pendingRefresh_ = false;
    pendingResume_ = false;
    activePanelRefreshKey_.clear();
    queuedPanelRefreshKey_.clear();
    pendingPanelRefresh_ = false;
    queuedLoadedSignals_.clear();
    queuedLoadedSignalApply_ = false;
    if (wasRunning && remaining > 0) {
        pausedSnapshot_ = activeFetchSnapshot_;
        pausedReadMode_ = activeMode;
        pausedKey_ = activeKey;
        setStopButtonPaused(true);
        setStatus(QString("Data refresh paused: %1 signals remaining").arg(remaining));
    } else {
        clearDataPause();
        setStatus(wasRunning ? "Data refresh stopped" : "No data refresh running");
    }
}

void MainWindow::resumeDataRefresh()
{
    if (!dataRefreshPaused_) {
        return;
    }
    // Fetch only signals that were never attempted; mark the already-attempted
    // ones hidden so fetchMdsSignals skips them while keeping signal indices
    // stable (item.signal is the raw slot index).
    LayoutConfig remainingSnapshot = pausedSnapshot_;
    for (int c = 0; c < remainingSnapshot.columns.size(); ++c) {
        for (int r = 0; r < remainingSnapshot.columns[c].size(); ++r) {
            PlotSpec& plot = remainingSnapshot.columns[c][r];
            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                if (attemptedSignals_.contains(signalKey(c, r, s))) {
                    plot.signalSpecs[s].hidden = true;
                }
            }
        }
    }
    const DataReadMode readMode = pausedReadMode_;
    const QString key = pausedKey_;
    clearDataPause();
    if (runningDataFetches_ > 0) {
        // Old (cancelled) fetch is still winding down; queue the resume so we
        // do not oversubscribe the thread pool with a second fetch.
        cancelDataFetch();
        pendingResume_ = true;
        pendingResumeSnapshot_ = remainingSnapshot;
        pausedReadMode_ = readMode;
        pausedKey_ = key;
        activeRefreshKey_.clear();
        queuedLoadedSignals_.clear();
        queuedLoadedSignalApply_ = false;
        setStatus("Resuming after current fetch stops...");
        return;
    }
    setStatus("Resuming data fetch...");
    launchDataFetch(remainingSnapshot, readMode, key);
}

void MainWindow::clearDataPause()
{
    pendingResume_ = false;
    if (dataRefreshPaused_) {
        setStopButtonPaused(false);
    }
}

void MainWindow::setStopButtonPaused(bool paused)
{
    dataRefreshPaused_ = paused;
    if (stopButton_) {
        stopButton_->setText(paused ? "Continue" : "Stop");
    }
}

void MainWindow::cancelDataFetch()
{
    if (dataCancel_) {
        dataCancel_->store(true, std::memory_order_relaxed);
    }
}

void MainWindow::cancelPanelFetch()
{
    if (panelCancel_) {
        panelCancel_->store(true, std::memory_order_relaxed);
    }
}

void MainWindow::cancelPrewarmConnections()
{
    if (warmCancel_) {
        warmCancel_->store(true, std::memory_order_relaxed);
    }
}

void MainWindow::prewarmConnections()
{
    if (config_.columns.isEmpty()) {
        return;
    }
    if (warmWatcher_.isRunning()) {
        return;
    }

    const LayoutConfig snapshot = expandedShotLayout(config_);
    warmCancel_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = warmCancel_;
    warmWatcher_.setFuture(QtConcurrent::run([snapshot, cancel] {
        warmMdsConnections(snapshot, cancel);
    }));
}

QString MainWindow::refreshKey(DataReadMode readMode) const
{
    const QString shot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    return QString("%1|%2|%3")
        .arg(shot)
        .arg(readMode == DataReadMode::Full ? "full" : "thin")
        .arg(layoutRefreshSignature(config_));
}

QString MainWindow::panelRefreshKey(int column, int row, int signal, DataReadMode readMode) const
{
    QString panelSignature;
    if (column >= 0 && row >= 0
        && column < config_.columns.size()
        && row < config_.columns[column].size()) {
        panelSignature = plotRefreshSignature(config_.columns[column][row]);
    }
    return QString("%1|%2|%3|%4|%5")
        .arg(column)
        .arg(row)
        .arg(signal)
        .arg(readMode == DataReadMode::Full ? "full" : "thin")
        .arg(panelSignature);
}

void MainWindow::refreshOne(int column, int row, int signal)
{
    if (column < 0 || row < 0 || column >= config_.columns.size() || row >= config_.columns[column].size()) {
        return;
    }
    if (column >= plotWidgets_.size() || row >= plotWidgets_[column].size()) {
        return;
    }

    const DataReadMode readMode = dataModeCombo_
                                      ? static_cast<DataReadMode>(dataModeCombo_->currentData().toInt())
                                      : DataReadMode::Thin;
    const QString key = panelRefreshKey(column, row, signal, readMode);

    activeRefreshKey_.clear();
    pendingRefresh_ = false;
    queuedRefreshKey_.clear();
    queuedLoadedSignals_.clear();
    queuedLoadedSignalApply_ = false;

    if (panelWatcher_.isRunning()) {
        if (key == activePanelRefreshKey_ || key == queuedPanelRefreshKey_) {
            setStatus(QString("Panel refresh already running: col %1 row %2").arg(column + 1).arg(row + 1));
            return;
        }
        if (panelCancel_) {
            cancelPanelFetch();
        }
        pendingPanelRefresh_ = true;
        pendingPanelColumn_ = column;
        pendingPanelRow_ = row;
        pendingPanelSignal_ = signal;
        queuedPanelRefreshKey_ = key;
        activePanelRefreshKey_.clear();
        setStatus(QString("Panel refresh queued: col %1 row %2").arg(column + 1).arg(row + 1));
        return;
    }

    syncDisplayConfig();
    LayoutConfig snapshot = displayConfig_;
    const bool singleSignalRefresh = false;
    Q_UNUSED(signal);
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            if (c == column && r == row) {
                continue;
            }
            snapshot.columns[c][r].signalSpecs.clear();
        }
    }
    if (!singleSignalRefresh) {
        plotWidgets_[column][row]->clearSeries();
    }
    activePanelRefreshKey_ = key;
    queuedPanelRefreshKey_.clear();
    panelCancel_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = panelCancel_;
    setStatus(singleSignalRefresh
                  ? QString("Fetching signal data: col %1 row %2 source %3").arg(column + 1).arg(row + 1).arg(signal + 1)
                  : QString("Fetching panel data: col %1 row %2").arg(column + 1).arg(row + 1));
    panelWatcher_.setFuture(QtConcurrent::run([snapshot, readMode, singleSignalRefresh, cancel] {
        QVector<LoadedSignal> loaded = fetchMdsSignals(snapshot, readMode, {}, cancel);
        Q_UNUSED(singleSignalRefresh);
        return loaded;
    }));
}

void MainWindow::queueLoadedSignal(const LoadedSignal& item)
{
    queuedLoadedSignals_.push_back(item);
    if (queuedLoadedSignalApply_) {
        return;
    }
    queuedLoadedSignalApply_ = true;
    QTimer::singleShot(16, this, [this] {
        flushQueuedLoadedSignals();
    });
}

void MainWindow::flushQueuedLoadedSignals()
{
    if (queuedLoadedSignals_.isEmpty()) {
        queuedLoadedSignalApply_ = false;
        return;
    }
    QVector<LoadedSignal> batch;
    batch.swap(queuedLoadedSignals_);
    queuedLoadedSignalApply_ = false;
    for (const LoadedSignal& item : std::as_const(batch)) {
        applyLoadedSignal(item);
    }
}

void MainWindow::applyLoadedSignal(const LoadedSignal& item)
{
    if (item.column < 0 || item.row < 0 || item.column >= plotWidgets_.size() || item.row >= plotWidgets_[item.column].size()) {
        return;
    }
    if (item.column >= displayConfig_.columns.size()
        || item.row >= displayConfig_.columns[item.column].size()
        || !loadedSignalMatchesConfig(displayConfig_, item)) {
        return;
    }

    plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
    // Record every delivered slot (loaded or failed) so a later Continue only
    // re-fetches signals that were never attempted.
    attemptedSignals_.insert(signalKey(item.column, item.row, item.signal));
    if (!item.series.hasData()) {
        ++streamedFailed_;
    } else {
        ++streamedOk_;
        rememberLoadedSourceSignal(item);
    }
    const int streamedTotal = streamedOk_ + streamedFailed_;
    if (streamedTotal == 1 || streamedTotal % 8 == 0) {
        setStatus(QString("Data refresh: %1 signals loaded, %2 failed...").arg(streamedOk_).arg(streamedFailed_));
    }
}

void MainWindow::applyLoadedSignals(const QVector<LoadedSignal>& loaded)
{
    flushQueuedLoadedSignals();
    if (activeRefreshKey_.isEmpty()) {
        if (pendingResume_) {
            pendingResume_ = false;
            launchDataFetch(pendingResumeSnapshot_, pausedReadMode_, pausedKey_);
            return;
        }
        if (pendingRefresh_) {
            pendingRefresh_ = false;
            refreshData();
        }
        return;
    }
    if (pendingRefresh_) {
        pendingRefresh_ = false;
        activeRefreshKey_.clear();
        refreshData();
        return;
    }
    activeRefreshKey_.clear();
    if (streamedOk_ + streamedFailed_ >= loaded.size()) {
        setStatus(QString("Data refresh done: %1 signals loaded, %2 failed").arg(streamedOk_).arg(streamedFailed_));
        if (startupPrewarmPending_) {
            startupPrewarmPending_ = false;
            prewarmConnections();
        }
        return;
    }

    int ok = 0;
    int failed = 0;
    for (const LoadedSignal& item : loaded) {
        if (item.column < plotWidgets_.size() && item.row < plotWidgets_[item.column].size()) {
            if (!loadedSignalMatchesConfig(displayConfig_, item)) {
                continue;
            }
            plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
            if (!item.series.hasData()) {
                ++failed;
            } else {
                ++ok;
                rememberLoadedSourceSignal(item);
            }
        }
    }
    setStatus(QString("Data refresh done: %1 signals loaded, %2 failed").arg(ok).arg(failed));
    if (startupPrewarmPending_) {
        startupPrewarmPending_ = false;
        prewarmConnections();
    }
}

void MainWindow::applyPanelLoadedSignals(const QVector<LoadedSignal>& loaded)
{
    int ok = 0;
    int failed = 0;
    for (const LoadedSignal& item : loaded) {
        if (item.column < 0 || item.row < 0
            || item.column >= plotWidgets_.size()
            || item.row >= plotWidgets_[item.column].size()
            || !loadedSignalMatchesConfig(displayConfig_, item)) {
            continue;
        }
        plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
        if (item.series.hasData()) {
            ++ok;
            rememberLoadedSourceSignal(item);
        } else {
            ++failed;
        }
    }
    setStatus(QString("Panel refresh done: %1 signals loaded, %2 failed").arg(ok).arg(failed));
}

void MainWindow::rememberLoadedSourceSignal(const LoadedSignal& item)
{
    if (!item.series.hasData()
        || item.column < 0 || item.row < 0 || item.signal < 0
        || item.column >= displayConfig_.columns.size()
        || item.row >= displayConfig_.columns[item.column].size()
        || item.signal >= displayConfig_.columns[item.column][item.row].signalSpecs.size()) {
        return;
    }
    const SignalSpec& sig = displayConfig_.columns[item.column][item.row].signalSpecs[item.signal];
    const QString tree = sig.experiment.trimmed().toLower();
    const QString signal = normalizedMdsSignal(sig.yExpr).trimmed().toLower();
    if (tree.isEmpty() || signal.isEmpty()) {
        return;
    }
    const QString key = tree + QChar('\n') + signal;
    if (rememberedSourceSignals_.contains(key)) {
        return;
    }
    rememberedSourceSignals_.insert(key);
    addSourceIndexSignal(sig.experiment, sig.yExpr);
}

PlotSpec MainWindow::defaultPlotFromSelection() const
{
    if (selectedColumn_ >= 0 && selectedRow_ >= 0 &&
        selectedColumn_ < config_.columns.size() && selectedRow_ < config_.columns[selectedColumn_].size()) {
        PlotSpec plot = config_.columns[selectedColumn_][selectedRow_];
        plot.title.clear();
        plot.signalSpecs.clear();
        return plot;
    }
    PlotSpec plot;
    plot.shot = "0";
    return plot;
}

void MainWindow::addPlotBelow()
{
    if (config_.columns.isEmpty()) {
        config_.columns.resize(1);
    }
    const int column = selectedColumn_ >= 0 ? selectedColumn_ : 0;
    const int row = selectedRow_ >= 0 ? selectedRow_ + 1 : config_.columns[column].size();
    SignalDialog dialog(defaultPlotFromSelection(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    PlotSpec plot = defaultPlotFromSelection();
    plot.shot = dialog.shot();
    SignalSpec sig = dialog.signal();
    if (sig.yExpr.isEmpty()) {
        QMessageBox::warning(this, "Add Plot", "Y expr is required.");
        return;
    }
    plot.title = sig.yExpr;
    plot.signalSpecs.push_back(sig);
    config_.columns[column].insert(row, plot);
    rebuildGrid();
    selectPlot(column, row);
    refreshOne(column, row, 0);
}

void MainWindow::deleteCurrentPlot()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0) {
        return;
    }
    config_.columns[selectedColumn_].removeAt(selectedRow_);
    const int newColumn = selectedColumn_;
    const int newRow = std::min(selectedRow_, static_cast<int>(config_.columns[newColumn].size()) - 1);
    rebuildGrid();
    if (newRow >= 0) {
        selectPlot(newColumn, newRow);
    }
    refreshData();
}

void MainWindow::addSignalToCurrentPlot()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0) {
        return;
    }
    PlotSpec& plot = config_.columns[selectedColumn_][selectedRow_];
    SignalDialog dialog(plot, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    SignalSpec sig = dialog.signal();
    if (sig.yExpr.isEmpty()) {
        QMessageBox::warning(this, "Add Signal", "Y expr is required.");
        return;
    }
    sig.colorName = colorForIndex(plot.signalSpecs.size());
    plot.shot = dialog.shot();
    plot.signalSpecs.push_back(sig);
    normalizePresetColors(plot.signalSpecs);
    syncDisplayConfig();
    refreshOne(selectedColumn_, selectedRow_, -1);
}

void MainWindow::deleteSignalFromCurrentPlot()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0) {
        return;
    }
    PlotSpec& plot = config_.columns[selectedColumn_][selectedRow_];
    if (plot.signalSpecs.isEmpty()) {
        return;
    }
    QStringList items;
    for (const auto& sig : plot.signalSpecs) {
        items.push_back(sig.yExpr);
    }
    bool ok = false;
    const QString choice = QInputDialog::getItem(this, "Delete Signal", "Signal", items, 0, false, &ok);
    if (!ok) {
        return;
    }
    const int idx = items.indexOf(choice);
    if (idx >= 0) {
        plot.signalSpecs.removeAt(idx);
        normalizePresetColors(plot.signalSpecs);
        syncDisplayConfig();
        refreshOne(selectedColumn_, selectedRow_, -1);
    }
}

void MainWindow::panelSetupForCurrentPanel()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0
        || selectedColumn_ >= config_.columns.size()
        || selectedRow_ >= config_.columns[selectedColumn_].size()
        || selectedColumn_ >= plotWidgets_.size()
        || selectedRow_ >= plotWidgets_[selectedColumn_].size()) {
        return;
    }

    PlotSpec& plot = config_.columns[selectedColumn_][selectedRow_];
    PanelSetupDialog dialog(plot, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    dialog.applyTo(&plot);
    displayConfig_ = expandedShotLayout(config_);
    plotWidgets_[selectedColumn_][selectedRow_]->setSpec(displayConfig_.columns[selectedColumn_][selectedRow_]);
    plotWidgets_[selectedColumn_][selectedRow_]->resetScale();
    updateTopInfoLabels();
    setStatus(QString("Updated panel setup: col %1 row %2").arg(selectedColumn_ + 1).arg(selectedRow_ + 1));
}

void MainWindow::dataSourceSetupForCurrentPanel()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0
        || selectedColumn_ >= config_.columns.size()
        || selectedRow_ >= config_.columns[selectedColumn_].size()
        || selectedColumn_ >= plotWidgets_.size()
        || selectedRow_ >= plotWidgets_[selectedColumn_].size()) {
        return;
    }

    PlotSpec& plot = config_.columns[selectedColumn_][selectedRow_];
    const QString currentShot = shotEdit_ ? shotEdit_->text().trimmed() : plot.shot;
    DataSourceDialog dialog(plot, currentShot, appSourceIndexDir(rootPath_), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<SignalSpec> specs = dialog.signalSpecs();
    if (specs.isEmpty()) {
        QMessageBox::warning(this, "Data Source Setup", "At least one signal is required.");
        return;
    }

    const bool dataSourceChanged = !signalDataSourcesEqual(plot.signalSpecs, specs);
    const bool specChanged = !signalSpecsEqual(plot.signalSpecs, specs);
    if (!specChanged) {
        setStatus(QString("Panel unchanged: col %1 row %2").arg(selectedColumn_ + 1).arg(selectedRow_ + 1));
        return;
    }
    plot.signalSpecs = std::move(specs);
    normalizePresetColors(plot.signalSpecs);
    plot.title = plot.signalSpecs.front().yExpr;
    if (dataSourceChanged) {
        plot.customXRange = false;
        plot.customYRange = false;
        plot.xmin = qQNaN();
        plot.xmax = qQNaN();
        plot.ymin = qQNaN();
        plot.ymax = qQNaN();
    }

    PlotWidget* widget = plotWidgets_[selectedColumn_][selectedRow_];
    const bool restoreView = !dataSourceChanged && widget->hasView();
    const QRectF previousView = restoreView ? widget->currentView() : QRectF();
    syncDisplayConfig();
    if (restoreView) {
        widget->applyView(previousView);
    }
    updateTopInfoLabels();
    if (dataSourceChanged) {
        refreshOne(selectedColumn_, selectedRow_, -1);
    } else {
        setStatus(QString("Updated panel style: col %1 row %2").arg(selectedColumn_ + 1).arg(selectedRow_ + 1));
    }
}

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

void MainWindow::applyScaleToAll()
{
    PlotWidget* current = currentPlotWidget();
    if (!current) {
        return;
    }
    const QRectF view = current->currentView();
    if (!view.isValid() || view.width() <= 0.0) {
        return;
    }
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot == current) {
                continue;
            }
            plot->applyXRangeAutoY(view.left(), view.right());
        }
    }
    setStatus("Applied current X scale to other panels");
}

void MainWindow::applyYScaleToAll()
{
    PlotWidget* current = currentPlotWidget();
    if (!current) {
        return;
    }
    const QRectF view = current->currentView();
    if (!view.isValid() || view.height() <= 0.0) {
        return;
    }
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot == current) {
                continue;
            }
            plot->applyYRangeKeepX(view.top(), view.bottom());
        }
    }
    setStatus("Applied current Y scale to other panels");
}

void MainWindow::resetCurrentScale()
{
    PlotWidget* current = currentPlotWidget();
    if (!current) {
        return;
    }
    if (selectedColumn_ >= 0 && selectedRow_ >= 0
        && selectedColumn_ < config_.columns.size()
        && selectedRow_ < config_.columns[selectedColumn_].size()) {
        clearCustomRanges(&config_.columns[selectedColumn_][selectedRow_]);
        displayConfig_ = expandedShotLayout(config_);
        if (selectedColumn_ < displayConfig_.columns.size()
            && selectedRow_ < displayConfig_.columns[selectedColumn_].size()) {
            current->setSpec(displayConfig_.columns[selectedColumn_][selectedRow_]);
        }
        updateTopInfoLabels();
        current = currentPlotWidget();
        if (!current) {
            return;
        }
    }
    current->resetScale();
    setStatus("Reset current panel to auto scale");
}

void MainWindow::resetScales()
{
    const bool updatesEnabled = gridHost_ ? gridHost_->updatesEnabled() : true;
    if (gridHost_ && updatesEnabled) {
        gridHost_->setUpdatesEnabled(false);
    }
    for (auto& col : config_.columns) {
        for (PlotSpec& plot : col) {
            clearCustomRanges(&plot);
        }
    }
    syncDisplayConfig();
    updateTopInfoLabels();
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot) {
                plot->resetScale(false);
            }
        }
    }
    if (gridHost_ && updatesEnabled) {
        gridHost_->setUpdatesEnabled(true);
    }
    for (auto& col : plotWidgets_) {
        for (PlotWidget* plot : col) {
            if (plot && plot->isVisible()) {
                plot->update();
            }
        }
    }
    setStatus("Reset all panels to auto scale");
}

void MainWindow::maximizeCurrentPanel()
{
    if (!currentPlotWidget()) {
        return;
    }
    singlePanelMaximized_ = true;
    maximizedColumn_ = selectedColumn_;
    maximizedRow_ = selectedRow_;
    for (int c = 0; c < plotWidgets_.size(); ++c) {
        QWidget* columnHost = plotWidgets_[c].isEmpty() ? nullptr : plotWidgets_[c].first()->parentWidget();
        if (columnHost) {
            columnHost->setVisible(c == maximizedColumn_);
            columnHost->setSizePolicy(c == maximizedColumn_ ? QSizePolicy::Expanding : QSizePolicy::Ignored,
                                      QSizePolicy::Expanding);
            if (auto* columnLayout = qobject_cast<QBoxLayout*>(columnHost->layout())) {
                for (int r = 0; r < plotWidgets_[c].size(); ++r) {
                    columnLayout->setStretch(r, c == maximizedColumn_ && r == maximizedRow_ ? 1 : 0);
                }
            }
        }
        gridLayout_->setColumnStretch(c, c == maximizedColumn_ ? 1 : 0);
        for (int r = 0; r < plotWidgets_[c].size(); ++r) {
            const bool visible = c == maximizedColumn_ && r == maximizedRow_;
            plotWidgets_[c][r]->setVisible(visible);
            plotWidgets_[c][r]->setLargeDisplayMode(visible);
        }
    }
    if (scrollArea_ && scrollArea_->viewport()) {
        gridHost_->setMinimumSize(scrollArea_->viewport()->size());
    }
    gridHost_->updateGeometry();
    setStatus(QString("Max panel col %1 row %2").arg(maximizedColumn_ + 1).arg(maximizedRow_ + 1));
}

void MainWindow::showAllPanels()
{
    singlePanelMaximized_ = false;
    maximizedColumn_ = -1;
    maximizedRow_ = -1;
    for (int c = 0; c < plotWidgets_.size(); ++c) {
        auto& col = plotWidgets_[c];
        QWidget* columnHost = col.isEmpty() ? nullptr : col.first()->parentWidget();
        if (columnHost) {
            columnHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            columnHost->show();
            if (auto* columnLayout = qobject_cast<QBoxLayout*>(columnHost->layout())) {
                for (int r = 0; r < col.size(); ++r) {
                    columnLayout->setStretch(r, 1);
                }
            }
        }
        gridLayout_->setColumnStretch(c, 1);
        for (PlotWidget* plot : col) {
            if (plot) {
                plot->setLargeDisplayMode(false);
                plot->show();
            }
        }
    }
    gridHost_->setMinimumSize(QSize(0, 0));
    gridHost_->updateGeometry();
    setStatus("Show all panels");
}

void MainWindow::saveCurrentEnvironment()
{
    if (config_.filePath.isEmpty()) {
        saveCurrentEnvironmentAs();
        return;
    }
    if (saveEnvironmentFile(config_.filePath)) {
        setStatus("Saved " + QFileInfo(config_.filePath).fileName());
    }
}

void MainWindow::saveCurrentEnvironmentAs()
{
    const QString path = QFileDialog::getSaveFileName(this, "Save MdsScope Config", rememberedFileDialogDir(), "MdsScope Config (*.toml)");
    if (path.isEmpty()) {
        return;
    }
    if (saveEnvironmentFile(path)) {
        const QFileInfo info(path);
        config_.filePath = info.suffix().isEmpty() ? path + ".toml" : path;
        rememberFileDialogDir(config_.filePath);
        loadEnvironmentFile(config_.filePath);
        setStatus("Saved " + QFileInfo(config_.filePath).fileName());
    }
}

bool MainWindow::saveEnvironmentFile(const QString& path) const
{
    QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    QString primaryPath = path;
    if (suffix != "toml" && suffix != "webscp") {
        primaryPath += ".toml";
        info = QFileInfo(primaryPath);
    }

    const QString baseName = info.completeBaseName();
    const QDir dir(info.absolutePath());
    const QString tomlPath = suffix == "toml" ? primaryPath : dir.filePath(baseName + ".toml");
    const QString webscpPath = suffix == "webscp" ? primaryPath : dir.filePath(baseName + ".webscp");

    QString tomlError;
    const bool tomlOk = writeEnvironmentToml(config_, tomlPath, &tomlError);
    const bool webscpOk = saveWebscpEnvironmentFile(webscpPath);

    if (!tomlOk && !webscpOk) {
        QMessageBox::warning(nullptr, "Save", "Cannot write TOML: " + tomlError + "\nCannot write webscp: " + webscpPath);
        return false;
    }
    if (!tomlOk) {
        QMessageBox::warning(nullptr, "Save", "Saved webscp, but TOML export failed: " + tomlError);
    }
    if (!webscpOk) {
        QMessageBox::warning(nullptr, "Save", "Saved TOML, but webscp export failed: " + webscpPath);
    }
    return tomlOk && webscpOk;
}

bool MainWindow::saveWebscpEnvironmentFile(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(nullptr, "Save", "Cannot write " + path);
        return false;
    }
    QTextStream out(&file);
    writeLine(out, "Title_Font", "java.awt.Font[family=Times New Roman,name=Times New Roman,style=plain,size=16]");
    writeLine(out, "Measurement_Units", "java.awt.Font[family=Times New Roman,name=Times New Roman,style=plain,size=14]");
    writeLine(out, "Coordinate_Axis", "java.awt.Font[family=Times New Roman,name=Times New Roman,style=plain,size=12]");
    writeLine(out, "Grid_Mode", "1");
    writeLine(out, "X_Lines", "5");
    writeLine(out, "Y_Lines", "5");
    writeLine(out, "Extraction_points", "2000");
    writeLine(out, "Vertical_offset", "0");
    writeLine(out, "Horizontal_offset", "0");
    writeLine(out, "xmax", "");
    writeLine(out, "xmin", "");
    writeLine(out, "ymax", "");
    writeLine(out, "ymin", "");
    writeLine(out, "File_position", QDir(rootPath_).filePath("data"));
    out << "\n \n";
    writeLine(out, "cols", QString::number(config_.columns.size()));
    out << " \n";
    for (int c = 0; c < config_.columns.size(); ++c) {
        writeLine(out, QString("%1.rows").arg(c + 1), QString::number(config_.columns[c].size()));
        for (int r = 0; r < config_.columns[c].size(); ++r) {
            const PlotSpec& plot = config_.columns[c][r];
            const QString p = QString("%1_%2.").arg(c + 1).arg(r + 1);
            writeLine(out, p + "shot_txt", plot.shot);
            writeLine(out, p + "num_shot", "1");
            writeLine(out, p + "num_sig", QString::number(plot.signalSpecs.size()));
            writeLine(out, p + "title_position", "0");
            writeLine(out, p + "y_log", "0");
            writeLine(out, p + "legend", "1");
            writeLine(out, p + "xseting_mode", plot.customXRange ? "0" : "1");
            writeLine(out, p + "yseting_mode", plot.customYRange ? "0" : "1");
            writeLine(out, p + "x_line_num", "5");
            writeLine(out, p + "y_line_num", "5");
            writeLine(out, p + "extraction_points", QString::number(plot.extractionPoints));
            writeLine(out, p + "vertical_offset", "0");
            writeLine(out, p + "horizontal_offset", "0");
            writeLine(out, p + "grid_mode", plot.grid ? "1" : "0");
            writeLine(out, p + "xmin_custom", std::isfinite(plot.xmin) ? QString::number(plot.xmin, 'g', 12) : "");
            writeLine(out, p + "xmax_custom", std::isfinite(plot.xmax) ? QString::number(plot.xmax, 'g', 12) : "");
            writeLine(out, p + "ymin_custom", std::isfinite(plot.ymin) ? QString::number(plot.ymin, 'g', 12) : "");
            writeLine(out, p + "ymax_custom", std::isfinite(plot.ymax) ? QString::number(plot.ymax, 'g', 12) : "");
            writeLine(out, p + "title", plot.title);
            writeLine(out, p + "xlabel", plot.xLabel);
            writeLine(out, p + "ylabel", plot.yLabel);
            for (int s = 0; s < plot.signalSpecs.size(); ++s) {
                const SignalSpec& sig = plot.signalSpecs[s];
                const bool defaultColor = !sig.manualColor || isDefaultSeriesColor(sig.colorName, s);
                const int colorIndex = defaultColor ? s : colorIndexForName(sig.colorName, s);
                writeLine(out, p + QString("color_%1_%2").arg(r + 1).arg(s + 1), QString::number(colorIndex));
                writeLine(out, p + QString("markers_%1_%2").arg(r + 1).arg(s + 1), "0");
                writeLine(out, p + QString("interpolate_%1_%2").arg(r + 1).arg(s + 1), "1");
                writeLine(out, p + QString("color_name_%1").arg(s + 1), defaultColor ? QString() : sig.colorName);
                writeLine(out, p + QString("color_manual_%1").arg(s + 1), defaultColor ? "0" : "1");
                writeLine(out, p + QString("shot_%1").arg(s + 1), sig.shot);
                writeLine(out, p + QString("y_expr_%1").arg(s + 1), sig.yExpr);
                writeLine(out, p + QString("x_expr_%1").arg(s + 1), sig.xExpr);
                writeLine(out, p + QString("experiment_%1").arg(s + 1), sig.experiment);
                writeLine(out, p + QString("server_ip_%1").arg(s + 1), sig.serverIp);
            }
        }
    }
    return true;
}

void MainWindow::updateTopInfoLabels()
{
    QString shot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    const PlotSpec* plot = nullptr;
    if (selectedColumn_ >= 0 && selectedRow_ >= 0
        && selectedColumn_ < config_.columns.size()
        && selectedRow_ < config_.columns[selectedColumn_].size()) {
        plot = &config_.columns[selectedColumn_][selectedRow_];
    }
    if (!plot) {
        for (const auto& col : config_.columns) {
            for (const PlotSpec& candidate : col) {
                if (!candidate.signalSpecs.isEmpty()) {
                    plot = &candidate;
                    break;
                }
            }
            if (plot) {
                break;
            }
        }
    }
    if (plot) {
        if (shot.isEmpty()) {
            shot = plot->shot.trimmed();
        }
    }

    if (shot.isEmpty()) {
        topSummaryShot_.clear();
        topSummaryIp_.clear();
        topSummaryPulse_.clear();
        topSummaryIt_.clear();
        topSummaryTime_.clear();
        pendingTopSummaryShot_.clear();
    } else if (shot != topSummaryShot_) {
        topSummaryIp_.clear();
        topSummaryPulse_.clear();
        topSummaryIt_.clear();
        topSummaryTime_.clear();
        scheduleTopInfoUpdate(shot);
    }

    setLabelTextIfChanged(topInfoLabel_, "Shot: " + (shot.isEmpty() ? QStringLiteral("--") : shot));
    const bool loading = !shot.isEmpty() && shot != topSummaryShot_ && pendingTopSummaryShot_ == shot;
    const QString emptyText = loading ? QStringLiteral("...") : QStringLiteral("--");
    setLabelTextIfChanged(ipInfoLabel_, "Ip: " + (topSummaryIp_.isEmpty() ? emptyText : topSummaryIp_ + " KA"));
    setLabelTextIfChanged(pulseInfoLabel_, "Pulse: " + (topSummaryPulse_.isEmpty() ? emptyText : topSummaryPulse_ + " s"));
    setLabelTextIfChanged(itInfoLabel_, "It: " + (topSummaryIt_.isEmpty() ? emptyText : topSummaryIt_ + " A"));
    setLabelTextIfChanged(timeInfoLabel_, "Time: " + (topSummaryTime_.isEmpty() ? emptyText : topSummaryTime_));
}

void MainWindow::scheduleTopInfoUpdate(const QString& shot)
{
    const QString trimmedShot = shot.trimmed();
    if (trimmedShot.isEmpty() || pendingTopSummaryShot_ == trimmedShot) {
        return;
    }

    pendingTopSummaryShot_ = trimmedShot;
    const int generation = ++topSummaryGeneration_;
    QThreadPool::globalInstance()->start([this, trimmedShot, generation] {
        QString ip;
        QString pulse;
        QString it;
        QString shotTime;
        const bool ok = loadShotSummaryFromApi(trimmedShot, &ip, &pulse, &it, &shotTime);
        QMetaObject::invokeMethod(this, [this, trimmedShot, generation, ok, ip, pulse, it, shotTime] {
            if (generation != topSummaryGeneration_ || pendingTopSummaryShot_ != trimmedShot) {
                return;
            }
            pendingTopSummaryShot_.clear();
            topSummaryShot_ = trimmedShot;
            topSummaryIp_ = ok ? ip : QString();
            topSummaryPulse_ = ok ? pulse : QString();
            topSummaryIt_ = ok ? it : QString();
            topSummaryTime_ = ok ? shotTime : QString();
            updateTopInfoLabels();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::setStatus(const QString& text)
{
    setLabelTextIfChanged(statusLabel_, text);
}

PlotWidget* MainWindow::currentPlotWidget() const
{
    if (selectedColumn_ < 0 || selectedRow_ < 0 ||
        selectedColumn_ >= plotWidgets_.size() || selectedRow_ >= plotWidgets_[selectedColumn_].size()) {
        return nullptr;
    }
    return plotWidgets_[selectedColumn_][selectedRow_];
}
