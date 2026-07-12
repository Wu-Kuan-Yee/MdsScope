// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"
#include "helpers.hpp"

QRect PlotWidget::syncedPointDirtyRect(const PointReadout& readout) const
{
    if (!readout.visible || !readout.plotRect.contains(readout.pixel)) {
        return {};
    }
    QRect dirty(qRound(readout.pixel.x()) - 1,
                qRound(readout.plotRect.top()),
                3,
                std::max(1, qRound(readout.plotRect.height())));
    dirty = dirty.united(QRect(qRound(readout.plotRect.left()),
                               qRound(readout.pixel.y()) - 1,
                               std::max(1, qRound(readout.plotRect.width())),
                               3));
    if (readout.showText && !readout.text.isEmpty()) {
        QRect textRect(qRound(readout.pixel.x()) + 4, qRound(readout.pixel.y()) - 20, 188, 22);
        if (textRect.right() > qRound(readout.plotRect.right())) {
            textRect.moveRight(qRound(readout.pixel.x()) - 4);
        }
        if (textRect.left() < qRound(readout.plotRect.left())) {
            textRect.moveLeft(qRound(readout.plotRect.left()) + 2);
        }
        if (textRect.top() < qRound(readout.plotRect.top())) {
            textRect.moveTop(qRound(readout.pixel.y()) + 4);
        }
        dirty = dirty.united(textRect);
    }
    return dirty.adjusted(-2, -2, 2, 2).intersected(rect());
}

void PlotWidget::drawSyncedPoint(QPainter& painter) const
{
    if (!syncedPoint_.visible || !syncedPoint_.plotRect.contains(syncedPoint_.pixel)) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(syncedPoint_.color, 1));
    painter.drawLine(QPointF(syncedPoint_.pixel.x(), syncedPoint_.plotRect.top()),
                     QPointF(syncedPoint_.pixel.x(), syncedPoint_.plotRect.bottom()));
    painter.drawLine(QPointF(syncedPoint_.plotRect.left(), syncedPoint_.pixel.y()),
                     QPointF(syncedPoint_.plotRect.right(), syncedPoint_.pixel.y()));
    if (syncedPoint_.showText && !syncedPoint_.text.isEmpty()) {
        QRectF textRect(syncedPoint_.pixel.x() + 4, syncedPoint_.pixel.y() - 20, 188, 22);
        if (textRect.right() > syncedPoint_.plotRect.right()) {
            textRect.moveRight(syncedPoint_.pixel.x() - 4);
        }
        if (textRect.left() < syncedPoint_.plotRect.left()) {
            textRect.moveLeft(syncedPoint_.plotRect.left() + 2);
        }
        if (textRect.top() < syncedPoint_.plotRect.top()) {
            textRect.moveTop(syncedPoint_.pixel.y() + 4);
        }
        QFont pointFont = painter.font();
        pointFont.setPointSize(std::max(8, pointFont.pointSize() + 1));
        pointFont.setBold(false);
        painter.setFont(pointFont);
        painter.setPen(syncedPoint_.color);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, syncedPoint_.text);
    }
    painter.restore();
}

void PlotWidget::drawZoomRubberBand(QPainter& painter) const
{
    if (!zooming_ || !zoomRubberBand_.isValid()) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor("#1d4ed8"), 1, Qt::DashLine));
    painter.setBrush(QColor(37, 99, 235, 35));
    painter.drawRect(zoomRubberBand_.normalized());
    painter.restore();
}

void PlotWidget::drawSelectionBorder(QPainter& painter) const
{
    if (!selected_) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor("#ff00ff"), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    painter.restore();
}

QRect PlotWidget::selectionBorderDirtyRect() const
{
    if (rect().isEmpty()) {
        return {};
    }
    const QRect outer = rect();
    return outer.adjusted(0, 0, -1, -1);
}

QRect PlotWidget::zoomRubberBandDirtyRect(const QRectF& band) const
{
    if (!band.isValid() || band.isEmpty()) {
        return {};
    }
    return band.normalized().toAlignedRect().adjusted(-3, -3, 3, 3).intersected(rect());
}

void PlotWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setClipRegion(event->region());
    const qreal dpr = devicePixelRatioF();
    const QSize pixmapSize(qCeil(width() * dpr), qCeil(height() * dpr));
    // Reallocate the backing pixmap only when the device size actually changes
    // (widget resize or dpr change). During pan/zoom the size is stable and
    // only baseCacheDirty_ is set, so the buffer is reused and cleared in place
    // (refcount is 1 between frames — the prior drawPixmap released its ref),
    // avoiding a full-window alloc/free every frame.
    if (baseCache_.isNull() || baseCache_.size() != pixmapSize) {
        baseCache_ = QPixmap(pixmapSize);
        baseCache_.setDevicePixelRatio(dpr);
        baseCacheDirty_ = true;
    }
    if (baseCacheDirty_ || baseCacheSize_ != size()) {
        baseCache_.fill(Qt::transparent);
        QPainter cachePainter(&baseCache_);
        renderBasePlot(cachePainter);
        baseCacheSize_ = size();
        baseCacheDirty_ = false;
    }
    painter.drawPixmap(0, 0, baseCache_);
    drawSelectionBorder(painter);
    drawSyncedPoint(painter);
    drawZoomRubberBand(painter);
}

void PlotWidget::renderBasePlot(QPainter& painter) const
{
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QPalette pal = palette();
    const QColor background = pal.color(QPalette::Base);
    const QColor frame = pal.color(QPalette::Mid);
    const QColor plotFrame = pal.color(QPalette::Midlight);
    const QColor textColor = pal.color(QPalette::Text);
    const QColor subtleText = pal.color(QPalette::Disabled, QPalette::Text);
    const QColor gridColor = pal.color(QPalette::AlternateBase).isValid()
                                 ? pal.color(QPalette::AlternateBase)
                                 : pal.color(QPalette::Midlight);
    const FontSettings& fonts = fontSettings();
    QFont axisFont(fonts.family, fonts.axisSize);
    int plotPointSize = fonts.axisSize;
    if (largeDisplayMode_) {
        plotPointSize += 4;
    }
    axisFont.setPointSize(std::max(7, plotPointSize));
    painter.setFont(axisFont);
    painter.fillRect(rect(), background);
    painter.setPen(QPen(frame, 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    const QRectF pr = plotRect();
    const QRectF view = effectiveView();
    painter.setPen(plotFrame);
    painter.drawRect(pr);
    if (spec_.grid) {
        painter.setPen(QPen(gridColor, 1));
        for (int i = 1; i < 5; ++i) {
            const double x = pr.left() + pr.width() * i / 5.0;
            const double y = pr.top() + pr.height() * i / 5.0;
            painter.drawLine(QPointF(x, pr.top()), QPointF(x, pr.bottom()));
            painter.drawLine(QPointF(pr.left(), y), QPointF(pr.right(), y));
        }
    }

    painter.setClipRect(pr.adjusted(1, 1, -1, -1));
    for (int i = 0; i < series_.size(); ++i) {
        if (i < spec_.signalSpecs.size() && spec_.signalSpecs[i].hidden) {
            continue;
        }
        const auto& s = series_[i];
        const QVector<QPointF> displayPoints = displayPointsForSeries(s, view, pr.width());
        if (displayPoints.size() < 2) {
            continue;
        }
        QVector<QPoint> polyline;
        polyline.reserve(displayPoints.size() + 1);
        QPointF pixel = dataToPixel(displayPoints.front(), view, pr);
        polyline.push_back(QPoint(qRound(pixel.x()), qRound(pixel.y())));
        const int pointCount = static_cast<int>(displayPoints.size());
        const int stride = std::max(1, pointCount / std::max(1, static_cast<int>(pr.width() * 2)));
        for (int p = stride; p < displayPoints.size(); p += stride) {
            pixel = dataToPixel(displayPoints[p], view, pr);
            polyline.push_back(QPoint(qRound(pixel.x()), qRound(pixel.y())));
        }
        pixel = dataToPixel(displayPoints.back(), view, pr);
        polyline.push_back(QPoint(qRound(pixel.x()), qRound(pixel.y())));
        painter.setPen(QPen(seriesColor(i), 1));
        painter.drawPolyline(polyline.constData(), polyline.size());
    }
    painter.setClipping(false);

    painter.setPen(textColor);
    const QString titleText = spec_.title.trimmed();
    if (!titleText.isEmpty()) {
        QFont titleFont(fonts.family, fonts.legendSize + (largeDisplayMode_ ? 4 : 0));
        titleFont.setBold(true);
        painter.setFont(titleFont);
        const QFontMetrics titleFm(titleFont);
        const QString elidedTitle = titleFm.elidedText(titleText, Qt::ElideRight, std::max(20, static_cast<int>(pr.width() - 16)));
        painter.drawText(QRectF(pr.left() + 8, pr.top() + 2, pr.width() - 16, std::max(14, titleFm.height())),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         elidedTitle);
        painter.setFont(axisFont);
    }

    const int yTickCount = std::clamp(static_cast<int>(pr.height() / 34.0) + 1, 3, 6);
    const int xTickCount = std::clamp(static_cast<int>(pr.width() / 78.0) + 1, 3, 7);
    const QString yUnit = spec_.yLabel.trimmed().isEmpty() ? QStringLiteral("a.u.") : spec_.yLabel.trimmed();
    const QFontMetrics axisFm(axisFont);
    const double yLabelLeft = pr.left() + 4.0;
    QVector<double> yValues;
    yValues.reserve(yTickCount);
    for (int i = 0; i < yTickCount; ++i) {
        const double ratio = yTickCount == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(yTickCount - 1);
        const double yValue = view.bottom() - view.height() * ratio;
        yValues.push_back(yValue);
    }
    const QStringList yLabels = uniformAxisValues(yValues);
    int maxYLabelAdvance = 0;
    for (const QString& label : std::as_const(yLabels)) {
        maxYLabelAdvance = std::max(maxYLabelAdvance, axisFm.horizontalAdvance(label));
    }
    const int yLabelWidth = std::clamp(maxYLabelAdvance + 6,
                                       42,
                                       largeDisplayMode_ ? 110 : 78);
    const double yUnitLeft = pr.left() + 1.0;
    const int axisLabelHeight = std::max(14, axisFm.height() + 2);
    const double halfAxisLabelHeight = axisLabelHeight * 0.5;
    QVector<QRectF> yLabelRects;
    yLabelRects.reserve(yTickCount);
    QVector<QRectF> axisLabelRects;
    axisLabelRects.reserve(yTickCount + xTickCount + 1);
    for (int i = 0; i < yTickCount; ++i) {
        const double ratio = yTickCount == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(yTickCount - 1);
        const double yPixel = pr.top() + pr.height() * ratio;
        painter.drawLine(QPointF(pr.left(), yPixel), QPointF(pr.left() + 3, yPixel));
        double labelTop = yPixel - halfAxisLabelHeight;
        const double minLabelTop = pr.top() + 1.0;
        const double maxLabelTop = pr.bottom() - axisLabelHeight - 1.0;
        if (maxLabelTop >= minLabelTop) {
            labelTop = std::clamp(labelTop, minLabelTop, maxLabelTop);
        } else {
            labelTop = pr.center().y() - halfAxisLabelHeight;
        }
        const QRectF labelRect(yLabelLeft, labelTop, yLabelWidth, axisLabelHeight);
        yLabelRects.push_back(labelRect);
        axisLabelRects.push_back(labelRect);
        painter.drawText(labelRect,
                         Qt::AlignLeft | Qt::AlignVCenter,
                         yLabels[i]);
    }
    if (!yUnit.isEmpty()) {
        QFont unitFont(fonts.family, fonts.unitSize + (largeDisplayMode_ ? 4 : 0));
        painter.setFont(unitFont);
        const QFontMetrics unitFm(unitFont);
        QString yUnitText = yUnit;
        const int maxUnitAdvance = std::max(14, static_cast<int>(pr.height() - 4.0));
        if (unitFm.horizontalAdvance(yUnitText) + 6 > maxUnitAdvance) {
            yUnitText = unitFm.elidedText(yUnitText, Qt::ElideRight, maxUnitAdvance - 6);
        }
        const double unitAdvance = std::max(8, unitFm.horizontalAdvance(yUnitText)) + 6.0;
        const double unitVisualWidth = std::max(9.0, static_cast<double>(unitFm.height()));
        const double unitX = yUnitLeft + unitVisualWidth / 2.0;
        const double unitHalfHeight = unitAdvance / 2.0;
        const double unitHalfWidth = unitVisualWidth / 2.0;
        double unitY = pr.center().y();
        double bestGapDistance = std::numeric_limits<double>::infinity();
        for (int i = 0; i + 1 < yLabelRects.size(); ++i) {
            const double gapTop = yLabelRects[i].bottom();
            const double gapBottom = yLabelRects[i + 1].top();
            if (gapBottom <= gapTop) {
                continue;
            }
            const double candidateY = (gapTop + gapBottom) * 0.5;
            const double distance = std::abs(candidateY - pr.center().y());
            if (distance < bestGapDistance) {
                bestGapDistance = distance;
                unitY = candidateY;
            }
        }
        const double minUnitY = pr.top() + unitHalfHeight + 1.0;
        const double maxUnitY = pr.bottom() - unitHalfHeight - 1.0;
        if (maxUnitY >= minUnitY) {
            unitY = std::clamp(unitY, minUnitY, maxUnitY);
        }
        painter.save();
        const QPointF unitCenter(unitX, unitY);
        painter.translate(unitCenter);
        painter.rotate(-90.0);
        const QRectF unitRect(-unitAdvance / 2.0, -unitVisualWidth / 2.0, unitAdvance, unitVisualWidth);
        painter.drawText(unitRect, Qt::AlignCenter, yUnitText);
        painter.restore();
        axisLabelRects.push_back(QRectF(unitX - unitHalfWidth, unitY - unitHalfHeight, unitVisualWidth, unitAdvance));
        painter.setFont(axisFont);
    }

    painter.setPen(textColor);
    QVector<double> xTickPixels;
    xTickPixels.reserve(xTickCount);
    QVector<double> xValues;
    xValues.reserve(xTickCount);
    for (int i = 0; i < xTickCount; ++i) {
        const double ratio = xTickCount == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(xTickCount - 1);
        const double xValue = view.left() + view.width() * ratio;
        xValues.push_back(xValue);
        const double xPixel = pr.left() + pr.width() * ratio;
        xTickPixels.push_back(xPixel);
    }
    const QStringList xLabels = uniformAxisValues(xValues);
    for (int i = 0; i < xTickCount; ++i) {
        const double xPixel = xTickPixels[i];
        painter.drawLine(QPointF(xPixel, pr.bottom()), QPointF(xPixel, pr.bottom() + 2));
        QRectF textRect(xPixel - 38, pr.bottom() + 1, 76, axisLabelHeight);
        Qt::Alignment alignment = Qt::AlignHCenter | Qt::AlignVCenter;
        if (i == 0) {
            textRect.moveLeft(pr.left());
            alignment = Qt::AlignLeft | Qt::AlignVCenter;
        } else if (i == xTickCount - 1) {
            textRect.moveRight(pr.right());
            alignment = Qt::AlignRight | Qt::AlignVCenter;
        }
        axisLabelRects.push_back(textRect);
        painter.drawText(textRect, alignment, xLabels.value(i));
    }
    const QString xUnit = spec_.xLabel.trimmed().isEmpty() ? QStringLiteral("s") : spec_.xLabel.trimmed();
    double xUnitCenter = pr.center().x();
    if (xTickPixels.size() >= 2) {
        double bestDistance = std::numeric_limits<double>::infinity();
        for (int i = 0; i + 1 < xTickPixels.size(); ++i) {
            const double candidate = (xTickPixels[i] + xTickPixels[i + 1]) * 0.5;
            const double distance = std::abs(candidate - pr.center().x());
            if (distance < bestDistance) {
                bestDistance = distance;
                xUnitCenter = candidate;
            }
        }
    }
    const QRectF xUnitRect(xUnitCenter - 30, pr.bottom() - 14, 60, 12);
    axisLabelRects.push_back(xUnitRect);
    QFont unitFont(fonts.family, fonts.unitSize + (largeDisplayMode_ ? 4 : 0));
    painter.setFont(unitFont);
    painter.drawText(xUnitRect, Qt::AlignCenter, xUnit);

    QFont legendFont(fonts.family, fonts.legendSize + (largeDisplayMode_ ? 4 : 0));
    const QFontMetrics legendFm(legendFont);
    int legendWidth = 0;
    for (const QString& label : legendLabels_) {
        legendWidth = std::max(legendWidth, legendFm.horizontalAdvance(label));
    }
    if (!legendLabels_.isEmpty()) {
        legendWidth += 20;
        const int legendLineHeight = std::max(10, legendFm.height());
        const int legendHeight = static_cast<int>(legendLabels_.size()) * legendLineHeight + 4;
        const int pad = 1;
        const QRectF legendArea = pr.adjusted(2, 2, -2, -2);
        const double legendAreaWidth = std::max(1.0, legendArea.width());
        const double legendAreaHeight = std::max(1.0, legendArea.height());
        const double legendW = std::min<double>(legendWidth, legendAreaWidth);
        const double legendH = std::min<double>(legendHeight, legendAreaHeight);
        const QRectF legendRect(legendArea.right() - legendW - pad,
                                legendArea.top() + pad,
                                legendW,
                                legendH);
        painter.save();
        painter.setFont(legendFont);
        int legendX = static_cast<int>(legendRect.left()) + 2;
        int legendY = static_cast<int>(legendRect.top()) + 2;
        for (int i = 0; i < legendLabels_.size(); ++i) {
            if (legendY + legendLineHeight > pr.bottom()) {
                break;
            }
            const int seriesIndex = legendSeriesIndexes_.value(i, i);
            const QColor color = seriesColor(seriesIndex);
            const int squareSize = std::max(6, std::min(9, legendLineHeight - 3));
            const QRectF swatchRect(legendX,
                                    legendY + (legendLineHeight - squareSize) / 2.0,
                                    squareSize,
                                    squareSize);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawRect(swatchRect);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(color);
            painter.drawText(QRectF(legendX + squareSize + 4, legendY, legendRect.width() - squareSize - 6, legendLineHeight),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             legendLabels_[i]);
            legendY += legendLineHeight;
        }
        painter.restore();
    }
    QStringList errors;
    bool hasPoints = false;
    for (const auto& s : series_) {
        hasPoints = hasPoints || s.hasData();
        if (!s.error.isEmpty()) {
            errors.push_back(s.name + ": " + s.error);
        }
    }
    if (!hasPoints) {
        painter.setPen(subtleText);
        const QString text = errors.isEmpty() ? "No data loaded" : errors.join("; ");
        painter.drawText(pr.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, text);
    }
}
