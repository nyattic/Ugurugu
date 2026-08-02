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

    // Redraws only the tiles the appended points touch. The result must stay
    // pixel-identical to rendering the whole stroke from scratch, which is
    // what the coverage regression tests assert; anything that changes how a
    // primitive rasterizes has to invalidate the affected tiles rather than
    // blend over them. A different base layer or clip mask discards the cache,
    // both detected by QImage::cacheKey.
    Update update(const QImage &baseLayer,
        const Document &document,
        const Stroke &stroke,
        int frameIndex,
        const QSize &outputSize);
    // Requires layerImage to match the cached output size exactly and to be
    // ARGB32_Premultiplied; tiles are copied over it, not composited.
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
