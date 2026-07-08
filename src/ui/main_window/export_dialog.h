// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "layout_dialog.h"

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

