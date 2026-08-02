#pragma once

#include <QtTypes>

namespace wobble
{

struct MemoryBudget final
{
    static constexpr qint64 residentTargetBytes = 768LL * 1024LL * 1024LL;
    static constexpr qint64 historyResidentBytes = 192LL * 1024LL * 1024LL;
    static constexpr qint64 serializationCacheBytes = 32LL * 1024LL * 1024LL;
    static constexpr int previewCacheKiB = 128 * 1024;
    static constexpr quint64 animationExportWorkingBytes =
        512ULL * 1024ULL * 1024ULL;
};

static_assert(
    MemoryBudget::historyResidentBytes
        + static_cast<qint64>(MemoryBudget::animationExportWorkingBytes)
    <= MemoryBudget::residentTargetBytes);
static_assert(MemoryBudget::historyResidentBytes
                  + MemoryBudget::serializationCacheBytes
                  + static_cast<qint64>(MemoryBudget::previewCacheKiB) * 1024LL
              <= MemoryBudget::residentTargetBytes);

}
