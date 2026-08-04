#include "document/history/HistoryMemory.hpp"

namespace ugurugu
{
namespace history
{

void accountByteStorage(MemoryFootprint &footprint, const QByteArray &bytes)
{
    if (bytes.capacity() <= 0 || !bytes.constData())
    {
        return;
    }
    footprint.addShared(QStringLiteral("bytes"),
        reinterpret_cast<quintptr>(bytes.constData()),
        bytes.capacity());
}

void accountImageStorage(MemoryFootprint &footprint, const QImage &image)
{
    if (!image.isNull())
    {
        footprint.addShared(QStringLiteral("image"),
            static_cast<quint64>(image.cacheKey()),
            image.sizeInBytes());
    }
}

void accountPackedMask(
    MemoryFootprint &footprint, const std::optional<PackedMaskRegion> &mask)
{
    if (mask)
    {
        accountByteStorage(footprint, mask->packedMask);
    }
}

}

}
