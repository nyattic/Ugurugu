#include "ui/Icons.hpp"

#include "ui/Theme.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <cmath>
#include <initializer_list>
#include <numbers>

namespace wobble {

namespace {

struct Glyph {
    QVector<QPolygonF> lines;
    QVector<QPolygonF> fills;
};

QPolygonF polyline(std::initializer_list<QPointF> points)
{
    QPolygonF result;
    result.reserve(static_cast<int>(points.size()));
    for (const QPointF &point : points) {
        result.append(point);
    }
    return result;
}

QPolygonF sampleQuad(
    const QPointF &start,
    const QPointF &control,
    const QPointF &end,
    int steps)
{
    QPolygonF result;
    result.reserve(steps + 1);
    for (int step = 0; step <= steps; ++step) {
        const qreal t = static_cast<qreal>(step) / steps;
        const qreal u = 1.0 - t;
        result.append(
            start * (u * u) + control * (2.0 * u * t) + end * (t * t));
    }
    return result;
}

QPolygonF circle(const QPointF &center, qreal radius, int steps)
{
    QPolygonF result;
    result.reserve(steps + 1);
    for (int step = 0; step <= steps; ++step) {
        const qreal angle =
            2.0 * std::numbers::pi_v<qreal> * step / steps;
        result.append(
            center
            + QPointF(
                radius * std::cos(angle),
                radius * std::sin(angle)));
    }
    return result;
}

QPointF direction(qreal degrees)
{
    const qreal radians = degrees * std::numbers::pi_v<qreal> / 180.0;
    return QPointF(std::cos(radians), -std::sin(radians));
}

void addArrowHead(
    Glyph &glyph,
    const QPointF &tip,
    qreal travelDegrees,
    qreal length)
{
    const QPointF left = direction(travelDegrees + 180.0 - 32.0);
    const QPointF right = direction(travelDegrees + 180.0 + 32.0);
    glyph.lines.append(polyline({tip + left * length, tip}));
    glyph.lines.append(polyline({tip, tip + right * length}));
}

Glyph brushGlyph()
{
    Glyph glyph;
    glyph.lines.append(polyline({{18.6, 3.6}, {11.4, 10.8}}));
    glyph.fills.append(polyline({
        {11.4, 10.8},
        {13.0, 12.8},
        {10.6, 16.4},
        {6.6, 18.8},
        {4.8, 17.2},
        {7.6, 13.4}
    }));
    return glyph;
}

Glyph eraserGlyph()
{
    Glyph glyph;
    glyph.lines.append(polyline({
        {14.6, 4.4},
        {19.8, 9.6},
        {11.6, 17.6},
        {6.4, 12.4},
        {14.6, 4.4}
    }));
    glyph.lines.append(polyline({{9.2, 9.7}, {14.4, 14.9}}));
    glyph.lines.append(polyline({{5.2, 20.4}, {15.4, 20.4}}));
    return glyph;
}

Glyph undoGlyph()
{
    Glyph glyph;
    glyph.lines.append(
        sampleQuad({19.0, 15.6}, {17.4, 5.6}, {6.4, 9.6}, 22));
    addArrowHead(glyph, {6.4, 9.6}, 199.0, 4.4);
    return glyph;
}

Glyph redoGlyph()
{
    Glyph glyph;
    glyph.lines.append(
        sampleQuad({5.0, 15.6}, {6.6, 5.6}, {17.6, 9.6}, 22));
    addArrowHead(glyph, {17.6, 9.6}, -19.0, 4.4);
    return glyph;
}

Glyph playGlyph()
{
    Glyph glyph;
    glyph.fills.append(polyline({
        {8.6, 5.4},
        {19.2, 12.0},
        {8.6, 18.6}
    }));
    return glyph;
}

Glyph pauseGlyph()
{
    Glyph glyph;
    glyph.fills.append(polyline({
        {7.2, 5.6}, {10.4, 5.6}, {10.4, 18.4}, {7.2, 18.4}
    }));
    glyph.fills.append(polyline({
        {13.6, 5.6}, {16.8, 5.6}, {16.8, 18.4}, {13.6, 18.4}
    }));
    return glyph;
}

Glyph addGlyph()
{
    Glyph glyph;
    glyph.lines.append(polyline({{12.0, 5.4}, {12.0, 18.6}}));
    glyph.lines.append(polyline({{5.4, 12.0}, {18.6, 12.0}}));
    return glyph;
}

Glyph duplicateGlyph()
{
    Glyph glyph;
    glyph.lines.append(polyline({
        {8.6, 6.4}, {19.2, 6.4}, {19.2, 15.2}
    }));
    glyph.lines.append(polyline({
        {4.8, 9.0},
        {15.2, 9.0},
        {15.2, 19.4},
        {4.8, 19.4},
        {4.8, 9.0}
    }));
    return glyph;
}

Glyph removeGlyph()
{
    Glyph glyph;
    glyph.lines.append(polyline({{6.0, 12.0}, {18.0, 12.0}}));
    return glyph;
}

Glyph moveUpGlyph()
{
    Glyph glyph;
    glyph.lines.append(polyline({{6.4, 14.6}, {12.0, 8.6}, {17.6, 14.6}}));
    return glyph;
}

Glyph moveDownGlyph()
{
    Glyph glyph;
    glyph.lines.append(polyline({{6.4, 9.4}, {12.0, 15.4}, {17.6, 9.4}}));
    return glyph;
}

Glyph eyeOpenGlyph()
{
    Glyph glyph;
    QPolygonF outline = sampleQuad({3.8, 12.0}, {12.0, 5.2}, {20.2, 12.0}, 14);
    outline += sampleQuad({20.2, 12.0}, {12.0, 18.8}, {3.8, 12.0}, 14);
    glyph.lines.append(outline);
    glyph.fills.append(circle({12.0, 12.0}, 2.5, 18));
    return glyph;
}

Glyph eyeClosedGlyph()
{
    Glyph glyph;
    glyph.lines.append(
        sampleQuad({3.8, 12.0}, {12.0, 18.8}, {20.2, 12.0}, 16));
    glyph.lines.append(polyline({{7.0, 15.7}, {5.6, 18.4}}));
    glyph.lines.append(polyline({{12.0, 17.4}, {12.0, 20.2}}));
    glyph.lines.append(polyline({{17.0, 15.7}, {18.4, 18.4}}));
    return glyph;
}

Glyph fitViewGlyph()
{
    Glyph glyph;
    glyph.lines.append(polyline({{4.6, 9.2}, {4.6, 4.6}, {9.2, 4.6}}));
    glyph.lines.append(polyline({{14.8, 4.6}, {19.4, 4.6}, {19.4, 9.2}}));
    glyph.lines.append(polyline({{19.4, 14.8}, {19.4, 19.4}, {14.8, 19.4}}));
    glyph.lines.append(polyline({{9.2, 19.4}, {4.6, 19.4}, {4.6, 14.8}}));
    return glyph;
}

Glyph lassoGlyph()
{
    Glyph glyph;
    QPolygonF loop;
    for (int step = 0; step <= 26; ++step) {
        const qreal angle =
            2.0 * std::numbers::pi_v<qreal> * step / 26.0;
        loop.append(QPointF(
            12.0 + 7.2 * std::cos(angle),
            9.8 + 5.4 * std::sin(angle)));
    }
    glyph.lines.append(loop);
    glyph.lines.append(
        sampleQuad({13.6, 15.0}, {10.0, 17.4}, {6.4, 19.6}, 10));
    glyph.lines.append(polyline({{6.4, 19.6}, {9.2, 20.8}}));
    return glyph;
}

Glyph wandGlyph()
{
    Glyph glyph;
    glyph.lines.append(polyline({{15.8, 8.2}, {6.6, 17.4}}));
    glyph.lines.append(polyline({{18.4, 2.8}, {18.4, 5.4}}));
    glyph.lines.append(polyline({{18.4, 7.4}, {18.4, 10.0}}));
    glyph.lines.append(polyline({{15.0, 6.4}, {17.4, 6.4}}));
    glyph.lines.append(polyline({{19.4, 6.4}, {21.8, 6.4}}));
    glyph.fills.append(circle({20.6, 11.6}, 1.0, 10));
    return glyph;
}

Glyph bucketGlyph()
{
    Glyph glyph;
    glyph.lines.append(
        sampleQuad({7.2, 9.6}, {12.0, 3.6}, {16.8, 9.6}, 12));
    glyph.lines.append(polyline({{5.8, 9.6}, {18.2, 9.6}}));
    glyph.lines.append(polyline({
        {6.8, 9.6},
        {8.4, 19.2},
        {15.6, 19.2},
        {17.2, 9.6}
    }));
    glyph.fills.append(circle({20.2, 14.4}, 1.4, 12));
    return glyph;
}

Glyph settingsGlyph()
{
    Glyph glyph;
    glyph.lines.append(circle({12.0, 12.0}, 3.2, 20));
    QPolygonF outer;
    for (int step = 0; step <= 24; ++step) {
        const qreal angle =
            2.0 * std::numbers::pi_v<qreal> * step / 24.0;
        const qreal radius = step % 3 == 1 ? 9.0 : 7.0;
        outer.append(
            QPointF(
                12.0 + radius * std::cos(angle),
                12.0 + radius * std::sin(angle)));
    }
    glyph.lines.append(outer);
    return glyph;
}

Glyph glyphFor(IconGlyph glyph)
{
    switch (glyph) {
    case IconGlyph::Brush:
        return brushGlyph();
    case IconGlyph::Eraser:
        return eraserGlyph();
    case IconGlyph::Undo:
        return undoGlyph();
    case IconGlyph::Redo:
        return redoGlyph();
    case IconGlyph::Play:
        return playGlyph();
    case IconGlyph::Pause:
        return pauseGlyph();
    case IconGlyph::Add:
        return addGlyph();
    case IconGlyph::Duplicate:
        return duplicateGlyph();
    case IconGlyph::Remove:
        return removeGlyph();
    case IconGlyph::MoveUp:
        return moveUpGlyph();
    case IconGlyph::MoveDown:
        return moveDownGlyph();
    case IconGlyph::EyeOpen:
        return eyeOpenGlyph();
    case IconGlyph::EyeClosed:
        return eyeClosedGlyph();
    case IconGlyph::FitView:
        return fitViewGlyph();
    case IconGlyph::Lasso:
        return lassoGlyph();
    case IconGlyph::Wand:
        return wandGlyph();
    case IconGlyph::Bucket:
        return bucketGlyph();
    case IconGlyph::Settings:
        return settingsGlyph();
    }
    return {};
}

quint64 mixSeed(quint64 value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

qreal seededAngle(quint64 seed, int channel)
{
    return static_cast<qreal>(mixSeed(seed + channel) % 6283) / 1000.0;
}

QPolygonF densify(const QPolygonF &source, qreal step)
{
    QPolygonF result;
    if (source.isEmpty()) {
        return result;
    }
    result.append(source.first());
    for (int index = 1; index < source.size(); ++index) {
        const QPointF from = source[index - 1];
        const QPointF to = source[index];
        const qreal length = std::hypot(to.x() - from.x(), to.y() - from.y());
        const int pieces = std::max(1, static_cast<int>(length / step));
        for (int piece = 1; piece <= pieces; ++piece) {
            const qreal t = static_cast<qreal>(piece) / pieces;
            result.append(from + (to - from) * t);
        }
    }
    return result;
}

QPolygonF wobbled(
    const QPolygonF &source,
    quint64 seed,
    qreal amplitude,
    qreal phase)
{
    const QPolygonF dense = densify(source, 1.8);
    const qreal a1 = seededAngle(seed, 1) + phase;
    const qreal a2 = seededAngle(seed, 2) + phase * 1.4;
    const qreal a3 = seededAngle(seed, 3) + phase * 0.8;
    const qreal a4 = seededAngle(seed, 4) + phase * 1.2;

    QPolygonF result;
    result.reserve(dense.size());
    qreal travelled = 0.0;
    for (int index = 0; index < dense.size(); ++index) {
        if (index > 0) {
            travelled += std::hypot(
                dense[index].x() - dense[index - 1].x(),
                dense[index].y() - dense[index - 1].y());
        }
        const qreal dx = amplitude
            * (0.62 * std::sin(travelled * 0.55 + a1)
               + 0.38 * std::sin(travelled * 1.35 + a2));
        const qreal dy = amplitude
            * (0.62 * std::sin(travelled * 0.62 + a3)
               + 0.38 * std::sin(travelled * 1.21 + a4));
        result.append(dense[index] + QPointF(dx, dy));
    }
    return result;
}

QPainterPath openPath(const QPolygonF &points)
{
    QPainterPath path;
    if (points.isEmpty()) {
        return path;
    }
    path.moveTo(points.first());
    for (int index = 1; index < points.size(); ++index) {
        path.lineTo(points[index]);
    }
    return path;
}

}

QPixmap Icons::pixmap(
    IconGlyph glyph,
    int size,
    const QColor &color,
    qreal wobblePhase,
    qreal devicePixelRatio)
{
    QPixmap result(
        qRound(size * devicePixelRatio),
        qRound(size * devicePixelRatio));
    result.setDevicePixelRatio(devicePixelRatio);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(size / 24.0, size / 24.0);

    const Glyph shapes = glyphFor(glyph);
    const quint64 seed = static_cast<quint64>(glyph) * 0x51ED2701ULL + 7;
    const qreal amplitude = 0.5;

    painter.setBrush(Qt::NoBrush);
    painter.setPen(
        QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (int index = 0; index < shapes.lines.size(); ++index) {
        painter.drawPath(openPath(wobbled(
            shapes.lines[index],
            seed + static_cast<quint64>(index) * 131,
            amplitude,
            wobblePhase)));
    }

    painter.setPen(
        QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(color);
    for (int index = 0; index < shapes.fills.size(); ++index) {
        QPainterPath path = openPath(wobbled(
            shapes.fills[index],
            seed + 977 + static_cast<quint64>(index) * 131,
            amplitude,
            wobblePhase));
        path.closeSubpath();
        painter.drawPath(path);
    }

    return result;
}

QIcon Icons::icon(IconGlyph glyph)
{
    QIcon result;
    for (const int size : {16, 20, 24, 32, 48}) {
        for (const qreal ratio : {1.0, 2.0}) {
            result.addPixmap(
                pixmap(glyph, size, Theme::textPrimary(), 0.0, ratio),
                QIcon::Normal,
                QIcon::Off);
            result.addPixmap(
                pixmap(glyph, size, Theme::textDisabled(), 0.0, ratio),
                QIcon::Disabled,
                QIcon::Off);
        }
    }
    return result;
}

QIcon Icons::toggleIcon(IconGlyph glyph)
{
    QIcon result = icon(glyph);
    for (const int size : {16, 20, 24, 32, 48}) {
        for (const qreal ratio : {1.0, 2.0}) {
            result.addPixmap(
                pixmap(glyph, size, Theme::accentText(), 0.0, ratio),
                QIcon::Normal,
                QIcon::On);
            result.addPixmap(
                pixmap(glyph, size, Theme::accentText(), 0.0, ratio),
                QIcon::Active,
                QIcon::On);
            result.addPixmap(
                pixmap(glyph, size, Theme::textDisabled(), 0.0, ratio),
                QIcon::Disabled,
                QIcon::On);
        }
    }
    return result;
}

}
