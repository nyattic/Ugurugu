#pragma once

#include "document/Document.hpp"

#include <QImage>

#include <optional>

namespace wobble
{

struct SelectionTransformMemoryStats
{
    QRect sourceBounds;
    QRect targetBounds;
    quint64 sourceImageBytes = 0;
    quint64 targetImageBytes = 0;
    quint64 resultBytes = 0;
    quint64 peakLiveImageBytes = 0;
    bool usedArgbSource = false;
    bool usedArgbTarget = false;
    bool usedFullTargetFallback = false;
};

std::optional<PackedMaskRegion> packBinaryMask(const QImage &mask);

bool isValidPackedMaskRegion(const PackedMaskRegion &region);

bool packedMaskContains(
    const PackedMaskRegion &region, int documentX, int documentY);

QImage unpackBinaryMask(const PackedMaskRegion &region);

SamplingMode samplingForSelectionTransform(const QTransform &transform);

std::optional<PackedMaskRegion> transformedPackedMask(
    const PackedMaskRegion &region,
    const QSize &targetSize,
    const QTransform &transform,
    SelectionTransformMemoryStats *memoryStats = nullptr);

std::optional<PixelSelectionOp> makePixelSelectionOp(
    const QImage &selectionMask,
    const QTransform &transform,
    bool clearSource,
    bool drawDestination);

bool isValidPixelSelectionOp(const PixelSelectionOp &operation);

QImage transformedSelectionSupport(const QImage &selectionMask,
    const QSize &targetSize,
    const QTransform &transform,
    SamplingMode sampling,
    SelectionTransformMemoryStats *memoryStats = nullptr);

bool pixelSelectionContains(
    const PixelSelectionOp &operation, int documentX, int documentY);

QImage unpackPixelSelectionMask(const PixelSelectionOp &operation);

quint64 packedSelectionBytes(const Document &document);

bool isValidReframeOp(const ReframeOp &operation);

}
