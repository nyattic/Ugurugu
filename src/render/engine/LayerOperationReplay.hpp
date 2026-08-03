#pragma once

#include "document/Document.hpp"

#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QRect>
#include <QSize>

#include <optional>

namespace wobble
{
namespace render_detail
{

// Replays a layer's ordered framebuffer operations at native document scale.
//
// Order is the contract: operations must be applied in document order,
// because a pixel selection or reframe observes whatever the earlier strokes
// left behind. The scale parameters exist for rendering into a differently
// sized framebuffer, not for previewing — anything that changes which pixels
// an operation reads belongs in DisplayScaleReplay instead.

QPainterPath maskPath(const QImage &mask);

QImage scaledMask(
    const QImage &mask, const QSize &outputSize, QHash<qint64, QImage> &cache);

std::optional<QRect> scaledVisibilityClip(const Stroke &stroke,
    const QSize &outputSize,
    qreal horizontalScale,
    qreal verticalScale);

void applyFillStroke(QImage &layerImage,
    const Stroke &stroke,
    const QImage &coverageMask,
    const QImage &clipMask,
    const std::optional<QRect> &visibilityClip,
    qreal horizontalScale,
    qreal verticalScale);

void renderLayerStrokes(QImage &layerImage,
    const Document &document,
    const QVector<Stroke> &strokes,
    int normalizedFrame,
    int frameCount,
    qreal horizontalScale,
    qreal verticalScale,
    QHash<qint64, QPainterPath> &clipPaths,
    QHash<qint64, QImage> &scaledClipMasks,
    const QPointF &logicalOrigin = {});

bool applyPixelSelectionOperation(
    QImage &layerImage, const PixelSelectionOp &operation);

bool applyReframeOperation(QImage &layerImage, const ReframeOp &operation);

// Returns false when the framebuffer no longer matches the document size the
// remaining operations were recorded against; the caller must discard it.
bool renderLayerOperations(QImage &layerImage,
    const Document &document,
    const QVector<Stroke> &operations,
    int normalizedFrame,
    int frameCount,
    const QSize &initialCanvasSize);

}

}
