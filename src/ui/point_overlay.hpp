// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mdsscope_internal.hpp"

class PointOverlay final : public QWidget {
public:
    explicit PointOverlay(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        vLine_ = new QWidget(this);
        hLine_ = new QWidget(this);
        label_ = new QLabel(this);
        const QString color = palette().color(QPalette::WindowText).name();
        vLine_->setStyleSheet("background: " + color + ";");
        hLine_->setStyleSheet("background: " + color + ";");
        label_->setStyleSheet("color: palette(highlight); background: transparent;");
        vLine_->setAttribute(Qt::WA_TransparentForMouseEvents);
        hLine_->setAttribute(Qt::WA_TransparentForMouseEvents);
        label_->setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

    void setPoint(QRectF plotRect, QPointF pixel, QString text)
    {
        const bool nextVisible = !text.isEmpty() && plotRect.contains(pixel);
        if (visible_ == nextVisible
            && text_ == text
            && plotRect_ == plotRect
            && qRound(pixel_.x()) == qRound(pixel.x())
            && qRound(pixel_.y()) == qRound(pixel.y())) {
            return;
        }
        plotRect_ = plotRect;
        pixel_ = pixel;
        text_ = std::move(text);
        const bool shouldShow = !text_.isEmpty() && plotRect_.contains(pixel_);
        if (shouldShow != visible_) {
            visible_ = shouldShow;
            setVisible(visible_);
        }
        if (visible_) {
            raise();
            positionChildren();
        }
    }

    void clearPoint()
    {
        if (!visible_ && text_.isEmpty()) {
            return;
        }
        visible_ = false;
        text_.clear();
        hide();
    }

private:
    void positionChildren()
    {
        if (!visible_ || text_.isEmpty() || !plotRect_.contains(pixel_)) {
            return;
        }
        const int x = qRound(pixel_.x());
        const int y = qRound(pixel_.y());
        vLine_->setGeometry(x, qRound(plotRect_.top()), 1, qRound(plotRect_.height()));
        hLine_->setGeometry(qRound(plotRect_.left()), y, qRound(plotRect_.width()), 1);
        QRectF textRect(pixel_.x() + 4, pixel_.y() - 18, 180, 18);
        if (textRect.right() > plotRect_.right()) {
            textRect.moveRight(pixel_.x() - 4);
        }
        if (textRect.left() < plotRect_.left()) {
            textRect.moveLeft(plotRect_.left() + 2);
        }
        if (textRect.top() < plotRect_.top()) {
            textRect.moveTop(pixel_.y() + 4);
        }
        label_->setText(text_);
        label_->setGeometry(textRect.toAlignedRect());
        vLine_->show();
        hLine_->show();
        label_->show();
    }

    QRectF plotRect_;
    QPointF pixel_;
    QString text_;
    bool visible_ = false;
    QWidget* vLine_ = nullptr;
    QWidget* hLine_ = nullptr;
    QLabel* label_ = nullptr;
};

class GlobalPointOverlay final : public QWidget {
public:
    explicit GlobalPointOverlay(QWidget* parent)
        : QWidget(parent), host_(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedSize(0, 0);
        hide();
    }

    void setReadouts(QVector<PointReadout> readouts)
    {
        if (readouts_.size() == readouts.size()) {
            bool same = true;
            for (int i = 0; i < readouts.size(); ++i) {
                const PointReadout& a = readouts_[i];
                const PointReadout& b = readouts[i];
                if (a.visible != b.visible
                    || a.showText != b.showText
                    || a.color != b.color
                    || a.text != b.text
                    || qRound(a.pixel.x()) != qRound(b.pixel.x())
                    || qRound(a.pixel.y()) != qRound(b.pixel.y())
                    || a.plotRect.toAlignedRect() != b.plotRect.toAlignedRect()) {
                    same = false;
                    break;
                }
            }
            if (same) {
                return;
            }
        }
        readouts_ = std::move(readouts);
        ensureItems(readouts_.size());
        for (int i = 0; i < items_.size(); ++i) {
            if (i >= readouts_.size()) {
                hideItem(items_[i]);
                continue;
            }
            positionItem(items_[i], readouts_[i]);
        }
    }

    void clearReadouts()
    {
        if (readouts_.isEmpty() && items_.isEmpty()) {
            return;
        }
        readouts_.clear();
        for (Item& item : items_) {
            hideItem(item);
        }
    }

private:
    struct Item {
        QWidget* vLine = nullptr;
        QWidget* hLine = nullptr;
        QLabel* label = nullptr;
        QString colorName;
    };

    static QRectF textRectFor(const PointReadout& readout)
    {
        QRectF textRect(readout.pixel.x() + 4, readout.pixel.y() - 18, 160, 18);
        if (textRect.right() > readout.plotRect.right()) {
            textRect.moveRight(readout.pixel.x() - 4);
        }
        if (textRect.left() < readout.plotRect.left()) {
            textRect.moveLeft(readout.plotRect.left() + 2);
        }
        if (textRect.top() < readout.plotRect.top()) {
            textRect.moveTop(readout.pixel.y() + 4);
        }
        return textRect;
    }

    void ensureItems(int count)
    {
        QWidget* parent = host_ ? host_ : this;
        while (items_.size() < count) {
            Item item;
            item.vLine = new QWidget(parent);
            item.hLine = new QWidget(parent);
            item.label = new QLabel(parent);
            item.vLine->setAttribute(Qt::WA_TransparentForMouseEvents);
            item.hLine->setAttribute(Qt::WA_TransparentForMouseEvents);
            item.label->setAttribute(Qt::WA_TransparentForMouseEvents);
            const QString color = parent->palette().color(QPalette::WindowText).name();
            item.vLine->setStyleSheet("background: " + color + ";");
            item.hLine->setStyleSheet("background: " + color + ";");
            item.label->setStyleSheet("color: " + color + "; background: transparent;");
            QFont labelFont = item.label->font();
            labelFont.setPointSize(std::max(8, labelFont.pointSize() + 1));
            labelFont.setBold(false);
            item.label->setFont(labelFont);
            hideItem(item);
            items_.push_back(item);
        }
    }

    static void hideItem(Item& item)
    {
        if (item.vLine) {
            item.vLine->hide();
        }
        if (item.hLine) {
            item.hLine->hide();
        }
        if (item.label) {
            item.label->hide();
            item.label->clear();
        }
    }

    static void positionItem(Item& item, const PointReadout& readout)
    {
        if (!readout.visible || !readout.plotRect.contains(readout.pixel)) {
            hideItem(item);
            return;
        }

        const int x = qRound(readout.pixel.x());
        const int y = qRound(readout.pixel.y());
        const QString color = readout.color.isValid() ? readout.color.name() : QStringLiteral("#333333");
        if (item.colorName != color) {
            item.colorName = color;
            item.vLine->setStyleSheet("background: " + color + ";");
            item.hLine->setStyleSheet("background: " + color + ";");
            item.label->setStyleSheet("color: " + color + "; background: transparent;");
        }
        item.vLine->setGeometry(x, qRound(readout.plotRect.top()), 1, std::max(1, qRound(readout.plotRect.height())));
        item.hLine->setGeometry(qRound(readout.plotRect.left()), y, std::max(1, qRound(readout.plotRect.width())), 1);
        item.vLine->raise();
        item.hLine->raise();
        item.vLine->show();
        item.hLine->show();

        if (readout.showText && !readout.text.isEmpty()) {
            item.label->setText(readout.text);
            item.label->setGeometry(textRectFor(readout).toAlignedRect());
            item.label->raise();
            item.label->show();
        } else {
            item.label->hide();
            item.label->clear();
        }
    }

    QWidget* host_ = nullptr;
    QVector<PointReadout> readouts_;
    QVector<Item> items_;
};
