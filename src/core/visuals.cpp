// SPDX-FileCopyrightText: 2026 Weikang Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mdsscope_internal.hpp"

QString colorForIndex(int index)
{
    static const QStringList colors = {
        "#2364aa", "#c44e52", "#2f855a", "#805ad5", "#d97706",
        "#0f766e", "#9f1239", "#4a5568", "#db2777", "#16a34a",
        "#ea580c", "#0891b2", "#7c3aed", "#ca8a04", "#0ea5e9",
        "#be123c"
    };
    return colors.at(index % colors.size());
}

bool isDefaultSeriesColor(const QString& colorName, int index)
{
    const QColor color(colorName);
    if (!color.isValid()) {
        return true;
    }
    return color.name().compare(QColor(colorForIndex(index)).name(), Qt::CaseInsensitive) == 0;
}

int colorIndexForName(const QString& colorName, int fallback)
{
    const QColor color(colorName);
    if (!color.isValid()) {
        return fallback;
    }
    const QString normalized = color.name().toLower();
    for (int i = 0; i < 32; ++i) {
        if (QColor(colorForIndex(i)).name().toLower() == normalized) {
            return i;
        }
    }
    return fallback;
}

void normalizePresetColors(QVector<SignalSpec>& specs)
{
    for (int i = 0; i < specs.size(); ++i) {
        if (!specs[i].manualColor) {
            specs[i].colorName = colorForIndex(i);
        }
    }
}

QString compactAxisValue(double value)
{
    if (!std::isfinite(value)) {
        return {};
    }
    const double absValue = std::abs(value);
    if (absValue >= 1000.0) {
        return QString::number(value, 'e', 1);
    }
    if (absValue > 0.0 && absValue < 0.001) {
        return QString::number(value, 'g', 2);
    }
    if (absValue >= 100.0) {
        return QString::number(value, 'f', 0);
    }
    if (absValue >= 10.0) {
        return QString::number(value, 'f', 1);
    }
    return QString::number(value, 'g', 3);
}

QStringList uniformAxisValues(const QVector<double>& values)
{
    QStringList labels;
    labels.reserve(values.size());
    bool scientific = false;
    double minStep = std::numeric_limits<double>::infinity();
    for (int i = 0; i < values.size(); ++i) {
        const double value = values[i];
        if (!std::isfinite(value)) {
            labels.push_back(QString());
            continue;
        }
        const double absValue = std::abs(value);
        if (absValue >= 1000.0 || (absValue > 0.0 && absValue < 0.001)) {
            scientific = true;
        }
        if (i > 0 && std::isfinite(values[i - 1])) {
            const double step = std::abs(value - values[i - 1]);
            if (step > 0.0) {
                minStep = std::min(minStep, step);
            }
        }
        labels.push_back(QString());
    }

    int decimals = 0;
    if (scientific) {
        decimals = 2;
    } else if (std::isfinite(minStep) && minStep > 0.0) {
        if (minStep >= 10.0) {
            decimals = 0;
        } else {
            decimals = std::clamp(static_cast<int>(std::ceil(-std::log10(minStep))) + 1, 0, 5);
        }
    }

    auto formatted = [&](int precision) {
        QStringList out;
        out.reserve(values.size());
        for (double value : values) {
            out.push_back(std::isfinite(value) ? QString::number(value, scientific ? 'e' : 'f', precision) : QString());
        }
        return out;
    };

    labels = formatted(decimals);
    while (!scientific && decimals < 6) {
        QSet<QString> seen;
        bool duplicate = false;
        for (const QString& label : labels) {
            if (label.isEmpty()) {
                continue;
            }
            if (seen.contains(label)) {
                duplicate = true;
                break;
            }
            seen.insert(label);
        }
        if (!duplicate) {
            break;
        }
        labels = formatted(++decimals);
    }
    return labels;
}

static QPixmap appIconPixmap(int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double s = static_cast<double>(size);
    const QRectF outer(s * 0.07, s * 0.07, s * 0.86, s * 0.86);

    QPainterPath body;
    body.addRoundedRect(outer, s * 0.18, s * 0.18);
    QLinearGradient background(outer.topLeft(), outer.bottomRight());
    background.setColorAt(0.0, QColor("#0f172a"));
    background.setColorAt(0.55, QColor("#1e293b"));
    background.setColorAt(1.0, QColor("#0b1120"));
    painter.setPen(QPen(QColor("#334155"), std::max(1.0, s * 0.025)));
    painter.setBrush(background);
    painter.drawPath(body);

    painter.save();
    painter.setClipPath(body);

    const QRectF plot(s * 0.20, s * 0.22, s * 0.61, s * 0.55);
    painter.setPen(QPen(QColor(148, 163, 184, 58), std::max(1.0, s * 0.009)));
    for (int i = 1; i <= 3; ++i) {
        const double x = plot.left() + plot.width() * i / 4.0;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const double y = plot.top() + plot.height() * i / 4.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    painter.setPen(QPen(QColor("#64748b"), std::max(1.0, s * 0.018), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));
    painter.drawLine(QPointF(plot.left(), plot.top()), QPointF(plot.left(), plot.bottom()));

    QPainterPath slowWave;
    slowWave.moveTo(plot.left(), plot.center().y() + plot.height() * 0.13);
    slowWave.cubicTo(plot.left() + plot.width() * 0.18, plot.top() + plot.height() * 0.08,
                     plot.left() + plot.width() * 0.37, plot.bottom() - plot.height() * 0.08,
                     plot.left() + plot.width() * 0.55, plot.center().y() + plot.height() * 0.02);
    slowWave.cubicTo(plot.left() + plot.width() * 0.70, plot.top() + plot.height() * 0.12,
                     plot.left() + plot.width() * 0.83, plot.bottom() - plot.height() * 0.17,
                     plot.right(), plot.top() + plot.height() * 0.22);
    painter.setPen(QPen(QColor("#22d3ee"), std::max(1.6, s * 0.05), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(slowWave);

    QPainterPath fastWave;
    fastWave.moveTo(plot.left() + plot.width() * 0.04, plot.bottom() - plot.height() * 0.18);
    fastWave.lineTo(plot.left() + plot.width() * 0.19, plot.bottom() - plot.height() * 0.18);
    fastWave.lineTo(plot.left() + plot.width() * 0.28, plot.top() + plot.height() * 0.20);
    fastWave.lineTo(plot.left() + plot.width() * 0.39, plot.bottom() - plot.height() * 0.18);
    fastWave.lineTo(plot.left() + plot.width() * 0.57, plot.bottom() - plot.height() * 0.18);
    fastWave.lineTo(plot.left() + plot.width() * 0.68, plot.top() + plot.height() * 0.30);
    fastWave.lineTo(plot.left() + plot.width() * 0.78, plot.bottom() - plot.height() * 0.18);
    fastWave.lineTo(plot.right() - plot.width() * 0.04, plot.bottom() - plot.height() * 0.18);
    painter.setPen(QPen(QColor("#f97316"), std::max(1.4, s * 0.036), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(fastWave);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#f8fafc"));
    painter.drawEllipse(QPointF(plot.left() + plot.width() * 0.55, plot.center().y() + plot.height() * 0.02),
                         s * 0.035,
                         s * 0.035);
    painter.setBrush(QColor("#f97316"));
    painter.drawEllipse(QPointF(plot.left() + plot.width() * 0.68, plot.top() + plot.height() * 0.30),
                         s * 0.03,
                         s * 0.03);
    painter.restore();

    painter.setPen(QPen(QColor(255, 255, 255, 42), std::max(1.0, s * 0.014)));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outer.adjusted(s * 0.035, s * 0.035, -s * 0.035, -s * 0.035), s * 0.13, s * 0.13);
    return pixmap;
}

QIcon appIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addPixmap(appIconPixmap(size));
    }
    return icon;
}

QIcon gearIcon()
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF center(11.0, 11.0);
    QPainterPath teeth;
    for (int i = 0; i < 8; ++i) {
        const double angle = (M_PI * 2.0 * i / 8.0) + M_PI / 8.0;
        const QPointF outer(center.x() + std::cos(angle) * 8.7,
                            center.y() + std::sin(angle) * 8.7);
        QRectF tooth(outer.x() - 1.8, outer.y() - 1.8, 3.6, 3.6);
        teeth.addEllipse(tooth);
    }
    painter.setPen(QPen(QColor("#475569"), 1.7));
    painter.setBrush(QColor("#64748b"));
    painter.drawPath(teeth);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, 6.9, 6.9);
    painter.drawEllipse(center, 2.5, 2.5);
    return QIcon(pixmap);
}

QIcon fontIcon()
{
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#475569"), 1.9));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(2.3, 2.3, 23.4, 23.4), 2.2, 2.2);
    QFont iconFont(QStringLiteral("Times New Roman"), 22, QFont::Bold);
    painter.setFont(iconFont);
    painter.setPen(QColor("#2563eb"));
    painter.drawText(pixmap.rect().adjusted(0, -2, 0, 2), Qt::AlignCenter, "A");
    return QIcon(pixmap);
}

QIcon saveIcon()
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#334155"), 1.6));
    painter.setBrush(QColor("#e2e8f0"));
    painter.drawRoundedRect(QRectF(2.5, 2.0, 17.0, 18.5), 1.8, 1.8);
    painter.setBrush(QColor("#475569"));
    painter.drawRect(QRectF(5.0, 4.5, 11.0, 5.8));
    painter.setBrush(QColor("#ffffff"));
    painter.drawRect(QRectF(6.0, 13.2, 10.0, 5.2));
    painter.setPen(QPen(QColor("#94a3b8"), 1.0));
    painter.drawLine(QPointF(7.8, 15.3), QPointF(14.2, 15.3));
    painter.drawLine(QPointF(7.8, 17.0), QPointF(14.2, 17.0));
    return QIcon(pixmap);
}

QIcon loginIcon(bool loggedIn)
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor frame = loggedIn ? QColor("#15803d") : QColor("#64748b");
    const QColor fill = loggedIn ? QColor("#dcfce7") : QColor("#e2e8f0");
    const QColor person = loggedIn ? QColor("#16a34a") : QColor("#94a3b8");

    painter.setPen(QPen(frame, 1.5));
    painter.drawLine(QPointF(6.5, 1.3), QPointF(11.0, 4.0));
    painter.drawLine(QPointF(15.5, 1.3), QPointF(11.0, 4.0));

    painter.setPen(QPen(frame, 1.6));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(1.5, 3.5, 19.0, 17.7), 2.0, 2.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(frame);
    painter.drawRoundedRect(QRectF(6.7, 2.7, 8.6, 2.7), 0.9, 0.9);

    painter.setBrush(person);
    painter.drawEllipse(QPointF(11.0, 10.3), 3.2, 3.2);

    QPainterPath shoulders;
    shoulders.moveTo(4.2, 18.0);
    shoulders.cubicTo(5.3, 13.1, 16.7, 13.1, 17.8, 18.0);
    shoulders.lineTo(17.8, 19.2);
    shoulders.lineTo(4.2, 19.2);
    shoulders.closeSubpath();
    painter.drawPath(shoulders);

    return QIcon(pixmap);
}

QIcon modeIcon(InteractionMode mode, bool active)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor color = active ? QColor("#38bdf8") : QColor("#9ca3af");
    painter.setPen(QPen(color, 2.3));
    painter.setBrush(Qt::NoBrush);
    if (mode == InteractionMode::Zoom) {
        painter.drawEllipse(QRectF(4.0, 4.0, 11.8, 11.8));
        painter.drawLine(QPointF(14.5, 14.5), QPointF(19.8, 19.8));
        painter.drawLine(QPointF(7.2, 9.9), QPointF(12.6, 9.9));
        painter.drawLine(QPointF(9.9, 7.2), QPointF(9.9, 12.6));
    } else if (mode == InteractionMode::Point) {
        painter.drawLine(QPointF(12.0, 3.8), QPointF(12.0, 20.2));
        painter.drawLine(QPointF(3.8, 12.0), QPointF(20.2, 12.0));
        painter.setBrush(color);
        painter.drawEllipse(QPointF(12.0, 12.0), 2.9, 2.9);
    } else {
        painter.setPen(color);
        QFont iconFont = painter.font();
        iconFont.setPointSize(17);
        painter.setFont(iconFont);
        painter.drawText(pixmap.rect(), Qt::AlignCenter, QString::fromUtf8("✋"));
    }
    return QIcon(pixmap);
}

