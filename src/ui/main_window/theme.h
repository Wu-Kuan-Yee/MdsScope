// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shared.h"

inline QString themeModeLabel(ThemeMode mode)
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

