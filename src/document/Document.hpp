#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTransform>
#include <QUuid>
#include <QVector>

#include <optional>

namespace wobble
{

enum class StrokeMode
{
    Paint,
    Erase,
    Fill,
    PixelSelection,
    Reframe
};

enum class SamplingMode
{
    Nearest,
    Smooth
};

struct PackedMaskRegion
{
    QSize canvasSize;
    QRect bounds;
    // One bit per pixel, padded to a whole byte per row.
    QByteArray packedMask;

    bool operator==(const PackedMaskRegion &) const = default;
};

struct PixelSelectionOp
{
    QSize canvasSize;
    QRect sourceBounds;
    // One bit per pixel in sourceBounds, padded to a whole byte per row.
    // Bit 7 is the leftmost pixel. Unused low bits in each row are zero.
    QByteArray packedMask;
    QTransform transform;
    SamplingMode sampling = SamplingMode::Smooth;
    bool clearSource = true;
    bool drawDestination = true;

    bool operator==(const PixelSelectionOp &) const = default;
};

enum class ReframeMode
{
    Canvas,
    Image
};

struct ReframeOp
{
    ReframeMode mode = ReframeMode::Canvas;
    SamplingMode sampling = SamplingMode::Nearest;
    QSize sourceSize;
    QSize targetSize;
    QPoint contentOffset;

    bool operator==(const ReframeOp &) const = default;
};

enum class BrushEngine
{
    Line,
    Airbrush,
    Spray
};

enum class BrushTipShape
{
    Round,
    Square
};

struct BrushSettings
{
    BrushEngine engine = BrushEngine::Line;
    BrushTipShape tipShape = BrushTipShape::Round;
    qreal opacity = 1.0;
    qreal flow = 1.0;
    qreal hardness = 1.0;
    qreal spacing = 0.15;
    qreal scatter = 0.0;
    qreal particleSize = 0.08;
    qreal density = 1.0;
    qreal sizeDynamics = 0.8;
    qreal opacityDynamics = 0.0;
    qreal sizeJitter = 0.0;
    bool animatedJitter = false;
    qreal wobbleScale = 1.0;
    bool antialiasing = false;

    bool operator==(const BrushSettings &) const = default;
};

bool isValidBrushSettings(const BrushSettings &settings);

struct StrokePoint
{
    QPointF position;
    qreal pressure = 1.0;

    bool operator==(const StrokePoint &) const = default;
};

struct Stroke
{
    QUuid id = QUuid::createUuid();
    quint64 seed = 0;
    StrokeMode mode = StrokeMode::Paint;
    QColor color = Qt::black;
    qreal width = 6.0;
    BrushSettings brush;
    QVector<StrokePoint> points;
    // A rectangular visibility restriction in document pixel coordinates.
    // A missing value means that no additional rectangular clip is applied.
    std::optional<QRect> visibilityClip;
    QImage clipMask;
    // Frozen flood-fill coverage for Fill strokes. A null image is accepted
    // for normal procedural fills. A non-null image is retained only to
    // preserve explicitly frozen schema-5 projects.
    QImage fillMask;
    // Ordered framebuffer operations are deliberately distinct from
    // primitive brush strokes and have strict validation invariants.
    std::optional<PixelSelectionOp> pixelSelectionOp;
    std::optional<ReframeOp> reframeOp;
};

enum class LayerBlendMode
{
    Normal,
    Multiply,
    Screen,
    Overlay
};

bool isValidLayerBlendMode(LayerBlendMode mode);

enum class LayerKind
{
    Paint,
    Group
};

bool isValidLayerKind(LayerKind kind);

struct Layer
{
    QUuid id = QUuid::createUuid();
    QString name;
    LayerKind kind = LayerKind::Paint;
    QUuid parentGroupId;
    bool clipToLayerBelow = false;
    bool visible = true;
    bool reference = false;
    qreal opacity = 1.0;
    LayerBlendMode blendMode = LayerBlendMode::Normal;
    // The framebuffer epoch before the first ordered operation.
    QSize initialCanvasSize;
    QVector<Stroke> strokes;
};

struct Document
{
    QSize size = QSize(1024, 768);
    QColor background = Qt::white;
    int animationFrames = 30;
    qreal framesPerSecond = 25.0;
    qreal wobbleAmount = 1.6;
    QVector<Layer> layers;
    QUuid activeLayerId;

    static Document createDefault(const QSize &size = QSize(1024, 768),
        const QString &initialLayerName = {});
    Layer *layer(const QUuid &id);
    const Layer *layer(const QUuid &id) const;
    int layerIndex(const QUuid &id) const;
    bool isLayerDescendantOf(
        const QUuid &layerId, const QUuid &ancestorGroupId) const;
    int layerDepth(const QUuid &id) const;
};

}
