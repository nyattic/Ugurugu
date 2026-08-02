#pragma once

#include "document/Document.hpp"

#include <QPainter>
#include <QPainterPath>

namespace wobble::StrokeRenderer
{

struct PreparedStroke
{
    QVector<StrokePoint> points;
    qreal width = 0.0;
    int normalizedFrame = 0;
    bool variablePressure = false;
    bool valid = false;
};

struct GeometryUpdate
{
    qsizetype changedFrom = 0;
    quint64 sourcePointsProcessed = 0;
    bool rebuilt = false;
    bool renderingModeChanged = false;
    bool valid = false;
};

class IncrementalGeometry final
{
public:
    const PreparedStroke &prepared() const;
    GeometryUpdate update(const Stroke &stroke,
        int frameIndex,
        int frameCount,
        qreal wobbleAmount);
    void clear();

private:
    bool matches(const Stroke &stroke,
        int normalizedFrame,
        int frameCount,
        qreal wobbleAmount) const;
    bool rebuildSamples(const Stroke &stroke, qreal spacing, qsizetype maximum);
    bool appendSamples(const Stroke &stroke);
    void displaceSamples(const Stroke &stroke,
        int normalizedFrame,
        qreal wobbleAmount,
        qsizetype changedFrom);

    PreparedStroke m_prepared;
    QVector<StrokePoint> m_regularSamples;
    QVector<StrokePoint> m_samples;
    QVector<qreal> m_arcLengths;
    Stroke m_identity;
    qreal m_spacing = 0.0;
    qreal m_distanceToNext = 0.0;
    long double m_totalLength = 0.0L;
    qsizetype m_sourcePointCount = 0;
    qsizetype m_maximumPoints = 0;
    int m_frameCount = 0;
    int m_normalizedFrame = 0;
    qreal m_wobbleAmount = 0.0;
    qreal m_regularMinimumPressure = 1.0;
    qreal m_regularMaximumPressure = 0.0;
    bool m_capped = false;
};

PreparedStroke prepare(
    const Stroke &stroke, int frameIndex, int frameCount, qreal wobbleAmount);
QPainterPath path(
    const Stroke &stroke, int frameIndex, int frameCount, qreal wobbleAmount);
void paint(
    QPainter &painter, const Stroke &stroke, const PreparedStroke &prepared);
void paintPrimitives(QPainter &painter,
    const Stroke &stroke,
    const PreparedStroke &prepared,
    QVector<int> primitiveIndexes);
QRectF primitiveBounds(
    const Stroke &stroke, const PreparedStroke &prepared, int primitiveIndex);
int primitiveCount(const Stroke &stroke, const PreparedStroke &prepared);

}
