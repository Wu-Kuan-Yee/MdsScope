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
           && lhs.hidden == rhs.hidden;
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

QString uniqueExportPath(const QDir& dir, const QString& baseName)
{
    QString path = dir.filePath(baseName + ".txt");
    if (!QFileInfo::exists(path)) {
        return path;
    }
    for (int i = 2; i < 10000; ++i) {
        path = dir.filePath(QString("%1_%2.txt").arg(baseName).arg(i));
        if (!QFileInfo::exists(path)) {
            return path;
        }
    }
    return dir.filePath(baseName + "_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".txt");
}

bool writeSeriesTextFile(const QString& path, const SignalSeries& series, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = "Cannot write " + path;
        }
        return false;
    }
    QTextStream out(&file);
    out << "# x y\n";
    if (!series.error.isEmpty()) {
        out << "# error: " << series.error << '\n';
    }
    if (series.hasUniformData()) {
        for (int i = 0; i < series.uniformY.size(); ++i) {
            out << QString::number(series.uniformStart + static_cast<double>(i) * series.uniformStep, 'g', 17)
                << ' ' << QString::number(series.uniformY[i], 'g', 9) << '\n';
        }
    } else {
        for (const QPointF& point : series.points) {
            out << QString::number(point.x(), 'g', 17)
                << ' ' << QString::number(point.y(), 'g', 17) << '\n';
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
        treeModel_ = new QStringListModel(readSourceIndexLines(QDir(sourceIndexDir_).filePath("trees.txt")), this);
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
        rowsLayout_->addWidget(new QLabel("", rowsHost_), 0, 6);
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
            sig.shot = defaultShot_;
            int activeCount = 0;
            bool copiedDefaults = false;
            if (!rows_.isEmpty()) {
                for (const Row* row : std::as_const(rows_)) {
                    if (!row->deleted) {
                        ++activeCount;
                        if (!copiedDefaults) {
                            sig.experiment = row->tree->text().trimmed();
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
            sig.shot = row->shot->text().trimmed();
            sig.xExpr = row->xExpr;
            sig.experiment = row->tree->text().trimmed();
            sig.serverIp = row->server->text().trimmed();
            sig.colorName = row->color.name();
            sig.manualColor = row->manualColor;
            sig.hidden = row->hidden->isChecked();
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
        QCompleter* treeCompleter = nullptr;
        QStringListModel* signalModel = nullptr;
        QCompleter* signalCompleter = nullptr;
        QPushButton* colorButton = nullptr;
        QCheckBox* hidden = nullptr;
        QPushButton* deleteButton = nullptr;
        QColor color;
        QString xExpr;
        bool deleted = false;
        bool manualColor = false;
    };

    void addRow(const SignalSpec& sig, int colorIndex)
    {
        auto* row = new Row;
        row->shot = new QLineEdit(sig.shot.trimmed().isEmpty() ? defaultShot_ : sig.shot.trimmed(), rowsHost_);
        row->tree = new QLineEdit(sig.experiment, rowsHost_);
        row->signal = new QLineEdit(sig.yExpr, rowsHost_);
        row->server = new QLineEdit(sig.serverIp, rowsHost_);
        row->treeCompleter = makeCompleter(treeModel_, row->tree);
        row->signalModel = new QStringListModel(row->signal);
        row->signalCompleter = makeCompleter(row->signalModel, row->signal);
        row->colorButton = new QPushButton(rowsHost_);
        row->hidden = new QCheckBox(rowsHost_);
        row->deleteButton = new QPushButton("Delete", rowsHost_);
        row->manualColor = sig.manualColor;
        row->color = QColor(row->manualColor && !sig.colorName.isEmpty() ? sig.colorName : colorForIndex(colorIndex));
        row->xExpr = sig.xExpr;
        row->hidden->setChecked(sig.hidden);
        row->shot->setMinimumWidth(72);
        row->signal->setMinimumWidth(150);
        row->server->setMinimumWidth(120);
        row->tree->setCompleter(row->treeCompleter);
        row->signal->setCompleter(row->signalCompleter);
        updateSignalCompleter(row);
        updateColorButton(row);

        const int gridRow = rows_.size() + 1;
        rowsLayout_->addWidget(row->shot, gridRow, 0);
        rowsLayout_->addWidget(row->tree, gridRow, 1);
        rowsLayout_->addWidget(row->signal, gridRow, 2);
        rowsLayout_->addWidget(row->server, gridRow, 3);
        rowsLayout_->addWidget(row->colorButton, gridRow, 4);
        rowsLayout_->addWidget(row->hidden, gridRow, 5, Qt::AlignCenter);
        rowsLayout_->addWidget(row->deleteButton, gridRow, 6);
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
            updateSignalCompleter(row);
        });
        connect(row->deleteButton, &QPushButton::clicked, this, [row] {
            row->deleted = true;
            for (QWidget* widget : {static_cast<QWidget*>(row->shot),
                                    static_cast<QWidget*>(row->tree),
                                    static_cast<QWidget*>(row->signal),
                                    static_cast<QWidget*>(row->server),
                                    static_cast<QWidget*>(row->colorButton),
                                    static_cast<QWidget*>(row->hidden),
                                    static_cast<QWidget*>(row->deleteButton)}) {
                widget->hide();
            }
        });
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
        const QString key = tree.trimmed().toLower();
        if (key.isEmpty()) {
            return {};
        }
        if (!signalCache_.contains(key)) {
            const QString path = QDir(QDir(sourceIndexDir_).filePath("signals")).filePath(sourceIndexFileName(tree));
            signalCache_.insert(key, readSourceIndexLines(path));
        }
        return signalCache_.value(key);
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
    QStringListModel* treeModel_ = nullptr;
    QHash<QString, QStringList> signalCache_;
    QVector<Row*> rows_;
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
        painter.fillRect(rect(), QColor("#ffffff"));
        painter.setPen(QPen(QColor("#cbd5e1"), 1));
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
                const QColor fill = item.selected ? QColor("#f97316") : QColor("#60a5fa");
                const QRectF rect = cellRect(area, c, r, cellW, cellH, gap);
                drawCell(painter, rect, fill);
                if (item.isNew) {
                    painter.setPen(Qt::white);
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
    explicit ExportDataDialog(const LayoutConfig& config, const QString& defaultDir, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Export Data");
        resize(900, 560);
        auto* mainLayout = new QHBoxLayout(this);
        canvas_ = new LayoutCanvas(config, this, false);
        mainLayout->addWidget(canvas_, 1);

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
        connect(selectAll, &QPushButton::clicked, canvas_, &LayoutCanvas::selectAllOriginalPanels);
        connect(clear, &QPushButton::clicked, canvas_, &LayoutCanvas::clearSelectedPanels);
        connect(exportButton, &QPushButton::clicked, this, [this] {
            if (selectedPanels().isEmpty()) {
                QMessageBox::warning(this, "Export Data", "Select at least one panel.");
                return;
            }
            if (outputBaseDir().isEmpty()) {
                QMessageBox::warning(this, "Export Data", "Choose an output directory.");
                return;
            }
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QVector<QPair<int, int>> selectedPanels() const
    {
        return canvas_->selectedOriginalPanels();
    }

    QString outputBaseDir() const
    {
        return outputDir_->text().trimmed();
    }

private:
    LayoutCanvas* canvas_ = nullptr;
    QLineEdit* outputDir_ = nullptr;
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
}

MainWindow::MainWindow(QString rootPath, QWidget* parent)
    : QMainWindow(parent), rootPath_(std::move(rootPath))
{
    environmentPath_ = appEnvironmentDir(rootPath_);
    ensureSourceIndexCache(rootPath_);
    exportBasePath_ = defaultExportBaseDir();
    loadFontSettings(rootPath_);
    buildUi();
    applyUiFont();
    connect(&dataWatcher_, &QFutureWatcher<QVector<LoadedSignal>>::finished, this, [this] {
        applyLoadedSignals(dataWatcher_.result());
    });
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
        if (warmRefreshPending_) {
            warmRefreshPending_ = false;
            refreshData();
        } else {
            setStatus("MDS connections ready");
        }
    });
    const QString defaultTomlConfig = QDir(environmentPath_).filePath("init.toml");
    const QString defaultWebscpConfig = QDir(environmentPath_).filePath("init.webscp");
    if (QFileInfo::exists(defaultTomlConfig)) {
        loadEnvironmentFile(defaultTomlConfig, true);
    } else if (QFileInfo::exists(defaultWebscpConfig)) {
        loadEnvironmentFile(defaultWebscpConfig, true);
    } else {
        loadEnvironmentList(true);
    }
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
    toolbar->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Open configure file", this, &MainWindow::openEnvironmentFile);
    QAction* saveAction = toolbar->addAction(saveIcon(), "Save", this, &MainWindow::saveCurrentEnvironment);
    saveAction->setShortcut(QKeySequence::Save);
    saveAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(saveAction);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "Export data", this, &MainWindow::openExportDataDialog);
    toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Refresh", this, &MainWindow::refreshData);
    loginAction_ = toolbar->addAction(loginIcon(false), "Login", this, &MainWindow::openLoginDialog);
    updateLoginActionIcon();
    toolbar->addSeparator();
    toolbar->addAction(gearIcon(), "Layout setup", this, &MainWindow::openLayoutSetupDialog);
    toolbar->addSeparator();
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
    auto* topLayout = new QHBoxLayout(topControls);
    topLayout->setContentsMargins(2, 0, 2, 0);
    topLayout->setSpacing(3);
    topControls->setStyleSheet(
        "QPushButton { padding: 1px 8px; min-height: 18px; }"
        "QLineEdit, QComboBox { min-height: 18px; padding: 0px 2px; }"
        "QLabel { margin-left: 2px; margin-right: 2px; }");
    topLayout->addWidget(new QLabel("Rate", topControls));
    dataModeCombo_ = new QComboBox(topControls);
    dataModeCombo_->addItem("Thin", static_cast<int>(DataReadMode::Thin));
    dataModeCombo_->addItem("Full", static_cast<int>(DataReadMode::Full));
    dataModeCombo_->setCurrentIndex(0);
    dataModeCombo_->setFixedWidth(78);
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
    toolbar->addWidget(topControls);

    auto* bottom = new QWidget(this);
    auto* bottomLayout = new QHBoxLayout(bottom);
    bottomLayout->setContentsMargins(4, 1, 4, 1);
    bottomLayout->setSpacing(5);
    bottom->setMaximumHeight(34);
    bottom->setStyleSheet(
        "QPushButton { padding: 1px 8px; min-height: 18px; }"
        "QLineEdit { min-height: 18px; padding: 0px 2px; }"
        "QToolButton { margin: 0px; padding: 1px; min-width: 30px; min-height: 28px; border: 1px solid transparent; }"
        "QToolButton:checked { border: 1px solid palette(highlight); background: palette(alternate-base); }");
    zoomButton_ = new QToolButton(bottom);
    pointButton_ = new QToolButton(bottom);
    for (QToolButton* button : {zoomButton_, pointButton_}) {
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setIconSize(QSize(24, 24));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    zoomButton_->setToolTip("Zoom / Move (Ctrl+Z): drag to zoom, middle-drag or Shift-drag to move");
    pointButton_->setToolTip("Point (Ctrl+P)");
    pointButton_->setChecked(true);
    bottomLayout->addWidget(zoomButton_);
    bottomLayout->addWidget(pointButton_);
    bottomLayout->addWidget(new QLabel("Shot", bottom));
    shotEdit_ = new QLineEdit(bottom);
    shotEdit_->setFixedWidth(105);
    bottomLayout->addWidget(shotEdit_);
    auto* apply = new QPushButton("Apply", bottom);
    auto* prev = new QPushButton("Prev", bottom);
    auto* next = new QPushButton("Next", bottom);
    auto* stop = new QPushButton("Stop", bottom);
    auto* latest = new QPushButton("Latest", bottom);
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
    connect(stop, &QPushButton::clicked, this, [this] { setStatus("Stop requested"); });
    connect(shotEdit_, &QLineEdit::returnPressed, this, &MainWindow::applyShot);
    connect(dataModeCombo_, &QComboBox::currentIndexChanged, this, [this] { refreshData(); });
}

void MainWindow::loadEnvironmentList(bool useLatestWhenNoCurrentShot)
{
    QDir dir(environmentPath_);
    const auto files = dir.entryInfoList({"*.toml", "*.webscp"}, QDir::Files, QDir::Name);
    if (!files.isEmpty()) {
        loadEnvironmentFile(files.first().absoluteFilePath(), useLatestWhenNoCurrentShot);
        return;
    }
    setStatus("No environment files found");
}

void MainWindow::loadSelectedEnvironment()
{
    openEnvironmentFile();
}

void MainWindow::openEnvironmentFile()
{
    QSettings settings(uiSettingsPath(rootPath_), QSettings::IniFormat);
    const QString allFilter = "All MdsScope Config (*.toml *.webscp)";
    const QString tomlFilter = "MdsScope TOML (*.toml)";
    const QString webscpFilter = "Legacy WebScope Config (*.webscp)";
    const QString filters = allFilter + ";;" + tomlFilter + ";;" + webscpFilter + ";;All Files (*)";
    QString selectedFilter = settings.value("files/open_filter", allFilter).toString();
    const QString path = QFileDialog::getOpenFileName(this, "Open MdsScope Config", environmentPath_, filters, &selectedFilter);
    if (!path.isEmpty()) {
        settings.setValue("files/open_filter", selectedFilter);
        loadEnvironmentFile(path);
    }
}

bool MainWindow::loadEnvironmentFile(const QString& path, bool useLatestWhenNoCurrentShot)
{
    const QString previousShot = shotEdit_ ? shotEdit_->text().trimmed() : QString();
    const bool shouldFetchLatest = useLatestWhenNoCurrentShot && previousShot.isEmpty();
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
    prewarmConnections(!shouldFetchLatest);
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
    plotWidgets_.clear();
    plotWidgets_.resize(config_.columns.size());

    for (int c = 0; c < config_.columns.size(); ++c) {
        auto* columnHost = new QWidget(gridHost_);
        columnHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto* columnLayout = new QVBoxLayout(columnHost);
        columnLayout->setContentsMargins(0, 0, 0, 0);
        columnLayout->setSpacing(0);
        plotWidgets_[c].resize(config_.columns[c].size());
        for (int r = 0; r < config_.columns[c].size(); ++r) {
            auto* plot = new PlotWidget(columnHost);
            plot->setSpec(config_.columns[c][r]);
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
        for (int c = 0; c < plotWidgets_.size(); ++c) {
            for (int r = 0; r < plotWidgets_[c].size(); ++r) {
                if (c < config_.columns.size() && r < config_.columns[c].size()) {
                    plotWidgets_[c][r]->setSpec(config_.columns[c][r]);
                }
            }
        }
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

void MainWindow::refreshData()
{
    if (warmWatcher_.isRunning()) {
        warmRefreshPending_ = true;
        setStatus("Connecting to MDS...");
        return;
    }
    const DataReadMode readMode = dataModeCombo_ && dataModeCombo_->currentData().toInt() == static_cast<int>(DataReadMode::Full)
                                      ? DataReadMode::Full
                                      : DataReadMode::Thin;
    const QString key = refreshKey(readMode);
    if (dataWatcher_.isRunning()) {
        if (key == activeRefreshKey_ || key == queuedRefreshKey_) {
            setStatus("Data refresh already running for current shot");
            return;
        }
        pendingRefresh_ = true;
        queuedRefreshKey_ = key;
        activeRefreshKey_.clear();
        queuedLoadedSignals_.clear();
        queuedLoadedSignalApply_ = false;
        for (auto& col : plotWidgets_) {
            for (PlotWidget* plot : col) {
                plot->clearSeries();
            }
        }
        setStatus("Data refresh queued for current shot");
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
    const LayoutConfig snapshot = config_;
    activeRefreshKey_ = key;
    queuedRefreshKey_.clear();
    streamedOk_ = 0;
    streamedFailed_ = 0;
    setStatus(readMode == DataReadMode::Thin ? "Fetching MDS data (thin)..." : "Fetching MDS data (full)...");
    dataWatcher_.setFuture(QtConcurrent::run([this, snapshot, readMode, key] {
        auto loaded = fetchMdsSignals(snapshot, readMode, [this, key](const LoadedSignal& item) {
            QMetaObject::invokeMethod(this, [this, key, item] {
                if (key == activeRefreshKey_) {
                    queueLoadedSignal(item);
                }
            }, Qt::QueuedConnection);
        });
        return loaded;
    }));
}

void MainWindow::prewarmConnections(bool refreshAfter)
{
    if (config_.columns.isEmpty()) {
        return;
    }
    warmRefreshPending_ = warmRefreshPending_ || refreshAfter;
    if (warmWatcher_.isRunning()) {
        setStatus("Connecting to MDS...");
        return;
    }

    const LayoutConfig snapshot = config_;
    setStatus("Connecting to MDS...");
    warmWatcher_.setFuture(QtConcurrent::run([snapshot] {
        warmMdsConnections(snapshot);
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

    const DataReadMode readMode = dataModeCombo_ && dataModeCombo_->currentData().toInt() == static_cast<int>(DataReadMode::Full)
                                      ? DataReadMode::Full
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
        pendingPanelRefresh_ = true;
        pendingPanelColumn_ = column;
        pendingPanelRow_ = row;
        pendingPanelSignal_ = signal;
        queuedPanelRefreshKey_ = key;
        activePanelRefreshKey_.clear();
        setStatus(QString("Panel refresh queued: col %1 row %2").arg(column + 1).arg(row + 1));
        return;
    }

    LayoutConfig snapshot = config_;
    const bool singleSignalRefresh = signal >= 0
                                     && signal < config_.columns[column][row].signalSpecs.size();
    for (int c = 0; c < snapshot.columns.size(); ++c) {
        for (int r = 0; r < snapshot.columns[c].size(); ++r) {
            if (c == column && r == row) {
                continue;
            }
            snapshot.columns[c][r].signalSpecs.clear();
        }
    }
    if (singleSignalRefresh) {
        snapshot.columns[column][row].signalSpecs = {config_.columns[column][row].signalSpecs[signal]};
    }

    if (!singleSignalRefresh) {
        plotWidgets_[column][row]->clearSeries();
    }
    activePanelRefreshKey_ = key;
    queuedPanelRefreshKey_.clear();
    setStatus(singleSignalRefresh
                  ? QString("Fetching signal data: col %1 row %2 source %3").arg(column + 1).arg(row + 1).arg(signal + 1)
                  : QString("Fetching panel data: col %1 row %2").arg(column + 1).arg(row + 1));
    panelWatcher_.setFuture(QtConcurrent::run([snapshot, readMode, singleSignalRefresh, signal] {
        QVector<LoadedSignal> loaded = fetchMdsSignals(snapshot, readMode);
        if (singleSignalRefresh) {
            for (LoadedSignal& item : loaded) {
                if (item.column >= 0 && item.row >= 0) {
                    item.signal = signal;
                }
            }
        }
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
    if (item.column >= config_.columns.size()
        || item.row >= config_.columns[item.column].size()
        || !loadedSignalMatchesConfig(config_, item)) {
        return;
    }

    plotWidgets_[item.column][item.row]->setSeries(item.signal, item.series);
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
        return;
    }

    int ok = 0;
    int failed = 0;
    for (const LoadedSignal& item : loaded) {
        if (item.column < plotWidgets_.size() && item.row < plotWidgets_[item.column].size()) {
            if (!loadedSignalMatchesConfig(config_, item)) {
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
}

void MainWindow::applyPanelLoadedSignals(const QVector<LoadedSignal>& loaded)
{
    int ok = 0;
    int failed = 0;
    for (const LoadedSignal& item : loaded) {
        if (item.column < 0 || item.row < 0
            || item.column >= plotWidgets_.size()
            || item.row >= plotWidgets_[item.column].size()
            || !loadedSignalMatchesConfig(config_, item)) {
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
        || item.column >= config_.columns.size()
        || item.row >= config_.columns[item.column].size()
        || item.signal >= config_.columns[item.column][item.row].signalSpecs.size()) {
        return;
    }
    const SignalSpec& sig = config_.columns[item.column][item.row].signalSpecs[item.signal];
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
    plotWidgets_[selectedColumn_][selectedRow_]->setSpec(plot);
    refreshOne(selectedColumn_, selectedRow_, plot.signalSpecs.size() - 1);
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
        plotWidgets_[selectedColumn_][selectedRow_]->setSpec(plot);
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
    plotWidgets_[selectedColumn_][selectedRow_]->setSpec(plot);
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
    widget->setSpec(plot);
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
    ExportDataDialog dialog(config_, exportBasePath_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    exportDataForPanels(dialog.selectedPanels(), dialog.outputBaseDir());
}

void MainWindow::exportCurrentPanelData()
{
    if (selectedColumn_ < 0 || selectedRow_ < 0) {
        return;
    }
    const QString baseDirPath = QFileDialog::getExistingDirectory(this, "Export Base Directory", exportBasePath_);
    if (baseDirPath.isEmpty()) {
        return;
    }
    exportDataForPanels({{selectedColumn_, selectedRow_}}, baseDirPath);
}

void MainWindow::exportDataForPanels(const QVector<QPair<int, int>>& panels, const QString& baseDirPath)
{
    if (panels.isEmpty()) {
        QMessageBox::warning(this, "Export Data", "Select at least one panel.");
        return;
    }
    if (baseDirPath.trimmed().isEmpty()) {
        QMessageBox::warning(this, "Export Data", "Choose an output directory.");
        return;
    }

    LayoutConfig snapshot = config_;
    QSet<QPair<int, int>> selected;
    for (const auto& panel : panels) {
        selected.insert(panel);
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

    const DataReadMode readMode = dataModeCombo_ && dataModeCombo_->currentData().toInt() == static_cast<int>(DataReadMode::Full)
                                      ? DataReadMode::Full
                                      : DataReadMode::Thin;
    setStatus(QString("Exporting data from %1 panels...").arg(panels.size()));
    QPointer<MainWindow> self(this);
    QThreadPool::globalInstance()->start([self, snapshot, baseDirPath = baseDirPath.trimmed(), readMode] {
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
                const QString path = uniqueExportPath(outputDir, QString("%1-%2-%3").arg(shot, tree, signal));
                QString error;
                if (writeSeriesTextFile(path, item.series, &error)) {
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

void MainWindow::resetCurrentScale()
{
    PlotWidget* current = currentPlotWidget();
    if (!current) {
        return;
    }
    current->resetScale();
    setStatus("Reset current panel scale");
}

void MainWindow::resetScales()
{
    const bool updatesEnabled = gridHost_ ? gridHost_->updatesEnabled() : true;
    if (gridHost_ && updatesEnabled) {
        gridHost_->setUpdatesEnabled(false);
    }
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
    setStatus("Reset all scales");
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
    const QString path = QFileDialog::getSaveFileName(this, "Save MdsScope Config", environmentPath_, "MdsScope Config (*.toml)");
    if (path.isEmpty()) {
        return;
    }
    if (saveEnvironmentFile(path)) {
        const QFileInfo info(path);
        config_.filePath = info.suffix().isEmpty() ? path + ".toml" : path;
        loadEnvironmentList();
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
