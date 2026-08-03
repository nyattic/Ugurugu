#pragma once

#include <QtTypes>

namespace wobble
{

// The large in-memory subsystems share one documented process budget. Export
// clears preview, serialization and decoded raster caches before allocating
// its working set. Project serialization clears decoded raster images before
// constructing JSON while retaining compressed raster payloads.
struct MemoryBudget final
{
    static constexpr qint64 residentTargetBytes = 1024LL * 1024LL * 1024LL;
    static constexpr qint64 historyResidentBytes = 192LL * 1024LL * 1024LL;
    static constexpr qint64 serializationCacheBytes = 64LL * 1024LL * 1024LL;
    static constexpr qint64 rasterAssetEncodedBytes = 72LL * 1024LL * 1024LL;
    static constexpr qint64 rasterDecodeCacheBytes = 128LL * 1024LL * 1024LL;
    static constexpr qint64 projectSerializationWorkingBytes =
        512LL * 1024LL * 1024LL;
    static constexpr int previewCacheKiB = 128 * 1024;
    static constexpr quint64 animationExportWorkingBytes =
        512ULL * 1024ULL * 1024ULL;
};

static_assert(
    MemoryBudget::historyResidentBytes + MemoryBudget::rasterAssetEncodedBytes
        + static_cast<qint64>(MemoryBudget::animationExportWorkingBytes)
    <= MemoryBudget::residentTargetBytes);
static_assert(MemoryBudget::historyResidentBytes
                  + MemoryBudget::serializationCacheBytes
                  + MemoryBudget::rasterAssetEncodedBytes
                  + MemoryBudget::rasterDecodeCacheBytes
                  + static_cast<qint64>(MemoryBudget::previewCacheKiB) * 1024LL
              <= MemoryBudget::residentTargetBytes);
static_assert(MemoryBudget::historyResidentBytes
                  + MemoryBudget::serializationCacheBytes
                  + MemoryBudget::rasterAssetEncodedBytes
                  + MemoryBudget::projectSerializationWorkingBytes
                  + static_cast<qint64>(MemoryBudget::previewCacheKiB) * 1024LL
              <= MemoryBudget::residentTargetBytes);

}
