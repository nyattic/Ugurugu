// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nyabi (nyattic)

#include "brush/BrushPreset.hpp"
#include "brush/EraserPreset.hpp"
#include "document/Document.hpp"
#include "document/DocumentController.hpp"
#include "document/DocumentLimits.hpp"
#include "document/SelectionOperation.hpp"
#include "document/SelectionOutline.hpp"
#include "document/StrokeMask.hpp"
#include "input/StrokeStabilizer.hpp"
#include "io/AnimationExportPolicy.hpp"
#include "io/DocumentSerializer.hpp"
#include "io/GifWriter.hpp"
#include "io/serializer/SerializerSchema.hpp"
#include "render/FloodFillMask.hpp"
#include "render/IncrementalStrokeRenderer.hpp"
#include "render/LayerThumbnailRenderer.hpp"
#include "render/RenderEngine.hpp"

#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRandomGenerator>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QUuid>

#include <algorithm>
#include <cstdint>
#include <emscripten/emscripten.h>
#include <memory>
#include <utility>

namespace
{

struct BridgeDocument
{
    std::unique_ptr<ugurugu::DocumentController> controller =
        std::make_unique<ugurugu::DocumentController>();
    ugurugu::Stroke brushTemplate;
    bool brushAntialiasing = false;
    ugurugu::StrokeStabilizer stabilizer;
    ugurugu::Stroke activeStroke;
    bool strokeInProgress = false;
    int strokeFrame = 0;
    QPointF lastRawPosition;
    quint64 lastTimestamp = 0;
    // Survives a committed stroke: ugu_stroke_end bakes the finished stroke
    // into layerBase, so the next stroke on the same layer and frame skips
    // renderLayerSplit entirely. splitFrame/-Layer identify what it holds.
    ugurugu::RenderEngine::LayerSplitFrame split;
    QUuid splitLayerId;
    int splitFrame = -1;
    bool splitUsable = false;
    ugurugu::IncrementalStrokeRenderer incremental;
    QUuid strokeLayerId;
    bool incrementalActive = false;
    QImage renderedFrame;
    // Which frame renderedFrame shows, and whether it shows it without a live
    // stroke drawn over it.
    int renderedFrameIndex = -1;
    bool renderedFrameCommitted = false;
    QRect dirty;
    QImage thumbnail;
    QByteArray serialized;
    QByteArray exportBytes;
    QByteArray scratchText;
    // Selection state, held exactly the way CanvasWidget holds it: a
    // Grayscale8 mask the size of the canvas plus the layer it was made on.
    // Strokes clip to it while it belongs to the layer being painted.
    QImage selectionMask;
    QUuid selectionLayerId;
    // Bumped on every selection change so the shell can skip re-reading an
    // outline it already has.
    int selectionRevision = 0;
    QVector<float> outlinePoints;
    ugurugu::FloodFillMask::Comparison fillComparison =
        ugurugu::FloodFillMask::Comparison::AlphaBoundary;
    int fillTolerance = 32;
    int fillReference = 0;
    bool bucketAntialiasing = true;
};

QByteArray &lastError()
{
    static QByteArray error;
    return error;
}

int &lastErrorCode()
{
    static int code = 0;
    return code;
}

// Mirrors the UguStatus values documented on ugu_last_error_code(). Kept as
// plain ints because the C ABI carries them as ints anyway.
enum BridgeStatus
{
    StatusOk = 0,
    StatusInvalidArgument = 1,
    StatusOutOfMemory = 2,
    StatusDocumentInvalid = 3,
    StatusNoPaintLayer = 4,
    StatusStrokeRejected = 5,
    StatusRenderFailed = 6,
    StatusExportTooLarge = 7,
    StatusExportFailed = 8,
    StatusNoSelection = 9,
    StatusEmptyRegion = 10,
    StatusLayerNotDrawable = 11
};

// Selection combine modes, matching CanvasSelectionCombine.
enum BridgeCombine
{
    CombineReplace = 0,
    CombineAdd = 1,
    CombineSubtract = 2
};

// Selection shapes, matching CanvasSelectionShape.
enum BridgeShape
{
    ShapeFreehand = 0,
    ShapeRectangle = 1,
    ShapeEllipse = 2
};

// Flood-fill reference sources, matching CanvasWandReference.
enum BridgeReference
{
    ReferenceActiveLayer = 0,
    ReferenceMarkedLayers = 1,
    ReferenceAllVisibleLayers = 2
};

void setError(int code, QByteArray message)
{
    lastErrorCode() = code;
    lastError() = std::move(message);
}

void clearError()
{
    lastErrorCode() = StatusOk;
    lastError().clear();
}

QUuid paintTargetLayer(const ugurugu::Document &document)
{
    if (const ugurugu::Layer *layer = document.layer(document.activeLayerId);
        layer != nullptr && layer->kind == ugurugu::LayerKind::Paint)
    {
        return document.activeLayerId;
    }
    for (const ugurugu::Layer &layer : document.layers)
    {
        if (layer.kind == ugurugu::LayerKind::Paint)
        {
            return layer.id;
        }
    }
    return {};
}

const ugurugu::Layer *layerAtIndex(const BridgeDocument *handle, int index)
{
    const auto &layers = handle->controller->document().layers;
    if (index < 0 || index >= layers.size())
    {
        return nullptr;
    }
    return &layers[index];
}

// addStroke refuses a stroke outright when any point falls outside the canvas
// (isValidInputStrokePoint), so a drag that leaves the canvas would silently
// lose the whole line. The desktop clamps at begin, continue and end through
// CanvasWidget::clampedDocumentPosition; this is the same contract.
ugurugu::StrokePoint canvasPoint(
    const BridgeDocument *handle, const QPointF &position, double pressure)
{
    const QSize size = handle->controller->document().size;
    return ugurugu::StrokePoint{
        QPointF(std::clamp(position.x(), 0.0, static_cast<qreal>(size.width())),
            std::clamp(position.y(), 0.0, static_cast<qreal>(size.height()))),
        std::clamp(pressure, 0.0, 1.0)};
}

void invalidateSplit(BridgeDocument *handle)
{
    handle->split = {};
    handle->splitLayerId = {};
    handle->splitFrame = -1;
    handle->splitUsable = false;
}

void renderCommittedFrame(BridgeDocument *handle, int frameIndex)
{
    handle->renderedFrame = ugurugu::RenderEngine::render(
        handle->controller->document(), frameIndex);
    handle->dirty = handle->renderedFrame.rect();
    handle->renderedFrameIndex = frameIndex;
    handle->renderedFrameCommitted = true;
}

void renderFullStrokePreview(BridgeDocument *handle)
{
    ugurugu::Document preview = handle->controller->document();
    if (ugurugu::Layer *layer = preview.layer(handle->strokeLayerId))
    {
        layer->strokes.append(handle->activeStroke);
    }
    handle->renderedFrame =
        ugurugu::RenderEngine::render(preview, handle->strokeFrame);
    handle->dirty = handle->renderedFrame.rect();
    handle->renderedFrameIndex = handle->strokeFrame;
    handle->renderedFrameCommitted = false;
}

// Draws the active stroke's pending points over renderedFrame and reports the
// tiles it touched. Returns false when the incremental contract broke and the
// caller has to fall back to a full preview render.
bool updateIncrementalStroke(BridgeDocument *handle, QRect *dirtyOut)
{
    const ugurugu::Document &document = handle->controller->document();
    const ugurugu::Layer *strokeLayer = document.layer(handle->strokeLayerId);
    if (strokeLayer == nullptr)
    {
        return false;
    }
    // The live stroke has to wobble the way its own layer does, not the way
    // the document does.
    const ugurugu::Document strokeDocument =
        ugurugu::documentForLayer(document, *strokeLayer);
    const auto update = handle->incremental.update(handle->split.layerBase,
        strokeDocument,
        handle->activeStroke,
        handle->strokeFrame,
        document.size);
    if (!update.valid)
    {
        return false;
    }
    QRect dirty;
    QPainter painter(&handle->renderedFrame);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (const auto &patch : update.patches)
    {
        // composeLayerSplitRegion wants the tile-sized patch image, not a
        // full layer surface; a null result means the contract broke, so fall
        // back to the full preview instead of dropping the patch.
        const QImage region = ugurugu::RenderEngine::composeLayerSplitRegion(
            handle->split, patch.layerImage, patch.bounds);
        if (region.isNull())
        {
            painter.end();
            return false;
        }
        painter.drawImage(patch.bounds.topLeft(), region);
        dirty = dirty.united(patch.bounds);
    }
    painter.end();
    handle->renderedFrameCommitted = false;
    if (dirtyOut != nullptr)
    {
        *dirtyOut = dirty;
    }
    return true;
}

bool selectionAppliesTo(const BridgeDocument *handle, const QUuid &layerId)
{
    return !handle->selectionMask.isNull()
           && handle->selectionLayerId == layerId
           && handle->selectionMask.size()
                  == handle->controller->document().size;
}

// Every selection change goes through here so the revision, the cached
// outline and the "empty means no selection" rule stay in one place.
void installSelection(BridgeDocument *handle, QImage mask, const QUuid &layerId)
{
    const QSize size = handle->controller->document().size;
    const bool usable = !mask.isNull() && mask.size() == size
                        && mask.format() == QImage::Format_Grayscale8
                        && ugurugu::maskHasContent(mask) && !layerId.isNull();
    handle->selectionMask = usable ? std::move(mask) : QImage();
    handle->selectionLayerId = usable ? layerId : QUuid();
    handle->selectionRevision += 1;

    handle->outlinePoints.clear();
    if (handle->selectionMask.isNull())
    {
        return;
    }
    // Flat float buffer: each contour is a vertex count followed by that many
    // x, y pairs, so the shell can walk it without a second length array.
    for (const QPolygonF &contour :
        ugurugu::selectionOutline(handle->selectionMask))
    {
        if (contour.size() < 2)
        {
            continue;
        }
        handle->outlinePoints.append(static_cast<float>(contour.size()));
        for (const QPointF &point : contour)
        {
            handle->outlinePoints.append(static_cast<float>(point.x()));
            handle->outlinePoints.append(static_cast<float>(point.y()));
        }
    }
}

// Combines with the same >= 128 threshold every other selection consumer
// uses, so a combined mask never disagrees with hit testing or packing.
QImage combinedSelectionMask(
    const QImage &base, const QImage &addition, int combine)
{
    if (base.isNull() || base.size() != addition.size()
        || base.format() != addition.format())
    {
        return combine == CombineAdd ? addition : QImage();
    }
    QImage combined = base;
    for (int y = 0; y < combined.height(); ++y)
    {
        uchar *line = combined.scanLine(y);
        const uchar *additionLine = addition.constScanLine(y);
        for (int x = 0; x < combined.width(); ++x)
        {
            if (combine == CombineAdd)
            {
                line[x] = std::max(line[x], additionLine[x]);
            }
            else if (additionLine[x] >= 128)
            {
                line[x] = 0;
            }
        }
    }
    return combined;
}

void applySelectionCombine(BridgeDocument *handle,
    const QImage &mask,
    const QUuid &layerId,
    int combine)
{
    if (combine == CombineReplace || !selectionAppliesTo(handle, layerId))
    {
        installSelection(handle, mask, layerId);
        return;
    }
    installSelection(handle,
        combinedSelectionMask(handle->selectionMask, mask, combine),
        layerId);
}

// The image the flood fill reads. Mirrors CanvasWidget's three reference
// renderers: one isolated layer, every layer marked as a reference, or the
// whole visible composite — always over transparency so alpha boundaries mean
// what the user sees.
QImage referenceImage(BridgeDocument *handle, int frame, const QUuid &layerId)
{
    ugurugu::Document document = handle->controller->document();
    if (!document.size.isValid())
    {
        return {};
    }
    document.background = Qt::transparent;
    if (handle->fillReference == ReferenceActiveLayer)
    {
        const ugurugu::Layer *layer = document.layer(layerId);
        if (layer == nullptr)
        {
            return {};
        }
        ugurugu::Layer isolated = *layer;
        isolated.visible = true;
        isolated.opacity = 1.0;
        isolated.parentGroupId = {};
        document.layers = {isolated};
        return ugurugu::RenderEngine::render(document, frame);
    }
    if (handle->fillReference == ReferenceMarkedLayers)
    {
        bool hasVisibleReference = false;
        for (ugurugu::Layer &layer : document.layers)
        {
            if (layer.kind != ugurugu::LayerKind::Paint)
            {
                continue;
            }
            if (!layer.reference)
            {
                layer.visible = false;
                continue;
            }
            hasVisibleReference =
                hasVisibleReference || (layer.visible && layer.opacity > 0.0);
        }
        if (!hasVisibleReference)
        {
            return {};
        }
    }
    return ugurugu::RenderEngine::render(document, frame);
}

QImage maskFromShape(
    const QSize &size, int shape, const double *points, int count)
{
    if (!size.isValid() || points == nullptr || count < 2)
    {
        return {};
    }
    const auto pointAt = [points](int index)
    {
        return QPointF(points[index * 2], points[index * 2 + 1]);
    };
    QPainterPath path;
    if (shape == ShapeFreehand)
    {
        if (count < 3)
        {
            return {};
        }
        path.moveTo(pointAt(0));
        for (int index = 1; index < count; ++index)
        {
            path.lineTo(pointAt(index));
        }
    }
    else
    {
        const QRectF bounds =
            QRectF(pointAt(0), pointAt(count - 1)).normalized();
        if (bounds.width() < 1.0 || bounds.height() < 1.0)
        {
            return {};
        }
        if (shape == ShapeRectangle)
        {
            path.addRect(bounds);
        }
        else
        {
            path.addEllipse(bounds);
        }
    }

    QImage mask(size, QImage::Format_Grayscale8);
    if (mask.isNull())
    {
        return {};
    }
    mask.fill(0);
    // Antialiasing stays off: the mask is a binary stencil that packBinaryMask
    // and every hit test read at the >= 128 threshold.
    QPainter painter(&mask);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(path);
    painter.end();
    return mask;
}

// Commits a coverage mask as a Fill stroke, the same shape of document
// operation the desktop bucket and lasso-paint modes produce.
int commitFrozenFill(BridgeDocument *handle, const QImage &coverage, int frame)
{
    const ugurugu::Document &document = handle->controller->document();
    const QUuid layerId = paintTargetLayer(document);
    const ugurugu::Layer *layer = document.layer(layerId);
    if (layer == nullptr)
    {
        setError(StatusNoPaintLayer,
            QByteArrayLiteral("document has no paint layer"));
        return 0;
    }
    if (!layer->visible || layer->opacity <= 0.0)
    {
        setError(StatusLayerNotDrawable,
            QByteArrayLiteral("the active layer is hidden or fully "
                              "transparent"));
        return 0;
    }
    const std::optional<ugurugu::PackedMaskRegion> packedCoverage =
        ugurugu::packBinaryMask(coverage);
    if (!packedCoverage)
    {
        setError(
            StatusEmptyRegion, QByteArrayLiteral("no fillable area was found"));
        return 0;
    }

    ugurugu::Stroke fill;
    fill.id = QUuid::createUuid();
    fill.seed = QRandomGenerator::global()->generate64();
    fill.mode = ugurugu::StrokeMode::Fill;
    fill.color = handle->brushTemplate.color;
    fill.width = std::clamp(handle->brushTemplate.width,
        ugurugu::DocumentLimits::minimumStrokeWidth,
        ugurugu::DocumentLimits::maximumStrokeWidth);
    fill.brush = handle->brushTemplate.brush;
    fill.brush.antialiasing = handle->bucketAntialiasing;
    fill.fillCoverage = *packedCoverage;
    if (selectionAppliesTo(handle, layerId))
    {
        fill.clipMask = handle->selectionMask;
    }
    fill.points.append(
        ugurugu::StrokePoint{QPointF(packedCoverage->bounds.center()), 1.0});

    using AddStrokeResult = ugurugu::DocumentController::AddStrokeResult;
    const auto result = handle->controller->addStroke(layerId, std::move(fill));
    invalidateSplit(handle);
    renderCommittedFrame(handle, frame);
    if (result != AddStrokeResult::Added
        && result != AddStrokeResult::AddedWithResampledPoints)
    {
        setError(StatusStrokeRejected,
            QByteArrayLiteral("the fill was "
                              "rejected"));
        return 0;
    }
    clearError();
    return 1;
}

}

extern "C"
{

    // Bumped whenever an exported function is added, removed, or changes
    // meaning. The web worker refuses to run against a build whose version it
    // does not know, which turns a stale engine artifact into one clear error
    // instead of a missing-export TypeError somewhere later.
    EMSCRIPTEN_KEEPALIVE int ugu_abi_version()
    {
        return 3;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_schema_version()
    {
        return ugurugu::serializer_detail::schemaVersion;
    }

    EMSCRIPTEN_KEEPALIVE const char *ugu_last_error()
    {
        return lastError().constData();
    }

    // 0 ok, 1 invalid argument, 2 out of memory, 3 invalid document,
    // 4 no paint layer, 5 stroke rejected, 6 render failed,
    // 7 export too large, 8 export failed, 9 no selection,
    // 10 empty region, 11 layer not drawable. Callers should branch on this
    // and use ugu_last_error() only for diagnostics.
    EMSCRIPTEN_KEEPALIVE int ugu_last_error_code()
    {
        return lastErrorCode();
    }

    EMSCRIPTEN_KEEPALIVE BridgeDocument *ugu_document_new(int width, int height)
    {
        if (width < ugurugu::DocumentLimits::minimumCanvasEdge
            || height < ugurugu::DocumentLimits::minimumCanvasEdge
            || width > ugurugu::DocumentLimits::maximumCanvasEdge
            || height > ugurugu::DocumentLimits::maximumCanvasEdge)
        {
            setError(StatusInvalidArgument,
                QByteArrayLiteral("canvas size is outside the engine limits"));
            return nullptr;
        }
        auto handle = std::make_unique<BridgeDocument>();
        QString error;
        if (!handle->controller->newDocument(QSize(width, height), &error))
        {
            setError(StatusOutOfMemory, error.toUtf8());
            return nullptr;
        }
        clearError();
        return handle.release();
    }

    // Caps how many operations the undo stack keeps. The web shell sets this
    // from its memory profile because the engine has no byte-level history
    // budget; the desktop default of 64 is the upper bound.
    EMSCRIPTEN_KEEPALIVE void ugu_set_undo_limit(
        BridgeDocument *handle, int limit)
    {
        if (limit > 0)
        {
            handle->controller->undoStack()->setUndoLimit(limit);
        }
    }

    EMSCRIPTEN_KEEPALIVE BridgeDocument *ugu_document_open(
        const std::uint8_t *data, int size)
    {
        QString error;
        const QByteArray bytes(reinterpret_cast<const char *>(data), size);
        auto document = ugurugu::DocumentSerializer::fromJson(bytes, &error);
        if (!document)
        {
            setError(StatusDocumentInvalid, error.toUtf8());
            return nullptr;
        }
        auto handle = std::make_unique<BridgeDocument>();
        if (!handle->controller->loadDocument(std::move(*document), &error))
        {
            setError(StatusDocumentInvalid, error.toUtf8());
            return nullptr;
        }
        clearError();
        return handle.release();
    }

    EMSCRIPTEN_KEEPALIVE void ugu_document_close(BridgeDocument *handle)
    {
        delete handle;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_width(const BridgeDocument *handle)
    {
        return handle->controller->document().size.width();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_height(const BridgeDocument *handle)
    {
        return handle->controller->document().size.height();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_frame_count(
        const BridgeDocument *handle)
    {
        return handle->controller->document().animationFrames;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_document_layer_count(
        const BridgeDocument *handle)
    {
        return static_cast<int>(handle->controller->document().layers.size());
    }

    EMSCRIPTEN_KEEPALIVE double ugu_document_fps(const BridgeDocument *handle)
    {
        return handle->controller->document().framesPerSecond;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_brush_preset_count()
    {
        return static_cast<int>(ugurugu::BrushPresetCatalog::builtIns().size());
    }

    EMSCRIPTEN_KEEPALIVE const char *ugu_brush_preset_name(
        BridgeDocument *handle, int index)
    {
        const auto &presets = ugurugu::BrushPresetCatalog::builtIns();
        if (index < 0 || index >= presets.size())
        {
            return "";
        }
        handle->scratchText =
            ugurugu::BrushPresetCatalog::displayName(presets[index]).toUtf8();
        return handle->scratchText.constData();
    }

    EMSCRIPTEN_KEEPALIVE double ugu_brush_preset_default_size(int index)
    {
        const auto &presets = ugurugu::BrushPresetCatalog::builtIns();
        if (index < 0 || index >= presets.size())
        {
            return 6.0;
        }
        return presets[index].defaultSize;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_set_brush_preset(
        BridgeDocument *handle, int index)
    {
        const auto &presets = ugurugu::BrushPresetCatalog::builtIns();
        if (index < 0 || index >= presets.size())
        {
            return;
        }
        handle->brushTemplate.brush = presets[index].settings;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_eraser_preset_count()
    {
        return static_cast<int>(
            ugurugu::EraserPresetCatalog::builtIns().size());
    }

    EMSCRIPTEN_KEEPALIVE const char *ugu_eraser_preset_name(
        BridgeDocument *handle, int index)
    {
        const auto &presets = ugurugu::EraserPresetCatalog::builtIns();
        if (index < 0 || index >= presets.size())
        {
            return "";
        }
        handle->scratchText =
            ugurugu::EraserPresetCatalog::displayName(presets[index]).toUtf8();
        return handle->scratchText.constData();
    }

    EMSCRIPTEN_KEEPALIVE double ugu_eraser_preset_default_size(int index)
    {
        const auto &presets = ugurugu::EraserPresetCatalog::builtIns();
        if (index < 0 || index >= presets.size())
        {
            return 6.0;
        }
        return presets[index].defaultSize;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_set_eraser_preset(
        BridgeDocument *handle, int index)
    {
        const auto &presets = ugurugu::EraserPresetCatalog::builtIns();
        if (index < 0 || index >= presets.size())
        {
            return;
        }
        handle->brushTemplate.brush = presets[index].settings;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_set_brush(BridgeDocument *handle,
        int red,
        int green,
        int blue,
        int alpha,
        double width,
        int erase)
    {
        handle->brushTemplate.color = QColor(red, green, blue, alpha);
        handle->brushTemplate.width = width;
        handle->brushTemplate.mode = erase != 0 ? ugurugu::StrokeMode::Erase
                                                : ugurugu::StrokeMode::Paint;
    }

    // The desktop carries this per stroke from a brush-panel toggle
    // (CanvasWidgetTools.cpp:83); BrushSettings defaults it off and no preset
    // sets it, so without this the web could only ever draw aliased strokes.
    // Stored beside the template rather than on it because preset selection
    // replaces the template's brush wholesale, which used to silently drop
    // the toggle after switching tools.
    EMSCRIPTEN_KEEPALIVE void ugu_set_brush_antialiasing(
        BridgeDocument *handle, int antialiasing)
    {
        handle->brushAntialiasing = antialiasing != 0;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_set_stabilization(
        BridgeDocument *handle, double strength)
    {
        handle->stabilizer.setStrength(strength);
    }

    EMSCRIPTEN_KEEPALIVE int ugu_stroke_begin(BridgeDocument *handle,
        int frame,
        double x,
        double y,
        double pressure,
        double timestamp)
    {
        const ugurugu::Document &document = handle->controller->document();
        const QUuid layerId = paintTargetLayer(document);
        if (layerId.isNull())
        {
            setError(StatusNoPaintLayer,
                QByteArrayLiteral("document has no paint layer"));
            return 0;
        }
        handle->strokeFrame = frame;
        handle->strokeLayerId = layerId;
        handle->activeStroke = handle->brushTemplate;
        handle->activeStroke.id = QUuid::createUuid();
        handle->activeStroke.seed = QRandomGenerator::global()->generate64();
        // Erasers keep their preset's antialiasing, matching the desktop's
        // beginStroke which applies the toggle to paint strokes only.
        if (handle->activeStroke.mode == ugurugu::StrokeMode::Paint)
        {
            handle->activeStroke.brush.antialiasing = handle->brushAntialiasing;
        }
        // A selection made on this layer confines the stroke to it, exactly as
        // CanvasWidget::beginStroke does. Both the incremental preview and the
        // committed render honour clipMask, so what is drawn is what lands.
        if (selectionAppliesTo(handle, layerId))
        {
            handle->activeStroke.clipMask = handle->selectionMask;
        }
        const QPointF raw(x, y);
        const auto time = static_cast<quint64>(timestamp);
        handle->activeStroke.points = {
            canvasPoint(handle, handle->stabilizer.begin(raw, time), pressure)};
        handle->lastRawPosition = raw;
        handle->lastTimestamp = time;
        handle->strokeInProgress = true;

        // A split promoted by the previous ugu_stroke_end already holds this
        // layer's committed pixels, so the common case of drawing stroke after
        // stroke never pays for renderLayerSplit again.
        const bool reusable =
            handle->splitUsable && handle->split.valid
            && handle->splitLayerId == layerId && handle->splitFrame == frame
            && handle->split.below.size() == document.size
            && ugurugu::RenderEngine::supportsLayerSplit(document, layerId);
        if (!reusable)
        {
            handle->split = ugurugu::RenderEngine::renderLayerSplit(document,
                frame,
                document.size,
                layerId,
                ugurugu::RenderEngine::ScaledRenderMode::NativeExact);
            handle->splitLayerId = layerId;
            handle->splitFrame = frame;
            handle->splitUsable = handle->split.valid;
        }
        handle->incrementalActive = handle->split.valid;
        if (!handle->incrementalActive)
        {
            renderCommittedFrame(handle, frame);
            clearError();
            return 1;
        }

        handle->incremental.clear();
        if (!handle->renderedFrameCommitted
            || handle->renderedFrameIndex != frame
            || handle->renderedFrame.size() != document.size)
        {
            // Composing the split with its own base layer reproduces the
            // committed frame exactly — LayerSplitPreviewTests pins that — and
            // costs three image composites instead of a second full render.
            QImage composed = ugurugu::RenderEngine::composeLayerSplit(
                handle->split, handle->split.layerBase);
            if (composed.isNull())
            {
                renderCommittedFrame(handle, frame);
            }
            else
            {
                handle->renderedFrame = std::move(composed);
                handle->renderedFrameIndex = frame;
                handle->renderedFrameCommitted = true;
                handle->dirty = handle->renderedFrame.rect();
            }
        }
        else
        {
            handle->dirty = QRect();
        }
        clearError();
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_stroke_append(BridgeDocument *handle,
        double x,
        double y,
        double pressure,
        double timestamp)
    {
        if (!handle->strokeInProgress)
        {
            return;
        }
        const QPointF raw(x, y);
        const auto time = static_cast<quint64>(timestamp);
        handle->activeStroke.points.append(canvasPoint(
            handle, handle->stabilizer.update(raw, time), pressure));
        handle->lastRawPosition = raw;
        handle->lastTimestamp = time;
    }

    // Renders the active stroke's pending points into the frame buffer and
    // narrows the dirty rectangle to the touched tiles when the incremental
    // path holds; otherwise the whole frame is re-rendered and dirty.
    EMSCRIPTEN_KEEPALIVE int ugu_stroke_render(BridgeDocument *handle)
    {
        if (!handle->strokeInProgress)
        {
            return 0;
        }
        if (!handle->incrementalActive)
        {
            renderFullStrokePreview(handle);
            return 1;
        }
        QRect dirty;
        if (!updateIncrementalStroke(handle, &dirty))
        {
            handle->incrementalActive = false;
            invalidateSplit(handle);
            renderFullStrokePreview(handle);
            return 1;
        }
        handle->dirty = dirty.intersected(handle->renderedFrame.rect());
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_stroke_end(BridgeDocument *handle)
    {
        if (!handle->strokeInProgress)
        {
            return -1;
        }
        const QPointF finished = canvasPoint(handle,
            handle->stabilizer.finish(
                handle->lastRawPosition, handle->lastTimestamp),
            1.0)
                                     .position;
        if (auto &points = handle->activeStroke.points; !points.isEmpty())
        {
            const QPointF delta = finished - points.last().position;
            // Matches CanvasWidget::endStroke: an endpoint the resampler would
            // merge away is moved into the last point instead of appended, so
            // the committed geometry matches what the preview drew.
            if (points.size() >= ugurugu::DocumentLimits::maximumPointsPerStroke
                || QPointF::dotProduct(delta, delta) < 0.75 * 0.75)
            {
                points.last().position = finished;
            }
            else
            {
                points.append(
                    ugurugu::StrokePoint{finished, points.last().pressure});
            }
        }
        handle->strokeInProgress = false;

        // Bring the preview up to the final point and bake it into the split's
        // base layer. If the commit then lands unchanged, that promoted split
        // is the committed document's split and renderedFrame already shows
        // it, so no full render is needed here or at the next stroke.
        QRect tailDirty;
        ugurugu::RenderEngine::LayerSplitFrame promoted;
        bool promotable = false;
        if (handle->incrementalActive
            && updateIncrementalStroke(handle, &tailDirty))
        {
            promoted = handle->split;
            promotable = handle->incremental.applyTo(promoted.layerBase);
        }
        handle->incrementalActive = false;

        const QUuid layerId = handle->strokeLayerId;
        const auto result = handle->controller->addStroke(
            layerId, std::move(handle->activeStroke));
        handle->activeStroke = {};
        using AddStrokeResult = ugurugu::DocumentController::AddStrokeResult;
        if (promotable && result == AddStrokeResult::Added)
        {
            handle->split = std::move(promoted);
            handle->splitLayerId = layerId;
            handle->splitFrame = handle->strokeFrame;
            handle->splitUsable = true;
            handle->renderedFrameIndex = handle->strokeFrame;
            handle->renderedFrameCommitted = true;
            handle->dirty = tailDirty.intersected(handle->renderedFrame.rect());
        }
        else
        {
            invalidateSplit(handle);
            renderCommittedFrame(handle, handle->strokeFrame);
        }
        if (result != AddStrokeResult::Added
            && result != AddStrokeResult::AddedWithResampledPoints)
        {
            setError(
                StatusStrokeRejected, QByteArrayLiteral("stroke rejected"));
        }
        return static_cast<int>(result);
    }

    EMSCRIPTEN_KEEPALIVE void ugu_stroke_cancel(BridgeDocument *handle)
    {
        handle->strokeInProgress = false;
        handle->incrementalActive = false;
        invalidateSplit(handle);
        handle->activeStroke = {};
        renderCommittedFrame(handle, handle->strokeFrame);
    }

    // reference: 0 active layer, 1 layers marked as references, 2 all visible
    // layers. comparison: 0 alpha boundary, 1 color tolerance. tolerance is
    // 0-255 and only applies to the color comparison.
    EMSCRIPTEN_KEEPALIVE void ugu_set_fill_options(BridgeDocument *handle,
        int reference,
        int comparison,
        int tolerance,
        int antialiasing)
    {
        handle->fillReference = std::clamp(
            reference, 0, static_cast<int>(ReferenceAllVisibleLayers));
        handle->fillComparison =
            comparison == 1 ? ugurugu::FloodFillMask::Comparison::Color
                            : ugurugu::FloodFillMask::Comparison::AlphaBoundary;
        handle->fillTolerance = std::clamp(tolerance, 0, 255);
        handle->bucketAntialiasing = antialiasing != 0;
    }

    // Floods from the seed on the configured reference image and commits the
    // result as one Fill stroke. Returns 1 on success; on 0 the status code
    // says whether the point was outside the selection, the area was not
    // fillable, or the layer refused the stroke.
    EMSCRIPTEN_KEEPALIVE int ugu_bucket_fill(
        BridgeDocument *handle, int frame, double x, double y)
    {
        const ugurugu::Document &document = handle->controller->document();
        const QSize size = document.size;
        if (!QRectF(QPointF(0.0, 0.0), QSizeF(size)).contains(QPointF(x, y)))
        {
            setError(StatusInvalidArgument,
                QByteArrayLiteral("the fill point is outside the canvas"));
            return 0;
        }
        const QUuid layerId = paintTargetLayer(document);
        if (layerId.isNull())
        {
            setError(StatusNoPaintLayer,
                QByteArrayLiteral("document has no paint layer"));
            return 0;
        }
        const QPoint seed(std::clamp(static_cast<int>(x), 0, size.width() - 1),
            std::clamp(static_cast<int>(y), 0, size.height() - 1));
        // A selection is a wall: the desktop refuses a fill seeded outside it
        // rather than quietly filling the whole layer and clipping it away.
        if (selectionAppliesTo(handle, layerId)
            && handle->selectionMask.constScanLine(seed.y())[seed.x()] < 128)
        {
            setError(StatusNoSelection,
                QByteArrayLiteral("click inside the selected area to fill it"));
            return 0;
        }

        const QImage reference = referenceImage(handle, frame, layerId);
        if (reference.isNull())
        {
            setError(StatusEmptyRegion,
                handle->fillReference == ReferenceMarkedLayers
                    ? QByteArrayLiteral("mark a visible paint layer as a "
                                        "reference first")
                    : QByteArrayLiteral("the reference image could not be "
                                        "rendered"));
            return 0;
        }
        const QImage coverage = ugurugu::FloodFillMask::fromImage(
            reference, seed, handle->fillComparison, handle->fillTolerance);
        if (coverage.isNull())
        {
            setError(StatusEmptyRegion,
                QByteArrayLiteral("no fillable area was found"));
            return 0;
        }
        return commitFrozenFill(handle, coverage, frame);
    }

    // points is a flat x, y array of `count` document-space points: the path
    // for a freehand lasso, or the two drag corners for a rectangle or
    // ellipse. paint 1 fills the area instead of selecting it, which is the
    // desktop lasso's Paint mode.
    EMSCRIPTEN_KEEPALIVE int ugu_selection_shape(BridgeDocument *handle,
        int frame,
        int shape,
        const double *points,
        int count,
        int combine,
        int paint)
    {
        const ugurugu::Document &document = handle->controller->document();
        const QUuid layerId = paintTargetLayer(document);
        if (layerId.isNull())
        {
            setError(StatusNoPaintLayer,
                QByteArrayLiteral("document has no paint layer"));
            return 0;
        }
        const QImage mask = maskFromShape(document.size, shape, points, count);
        if (mask.isNull())
        {
            setError(StatusEmptyRegion,
                QByteArrayLiteral("the drawn area is too small to use"));
            return 0;
        }
        if (paint != 0)
        {
            return commitFrozenFill(handle, mask, frame);
        }
        applySelectionCombine(handle, mask, layerId, combine);
        clearError();
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_selection_flood(
        BridgeDocument *handle, int frame, double x, double y, int combine)
    {
        const ugurugu::Document &document = handle->controller->document();
        const QSize size = document.size;
        if (!QRectF(QPointF(0.0, 0.0), QSizeF(size)).contains(QPointF(x, y)))
        {
            setError(StatusInvalidArgument,
                QByteArrayLiteral("the point is outside the canvas"));
            return 0;
        }
        const QUuid layerId = paintTargetLayer(document);
        if (layerId.isNull())
        {
            setError(StatusNoPaintLayer,
                QByteArrayLiteral("document has no paint layer"));
            return 0;
        }
        const QImage reference = referenceImage(handle, frame, layerId);
        if (reference.isNull())
        {
            setError(StatusEmptyRegion,
                handle->fillReference == ReferenceMarkedLayers
                    ? QByteArrayLiteral("mark a visible paint layer as a "
                                        "reference first")
                    : QByteArrayLiteral("the reference image could not be "
                                        "rendered"));
            return 0;
        }
        const QPoint seed(std::clamp(static_cast<int>(x), 0, size.width() - 1),
            std::clamp(static_cast<int>(y), 0, size.height() - 1));
        const QImage mask = ugurugu::FloodFillMask::fromImage(
            reference, seed, handle->fillComparison, handle->fillTolerance);
        if (mask.isNull())
        {
            setError(StatusEmptyRegion,
                QByteArrayLiteral("click an area enclosed by lines to select "
                                  "it"));
            return 0;
        }
        applySelectionCombine(handle, mask, layerId, combine);
        clearError();
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_selection_all(BridgeDocument *handle)
    {
        const ugurugu::Document &document = handle->controller->document();
        const QUuid layerId = paintTargetLayer(document);
        if (layerId.isNull())
        {
            setError(StatusNoPaintLayer,
                QByteArrayLiteral("document has no paint layer"));
            return 0;
        }
        QImage mask(document.size, QImage::Format_Grayscale8);
        if (mask.isNull())
        {
            setError(StatusOutOfMemory,
                QByteArrayLiteral("the selection mask could not be allocated"));
            return 0;
        }
        mask.fill(255);
        installSelection(handle, std::move(mask), layerId);
        clearError();
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_selection_invert(BridgeDocument *handle)
    {
        if (handle->selectionMask.isNull())
        {
            setError(StatusNoSelection,
                QByteArrayLiteral("there is no selection to invert"));
            return 0;
        }
        QImage inverted = handle->selectionMask;
        inverted.invertPixels();
        installSelection(handle, std::move(inverted), handle->selectionLayerId);
        clearError();
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_selection_clear(BridgeDocument *handle)
    {
        if (handle->selectionMask.isNull())
        {
            return;
        }
        installSelection(handle, QImage(), QUuid());
    }

    EMSCRIPTEN_KEEPALIVE int ugu_selection_fill(
        BridgeDocument *handle, int frame)
    {
        const QUuid layerId = paintTargetLayer(handle->controller->document());
        if (!selectionAppliesTo(handle, layerId))
        {
            setError(StatusNoSelection,
                QByteArrayLiteral("select an area on this layer to fill"));
            return 0;
        }
        // Held by value: committing the fill may replace the selection, and
        // the fill clips to the same mask so it lands exactly inside the
        // marching ants instead of bleeding a pixel past them.
        const QImage mask = handle->selectionMask;
        return commitFrozenFill(handle, mask, frame);
    }

    EMSCRIPTEN_KEEPALIVE int ugu_selection_delete(
        BridgeDocument *handle, int frame)
    {
        const ugurugu::Document &document = handle->controller->document();
        const QUuid layerId = paintTargetLayer(document);
        if (!selectionAppliesTo(handle, layerId))
        {
            setError(StatusNoSelection,
                QByteArrayLiteral("select an area on this layer to delete"));
            return 0;
        }
        const ugurugu::Layer *layer = document.layer(layerId);
        if (layer == nullptr)
        {
            setError(StatusNoPaintLayer,
                QByteArrayLiteral("document has no paint layer"));
            return 0;
        }
        // The desktop offers every stroke on the layer to the controller and
        // lets removeSelectedContent decide which ones the mask actually
        // covers; matching that keeps one definition of "selected content".
        QVector<QUuid> strokeIds;
        strokeIds.reserve(layer->strokes.size());
        for (const ugurugu::Stroke &stroke : layer->strokes)
        {
            strokeIds.append(stroke.id);
        }
        const bool removed = handle->controller->removeSelectedContent(
            layerId, strokeIds, handle->selectionMask);
        if (!removed)
        {
            setError(StatusEmptyRegion,
                QByteArrayLiteral("there is no content in the selected area"));
            return 0;
        }
        installSelection(handle, QImage(), QUuid());
        invalidateSplit(handle);
        renderCommittedFrame(handle, frame);
        clearError();
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_selection_active(const BridgeDocument *handle)
    {
        return handle->selectionMask.isNull() ? 0 : 1;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_selection_revision(
        const BridgeDocument *handle)
    {
        return handle->selectionRevision;
    }

    // Flat float buffer of closed contours in document coordinates: a vertex
    // count, then that many x, y pairs, repeated. Valid until the next
    // selection change or close on the same handle.
    EMSCRIPTEN_KEEPALIVE const float *ugu_selection_outline(
        const BridgeDocument *handle)
    {
        return handle->outlinePoints.constData();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_selection_outline_size(
        const BridgeDocument *handle)
    {
        return static_cast<int>(handle->outlinePoints.size());
    }

    EMSCRIPTEN_KEEPALIVE int ugu_can_undo(const BridgeDocument *handle)
    {
        return handle->controller->undoStack()->canUndo() ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_can_redo(const BridgeDocument *handle)
    {
        return handle->controller->undoStack()->canRedo() ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE void ugu_undo(BridgeDocument *handle)
    {
        handle->controller->undoStack()->undo();
        invalidateSplit(handle);
    }

    EMSCRIPTEN_KEEPALIVE void ugu_redo(BridgeDocument *handle)
    {
        handle->controller->undoStack()->redo();
        invalidateSplit(handle);
    }

    EMSCRIPTEN_KEEPALIVE const char *ugu_layer_name(
        BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        if (layer == nullptr)
        {
            return "";
        }
        handle->scratchText = layer->name.toUtf8();
        return handle->scratchText.constData();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_layer_kind(
        const BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        return layer != nullptr && layer->kind == ugurugu::LayerKind::Group ? 1
                                                                            : 0;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_layer_visible(
        const BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        return layer != nullptr && layer->visible ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE double ugu_layer_opacity(
        const BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        return layer != nullptr ? layer->opacity : 1.0;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_layer_is_active(
        const BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        return layer != nullptr
                       && layer->id
                              == handle->controller->document().activeLayerId
                   ? 1
                   : 0;
    }

    EMSCRIPTEN_KEEPALIVE int ugu_layer_depth(
        const BridgeDocument *handle, int index)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        if (layer == nullptr)
        {
            return 0;
        }
        return handle->controller->document().layerDepth(layer->id);
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_activate(
        BridgeDocument *handle, int index)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->setActiveLayer(layer->id);
            invalidateSplit(handle);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_set_visible(
        BridgeDocument *handle, int index, int visible)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->setLayerVisible(layer->id, visible != 0);
            invalidateSplit(handle);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_set_opacity(
        BridgeDocument *handle, int index, double opacity)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->setLayerOpacity(layer->id, opacity);
            invalidateSplit(handle);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_add(BridgeDocument *handle)
    {
        const ugurugu::Document &document = handle->controller->document();
        QUuid parentGroupId;
        if (const ugurugu::Layer *active =
                document.layer(document.activeLayerId))
        {
            parentGroupId = active->parentGroupId;
        }
        handle->controller->addLayer(parentGroupId);
        invalidateSplit(handle);
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_remove(
        BridgeDocument *handle, int index)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->removeLayer(layer->id);
            invalidateSplit(handle);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_rename(
        BridgeDocument *handle, int index, const char *name)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->renameLayer(layer->id, QString::fromUtf8(name));
            invalidateSplit(handle);
        }
    }

    EMSCRIPTEN_KEEPALIVE void ugu_layer_move(
        BridgeDocument *handle, int index, int offset)
    {
        if (const ugurugu::Layer *layer = layerAtIndex(handle, index))
        {
            handle->controller->moveLayer(layer->id, offset);
            invalidateSplit(handle);
        }
    }

    // The returned buffer is QImage::Format_ARGB32_Premultiplied: on the
    // little-endian wasm heap each pixel is the byte sequence B, G, R, A with
    // premultiplied color channels. Rows are ugu_frame_bytes_per_line() apart.
    // The pointer stays valid until the next render or close on the same
    // handle. ugu_dirty_* describe the region the last render actually
    // changed; consumers may upload just that region.
    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_render_frame(
        BridgeDocument *handle, int frameIndex)
    {
        renderCommittedFrame(handle, frameIndex);
        if (handle->renderedFrame.isNull())
        {
            setError(StatusRenderFailed,
                QByteArrayLiteral("render produced a null frame"));
            return nullptr;
        }
        clearError();
        return handle->renderedFrame.constBits();
    }

    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_frame_pixels(
        const BridgeDocument *handle)
    {
        return handle->renderedFrame.constBits();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_frame_width(const BridgeDocument *handle)
    {
        return handle->renderedFrame.width();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_frame_height(const BridgeDocument *handle)
    {
        return handle->renderedFrame.height();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_frame_bytes_per_line(
        const BridgeDocument *handle)
    {
        return static_cast<int>(handle->renderedFrame.bytesPerLine());
    }

    EMSCRIPTEN_KEEPALIVE int ugu_dirty_x(const BridgeDocument *handle)
    {
        return handle->dirty.x();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_dirty_y(const BridgeDocument *handle)
    {
        return handle->dirty.y();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_dirty_width(const BridgeDocument *handle)
    {
        return handle->dirty.width();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_dirty_height(const BridgeDocument *handle)
    {
        return handle->dirty.height();
    }

    // Renders the layer subtree at index as a static thumbnail (frame 0,
    // wobble off), the same picture the desktop layer dock shows. The buffer
    // is premultiplied BGRA like ugu_render_frame and stays valid until the
    // next thumbnail render or close on the same handle.
    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_layer_thumbnail(
        BridgeDocument *handle, int index, double devicePixelRatio)
    {
        const ugurugu::Layer *layer = layerAtIndex(handle, index);
        if (layer == nullptr)
        {
            return nullptr;
        }
        handle->thumbnail = ugurugu::LayerThumbnailRenderer::renderImage(
            handle->controller->document(), *layer, devicePixelRatio);
        if (handle->thumbnail.isNull())
        {
            return nullptr;
        }
        return handle->thumbnail.constBits();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_thumbnail_width(const BridgeDocument *handle)
    {
        return handle->thumbnail.width();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_thumbnail_height(const BridgeDocument *handle)
    {
        return handle->thumbnail.height();
    }

    EMSCRIPTEN_KEEPALIVE int ugu_thumbnail_bytes_per_line(
        const BridgeDocument *handle)
    {
        return static_cast<int>(handle->thumbnail.bytesPerLine());
    }

    // Encodes every animation frame into a GIF, matching the desktop export
    // path (NativeExact pixels, drift-corrected centisecond delays, alpha
    // preserved). The frame set is held in memory like the desktop worker, so
    // documents over the web budget are refused up front instead of risking a
    // tab kill. The pointer stays valid until the next export or close on the
    // same handle; the size comes from ugu_export_size().
    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_export_gif(
        BridgeDocument *handle)
    {
        const ugurugu::Document &document = handle->controller->document();
        constexpr qint64 webFrameSetBudget = 128LL * 1024LL * 1024LL;
        const qint64 frameSetBytes = static_cast<qint64>(document.size.width())
                                     * document.size.height() * 4LL
                                     * document.animationFrames;
        if (frameSetBytes > webFrameSetBudget)
        {
            setError(StatusExportTooLarge,
                QByteArrayLiteral("document is too large for web GIF export"));
            return nullptr;
        }

        QVector<QImage> frames;
        frames.reserve(document.animationFrames);
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QImage image = ugurugu::RenderEngine::render(document, frame);
            if (image.isNull())
            {
                setError(StatusRenderFailed,
                    QByteArrayLiteral("an animation frame could not render"));
                return nullptr;
            }
            frames.append(std::move(image));
        }

        const QString path = QStringLiteral("/ugurugu-export.gif");
        QString error;
        if (!ugurugu::GifWriter::write(path,
                frames,
                ugurugu::AnimationExportPolicy::frameDurations(
                    document.animationFrames, document.framesPerSecond, 100),
                &error))
        {
            setError(StatusExportFailed, error.toUtf8());
            return nullptr;
        }
        QFile output(path);
        if (!output.open(QIODevice::ReadOnly))
        {
            setError(StatusExportFailed,
                QByteArrayLiteral("encoded GIF could not be read"));
            return nullptr;
        }
        handle->exportBytes = output.readAll();
        output.close();
        QFile::remove(path);
        if (handle->exportBytes.isEmpty())
        {
            setError(
                StatusExportFailed, QByteArrayLiteral("encoded GIF is empty"));
            return nullptr;
        }
        clearError();
        return reinterpret_cast<const std::uint8_t *>(
            handle->exportBytes.constData());
    }

    EMSCRIPTEN_KEEPALIVE int ugu_export_size(const BridgeDocument *handle)
    {
        return static_cast<int>(handle->exportBytes.size());
    }

    // The pointer stays valid until the next serialize or close on the same
    // handle.
    EMSCRIPTEN_KEEPALIVE const std::uint8_t *ugu_serialize(
        BridgeDocument *handle)
    {
        handle->serialized =
            ugurugu::DocumentSerializer::toJson(handle->controller->document());
        if (handle->serialized.isEmpty())
        {
            setError(StatusExportFailed,
                QByteArrayLiteral("serialization produced no bytes"));
            return nullptr;
        }
        clearError();
        return reinterpret_cast<const std::uint8_t *>(
            handle->serialized.constData());
    }

    EMSCRIPTEN_KEEPALIVE int ugu_serialized_size(const BridgeDocument *handle)
    {
        return static_cast<int>(handle->serialized.size());
    }
}
