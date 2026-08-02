#pragma once

#include "document/Document.hpp"
#include "render/StrokeRenderer.hpp"

#include <QHash>
#include <QImage>
#include <QPainterPath>

namespace wobble
{

class IncrementalStrokeRenderer final
{
public:
    struct Patch
    {
        QRect bounds;
        QImage layerImage;
    };

    struct Update
    {
        QVector<Patch> patches;
        quint64 sourcePointsProcessed = 0;
        quint64 tilesRendered = 0;
        quint64 pixelsRendered = 0;
        quint64 cachedTileBytes = 0;
        bool geometryRebuilt = false;
        bool valid = false;
    };

    Update update(const QImage &baseLayer,
        const Document &document,
        const Stroke &stroke,
        int frameIndex,
        const QSize &outputSize);
    bool applyTo(QImage &layerImage) const;
    void clear();

private:
    static constexpr int tileEdge = 256;

    QVector<QPoint> tilesForBounds(
        const QRectF &bounds, const Document &document) const;
    QRect tileBounds(const QPoint &tile) const;
    void removePrimitiveIndexesFrom(qsizetype first, QSet<QPoint> &dirtyTiles);
    void addPrimitiveIndexesFrom(const Stroke &stroke,
        const StrokeRenderer::PreparedStroke &prepared,
        qsizetype first,
        const Document &document,
        QSet<QPoint> &dirtyTiles);
    QImage renderTile(const QImage &baseLayer,
        const Document &document,
        const Stroke &stroke,
        const StrokeRenderer::PreparedStroke &prepared,
        const QPoint &tile);

    StrokeRenderer::IncrementalGeometry m_geometry;
    QHash<QPoint, QVector<int>> m_primitivesByTile;
    QVector<QVector<QPoint>> m_tilesByPrimitive;
    QHash<QPoint, QImage> m_layerTiles;
    QPainterPath m_clipPath;
    qint64 m_clipMaskKey = 0;
    qint64 m_baseLayerKey = 0;
    QSize m_documentSize;
    QSize m_outputSize;
};

}
