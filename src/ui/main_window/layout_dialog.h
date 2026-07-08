// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shared.h"

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

inline bool layoutItemsMatchConfig(const QVector<QVector<LayoutCanvas::Item>>& layout, const LayoutConfig& config)
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

