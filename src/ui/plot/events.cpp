// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "helpers.hpp"

void PlotWidget::mousePressEvent(QMouseEvent* event)
{
    emit selected();
    const bool moveDrag = interactionMode_ == InteractionMode::Pan
                          || (interactionMode_ == InteractionMode::Zoom
                              && (event->button() == Qt::MiddleButton
                                  || (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ShiftModifier))));
    if ((event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton)
        || (event->button() == Qt::MiddleButton && !moveDrag)) {
        event->ignore();
        return;
    }
    setFocus(Qt::MouseFocusReason);
    if (moveDrag) {
        dragging_ = true;
        lastDragPos_ = event->position();
        setCursor(Qt::ClosedHandCursor);
    } else if (interactionMode_ == InteractionMode::Zoom) {
        zooming_ = true;
        zoomStart_ = event->position();
        zoomRubberBand_ = QRectF(zoomStart_, QSizeF());
    } else {
        pointTrackingActive_ = true;
        bool changed = false;
        const int legendIndex = legendSeriesAt(event->position());
        if (legendIndex >= 0) {
            const double dataX = hoverText_.isEmpty() ? pixelToData(event->position(), effectiveView(), plotRect()).x() : hoverData_.x();
            changed = updateHoverForSeriesX(legendIndex, dataX, true);
        } else {
            changed = updateHover(event->position(), true);
            if (!hoverSeriesLocked_) {
                const double dataX = pixelToData(event->position(), effectiveView(), plotRect()).x();
                changed = updateHoverForSeriesX(0, dataX, true) || changed;
            }
        }
        if (changed && !hoverText_.isEmpty()) {
            emit pointXChanged(hoverData_.x());
        }
    }
    event->accept();
}

void PlotWidget::keyPressEvent(QKeyEvent* event)
{
    if (interactionMode_ == InteractionMode::Point && event->modifiers() == Qt::NoModifier) {
        if (event->key() == Qt::Key_Escape) {
            if (pointTrackingActive_) {
                pointTrackingActive_ = false;
                ++pointHoverGeneration_;
                pointHoverQueued_ = false;
                hoverText_.clear();
                hoverSeriesIndex_ = -1;
                hoverSeriesLocked_ = false;
                emit pointTrackingStopped();
                event->accept();
                return;
            }
        } else if (event->key() == Qt::Key_Left) {
            if (stepActivePoint(-1)) {
                event->accept();
                return;
            }
        } else if (event->key() == Qt::Key_Right) {
            if (stepActivePoint(1)) {
                event->accept();
                return;
            }
        }
    }
    QWidget::keyPressEvent(event);
}

void PlotWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        invalidatePlotCache();
        update();
    }
}

void PlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QRectF pr = plotRect();
    QRectF view = effectiveView();
    const bool leftButtonDown = event->buttons().testFlag(Qt::LeftButton);
    const bool moveButtonDown = event->buttons().testFlag(Qt::MiddleButton)
                                || (leftButtonDown && event->modifiers().testFlag(Qt::ShiftModifier));
    if (!leftButtonDown && !moveButtonDown) {
        if (zooming_) {
            const QRect oldDirty = zoomRubberBandDirtyRect(zoomRubberBand_);
            zooming_ = false;
            zoomRubberBand_ = {};
            if (oldDirty.isValid() && !oldDirty.isEmpty()) {
                update(oldDirty);
            }
        }
        if (dragging_) {
            dragging_ = false;
            unsetCursor();
        }
    }
    if ((interactionMode_ == InteractionMode::Pan || interactionMode_ == InteractionMode::Zoom) && dragging_ && pr.isValid()) {
        const QPointF delta = event->position() - lastDragPos_;
        const double dx = -delta.x() / pr.width() * view.width();
        const double dy = delta.y() / pr.height() * view.height();
        view.translate(dx, dy);
        view_ = view;
        hasView_ = true;
        invalidatePlotCache();
        lastDragPos_ = event->position();
    }
    if (interactionMode_ == InteractionMode::Zoom && zooming_ && leftButtonDown && !dragging_) {
        const QRect oldDirty = zoomRubberBandDirtyRect(zoomRubberBand_);
        zoomRubberBand_ = QRectF(zoomStart_, event->position()).normalized().intersected(pr);
        const QRect newDirty = zoomRubberBandDirtyRect(zoomRubberBand_);
        const QRect dirty = oldDirty.united(newDirty);
        if (dirty.isValid() && !dirty.isEmpty()) {
            update(dirty);
        }
    }
    bool needsUpdate = (interactionMode_ == InteractionMode::Pan || interactionMode_ == InteractionMode::Zoom) && dragging_;
    if (interactionMode_ == InteractionMode::Point) {
        if (!pointTrackingActive_) {
            event->accept();
            return;
        }
        if (hoverSeriesLocked_) {
            schedulePointHoverUpdate(event->position());
        }
        needsUpdate = false;
    }
    if (needsUpdate) {
        scheduleUpdate();
    }
}

void PlotWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton) {
        event->ignore();
        return;
    }
    if (event->button() == Qt::LeftButton && interactionMode_ == InteractionMode::Zoom && zooming_) {
        const QRectF pr = plotRect();
        const QRect oldDirty = zoomRubberBandDirtyRect(zoomRubberBand_);
        const QRectF band = QRectF(zoomStart_, event->position()).normalized().intersected(pr);
        bool viewChanged = false;
        if (band.width() > 8 && band.height() > 8) {
            const QRectF view = effectiveView();
            const QPointF p1 = pixelToData(band.bottomLeft(), view, pr);
            const QPointF p2 = pixelToData(band.topRight(), view, pr);
            view_ = QRectF(QPointF(p1.x(), p1.y()), QPointF(p2.x(), p2.y())).normalized();
            expandFlatRange(view_);
            hasView_ = true;
            invalidatePlotCache();
            viewChanged = true;
        }
        zoomRubberBand_ = {};
        zooming_ = false;
        if (viewChanged) {
            update();
        } else if (oldDirty.isValid() && !oldDirty.isEmpty()) {
            update(oldDirty);
        }
        dragging_ = false;
        unsetCursor();
        event->accept();
        return;
    }
    dragging_ = false;
    unsetCursor();
    if (interactionMode_ == InteractionMode::Pan || interactionMode_ == InteractionMode::Zoom) {
        update();
    }
}

void PlotWidget::wheelEvent(QWheelEvent* event)
{
    const QRectF pr = plotRect();
    if (!pr.contains(event->position())) {
        return;
    }
    QRectF view = effectiveView();
    const QPointF center = pixelToData(event->position(), view, pr);
    const double factor = event->angleDelta().y() > 0 ? 0.82 : 1.22;
    const double left = center.x() - (center.x() - view.left()) * factor;
    const double right = center.x() + (view.right() - center.x()) * factor;
    const double bottom = center.y() - (center.y() - view.top()) * factor;
    const double top = center.y() + (view.bottom() - center.y()) * factor;
    view = QRectF(QPointF(left, bottom), QPointF(right, top));
    expandFlatRange(view);
    view_ = view;
    hasView_ = true;
    invalidatePlotCache();
    if (interactionMode_ == InteractionMode::Point && pointTrackingActive_ && hoverSeriesLocked_) {
        schedulePointHoverUpdate(event->position());
    } else {
        updateHover(event->position());
    }
    update();
}

void PlotWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    invalidatePlotCache();
}

void PlotWidget::leaveEvent(QEvent*)
{
    if (interactionMode_ == InteractionMode::Point && pointTrackingActive_) {
        ++pointHoverGeneration_;
        pointHoverQueued_ = false;
    }
}
