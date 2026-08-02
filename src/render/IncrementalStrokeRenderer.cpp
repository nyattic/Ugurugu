#include "render/IncrementalStrokeRenderer.hpp"

#include <QPainter>
#include <QSet>

#include <algorithm>
#include <utility>

namespace wobble
{

namespace
{

QPainterPath maskPath(const QImage &mask)
{
    QPainterPath path;
    if (mask.isNull() || mask.format() != QImage::Format_Grayscale8)
    {
        return path;
    }
    for (int y = 0; y < mask.height(); ++y)
    {
        const uchar *line = mask.constScanLine(y);
        int x = 0;
        while (x < mask.width())
        {
            while (x < mask.width() && line[x] < 128)
            {
                ++x;
            }
            const int left = x;
            while (x < mask.width() && line[x] >= 128)
            {
                ++x;
            }
            if (left < x)
            {
                path.addRect(left, y, x - left, 1);
            }
        }
    }
    return path;
}

}

IncrementalStrokeRenderer::Update IncrementalStrokeRenderer::update(
    const QImage &baseLayer,
    const Document &document,
    const Stroke &stroke,
    int frameIndex,
    const QSize &outputSize)
{
    Update result;
    if (document.size.isEmpty() || outputSize.isEmpty() || baseLayer.isNull()
        || baseLayer.size() != outputSize
        || baseLayer.format() != QImage::Format_ARGB32_Premultiplied
        || (stroke.mode != StrokeMode::Paint
            && stroke.mode != StrokeMode::Erase))
    {
        clear();
        return result;
    }

    const bool baseChanged = m_baseLayerKey != baseLayer.cacheKey()
                             || m_documentSize != document.size
                             || m_outputSize != outputSize;
    QSet<QPoint> dirtyTiles;
    if (baseChanged)
    {
        for (auto tile = m_layerTiles.cbegin(); tile != m_layerTiles.cend();
            ++tile)
        {
            dirtyTiles.insert(tile.key());
        }
        m_geometry.clear();
        m_primitivesByTile.clear();
        m_tilesByPrimitive.clear();
        m_layerTiles.clear();
        m_clipPath = {};
        m_clipMaskKey = 0;
    }
    m_baseLayerKey = baseLayer.cacheKey();
    m_documentSize = document.size;
    m_outputSize = outputSize;

    const StrokeRenderer::GeometryUpdate geometry = m_geometry.update(
        stroke, frameIndex, document.animationFrames, document.wobbleAmount);
    if (!geometry.valid)
    {
        clear();
        return result;
    }
    result.sourcePointsProcessed = geometry.sourcePointsProcessed;
    result.geometryRebuilt = geometry.rebuilt;

    const StrokeRenderer::PreparedStroke &prepared = m_geometry.prepared();
    qsizetype rebuildFrom = geometry.changedFrom;
    if (stroke.brush.engine == BrushEngine::Line)
    {
        rebuildFrom = std::max<qsizetype>(0, rebuildFrom - 2);
    }
    if (geometry.rebuilt || geometry.renderingModeChanged)
    {
        rebuildFrom = 0;
    }
    removePrimitiveIndexesFrom(rebuildFrom, dirtyTiles);
    addPrimitiveIndexesFrom(
        stroke, prepared, rebuildFrom, document, dirtyTiles);

    if (m_clipMaskKey != stroke.clipMask.cacheKey())
    {
        m_clipPath = maskPath(stroke.clipMask);
        m_clipMaskKey = stroke.clipMask.cacheKey();
        for (auto tile = m_layerTiles.cbegin(); tile != m_layerTiles.cend();
            ++tile)
        {
            dirtyTiles.insert(tile.key());
        }
    }

    QVector<QPoint> orderedTiles(dirtyTiles.cbegin(), dirtyTiles.cend());
    std::sort(orderedTiles.begin(),
        orderedTiles.end(),
        [](const QPoint &left, const QPoint &right)
        {
            return left.y() < right.y()
                   || (left.y() == right.y() && left.x() < right.x());
        });
    result.patches.reserve(orderedTiles.size());
    for (const QPoint &tile : orderedTiles)
    {
        const auto primitives = m_primitivesByTile.constFind(tile);
        if (primitives == m_primitivesByTile.cend() || primitives->isEmpty())
        {
            m_layerTiles.remove(tile);
            continue;
        }
        QImage image = renderTile(baseLayer, document, stroke, prepared, tile);
        if (image.isNull())
        {
            clear();
            return {};
        }
        const QRect bounds = tileBounds(tile);
        result.tilesRendered += 1;
        result.pixelsRendered +=
            static_cast<quint64>(bounds.width()) * bounds.height();
        m_layerTiles.insert(tile, image);
        result.patches.append({bounds, std::move(image)});
    }
    for (auto tile = m_layerTiles.cbegin(); tile != m_layerTiles.cend(); ++tile)
    {
        result.cachedTileBytes += tile.value().sizeInBytes();
    }
    result.valid = true;
    return result;
}

bool IncrementalStrokeRenderer::applyTo(QImage &layerImage) const
{
    if (layerImage.isNull() || layerImage.size() != m_outputSize
        || layerImage.format() != QImage::Format_ARGB32_Premultiplied)
    {
        return false;
    }
    QPainter painter(&layerImage);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (auto tile = m_layerTiles.cbegin(); tile != m_layerTiles.cend(); ++tile)
    {
        painter.drawImage(tileBounds(tile.key()).topLeft(), tile.value());
    }
    return true;
}

void IncrementalStrokeRenderer::clear()
{
    m_geometry.clear();
    m_primitivesByTile.clear();
    m_tilesByPrimitive.clear();
    m_layerTiles.clear();
    m_clipPath = {};
    m_clipMaskKey = 0;
    m_baseLayerKey = 0;
    m_documentSize = {};
    m_outputSize = {};
}

QVector<QPoint> IncrementalStrokeRenderer::tilesForBounds(
    const QRectF &bounds, const Document &document) const
{
    if (bounds.isEmpty() || document.size.isEmpty() || m_outputSize.isEmpty())
    {
        return {};
    }
    const qreal horizontalScale =
        static_cast<qreal>(m_outputSize.width()) / document.size.width();
    const qreal verticalScale =
        static_cast<qreal>(m_outputSize.height()) / document.size.height();
    const QRect outputBounds = QRectF(bounds.x() * horizontalScale,
        bounds.y() * verticalScale,
        bounds.width() * horizontalScale,
        bounds.height() * verticalScale)
                                   .toAlignedRect()
                                   .intersected(QRect(QPoint(), m_outputSize));
    if (outputBounds.isEmpty())
    {
        return {};
    }
    QVector<QPoint> result;
    const int firstColumn = outputBounds.left() / tileEdge;
    const int lastColumn = outputBounds.right() / tileEdge;
    const int firstRow = outputBounds.top() / tileEdge;
    const int lastRow = outputBounds.bottom() / tileEdge;
    result.reserve((lastColumn - firstColumn + 1) * (lastRow - firstRow + 1));
    for (int row = firstRow; row <= lastRow; ++row)
    {
        for (int column = firstColumn; column <= lastColumn; ++column)
        {
            result.append(QPoint(column, row));
        }
    }
    return result;
}

QRect IncrementalStrokeRenderer::tileBounds(const QPoint &tile) const
{
    return QRect(tile.x() * tileEdge, tile.y() * tileEdge, tileEdge, tileEdge)
        .intersected(QRect(QPoint(), m_outputSize));
}

void IncrementalStrokeRenderer::removePrimitiveIndexesFrom(
    qsizetype first, QSet<QPoint> &dirtyTiles)
{
    first = std::clamp<qsizetype>(first, 0, m_tilesByPrimitive.size());
    for (qsizetype index = first; index < m_tilesByPrimitive.size(); ++index)
    {
        for (const QPoint &tile : m_tilesByPrimitive[index])
        {
            dirtyTiles.insert(tile);
            auto primitives = m_primitivesByTile.find(tile);
            if (primitives == m_primitivesByTile.end())
            {
                continue;
            }
            primitives->removeAll(static_cast<int>(index));
            if (primitives->isEmpty())
            {
                m_primitivesByTile.erase(primitives);
            }
        }
    }
    m_tilesByPrimitive.resize(first);
}

void IncrementalStrokeRenderer::addPrimitiveIndexesFrom(const Stroke &stroke,
    const StrokeRenderer::PreparedStroke &prepared,
    qsizetype first,
    const Document &document,
    QSet<QPoint> &dirtyTiles)
{
    const int count = StrokeRenderer::primitiveCount(stroke, prepared);
    first = std::clamp<qsizetype>(first, 0, count);
    m_tilesByPrimitive.resize(count);
    for (int index = static_cast<int>(first); index < count; ++index)
    {
        const QVector<QPoint> tiles = tilesForBounds(
            StrokeRenderer::primitiveBounds(stroke, prepared, index), document);
        m_tilesByPrimitive[index] = tiles;
        for (const QPoint &tile : tiles)
        {
            m_primitivesByTile[tile].append(index);
            dirtyTiles.insert(tile);
        }
    }
}

QImage IncrementalStrokeRenderer::renderTile(const QImage &baseLayer,
    const Document &document,
    const Stroke &stroke,
    const StrokeRenderer::PreparedStroke &prepared,
    const QPoint &tile)
{
    const QRect bounds = tileBounds(tile);
    if (bounds.isEmpty())
    {
        return {};
    }
    QImage image = baseLayer.copy(bounds);
    if (image.isNull())
    {
        return {};
    }
    const qreal horizontalScale =
        static_cast<qreal>(m_outputSize.width()) / document.size.width();
    const qreal verticalScale =
        static_cast<qreal>(m_outputSize.height()) / document.size.height();
    const QPointF logicalOrigin(
        bounds.x() / horizontalScale, bounds.y() / verticalScale);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, stroke.brush.antialiasing);
    painter.scale(horizontalScale, verticalScale);
    painter.translate(-logicalOrigin);
    if (stroke.visibilityClip)
    {
        painter.setClipRect(QRectF(*stroke.visibilityClip), Qt::IntersectClip);
    }
    if (!stroke.clipMask.isNull())
    {
        painter.setClipPath(m_clipPath, Qt::IntersectClip);
    }
    painter.setCompositionMode(stroke.mode == StrokeMode::Erase
                                   ? QPainter::CompositionMode_DestinationOut
                                   : QPainter::CompositionMode_SourceOver);
    painter.setBrush(Qt::NoBrush);
    StrokeRenderer::paintPrimitives(
        painter, stroke, prepared, m_primitivesByTile.value(tile));
    return image;
}

}
