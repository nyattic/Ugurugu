#pragma once

#include "document/Document.hpp"

#include <QImage>

namespace ugurugu
{

class RasterAssetCache final
{
public:
    static QImage image(const Document &document, const QString &assetId);
    static QImage transformedImage(const Document &document,
        const QString &assetId,
        const QSize &targetSize,
        const QTransform &transform,
        SamplingMode sampling);
    static void clear();
};

}
