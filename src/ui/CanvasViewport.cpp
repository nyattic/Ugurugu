// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

#include "ui/CanvasViewport.hpp"

#include "ui/Theme.hpp"

#include <QHash>
#include <QList>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace ugurugu
{
namespace canvas_detail
{

const QBrush &checkerBrush()
{
    static const QBrush brush = []()
    {
        QImage tile(checkerSize * 2, checkerSize * 2, QImage::Format_RGB32);
        tile.fill(QColor(238, 238, 238));
        QPainter painter(&tile);
        painter.fillRect(
            checkerSize, 0, checkerSize, checkerSize, QColor(210, 210, 210));
        painter.fillRect(
            0, checkerSize, checkerSize, checkerSize, QColor(210, 210, 210));
        return QBrush(tile);
    }();
    return brush;
}

bool fuzzyIdentity(const QTransform &transform)
{
    return qFuzzyCompare(transform.m11(), 1.0) && qFuzzyIsNull(transform.m12())
           && qFuzzyIsNull(transform.m13()) && qFuzzyIsNull(transform.m21())
           && qFuzzyCompare(transform.m22(), 1.0)
           && qFuzzyIsNull(transform.m23()) && qFuzzyIsNull(transform.m31())
           && qFuzzyIsNull(transform.m32())
           && qFuzzyCompare(transform.m33(), 1.0);
}

qreal pointDistance(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

bool documentHasStrokes(const Document &document)
{
    for (const Layer &layer : document.layers)
    {
        if (!layer.strokes.isEmpty())
        {
            return true;
        }
    }
    return false;
}

quint64 encodedPoint(int x, int y)
{
    return (static_cast<quint64>(static_cast<quint32>(x)) << 32U)
           | static_cast<quint32>(y);
}

QPointF decodedPoint(quint64 point)
{
    return QPointF(
        static_cast<quint32>(point >> 32U), static_cast<quint32>(point));
}

QPainterPath outlinePath(const QImage &mask)
{
    QHash<quint64, QVector<quint64>> edges;
    const auto inside = [&mask](int x, int y)
    {
        return x >= 0 && y >= 0 && x < mask.width() && y < mask.height()
               && mask.constScanLine(y)[x] >= 128;
    };
    const auto addEdge = [&edges](int x1, int y1, int x2, int y2)
    {
        edges[encodedPoint(x1, y1)].append(encodedPoint(x2, y2));
    };

    for (int y = 0; y < mask.height(); ++y)
    {
        const uchar *line = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x)
        {
            if (line[x] < 128)
            {
                continue;
            }
            if (!inside(x, y - 1))
            {
                addEdge(x, y, x + 1, y);
            }
            if (!inside(x + 1, y))
            {
                addEdge(x + 1, y, x + 1, y + 1);
            }
            if (!inside(x, y + 1))
            {
                addEdge(x + 1, y + 1, x, y + 1);
            }
            if (!inside(x - 1, y))
            {
                addEdge(x, y + 1, x, y);
            }
        }
    }

    QPainterPath path;
    while (!edges.isEmpty())
    {
        auto first = edges.begin();
        const quint64 start = first.key();
        quint64 current = start;
        path.moveTo(decodedPoint(start));

        do
        {
            auto edge = edges.find(current);
            if (edge == edges.end())
            {
                break;
            }
            const quint64 next = edge.value().takeLast();
            if (edge.value().isEmpty())
            {
                edges.erase(edge);
            }
            path.lineTo(decodedPoint(next));
            current = next;
        } while (current != start);

        if (current == start)
        {
            path.closeSubpath();
        }
    }
    return path;
}

void drawSelectionPath(
    QPainter &painter, const QPainterPath &path, qreal dashOffset)
{
    QPen lightPen(QColor(255, 255, 255, 235), 1.8);
    lightPen.setCosmetic(true);
    lightPen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(lightPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    QPen darkPen(QColor(20, 20, 20, 245), 1.0);
    darkPen.setCosmetic(true);
    darkPen.setJoinStyle(Qt::MiterJoin);
    darkPen.setDashPattern({4.0, 4.0});
    darkPen.setDashOffset(dashOffset);
    painter.setPen(darkPen);
    painter.drawPath(path);
}

}

}
