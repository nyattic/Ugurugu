#pragma once

#include "document/Document.hpp"

#include <QImage>
#include <QPixmap>

namespace ugurugu
{

class LayerThumbnailRenderer final
{
public:
    static constexpr QSize targetSize{96, 64};

    static QSize renderSize(const QSize &documentSize);
    static QImage renderImage(const Document &document, const Layer &layer);
    static QPixmap render(const Document &document, const Layer &layer);
};

}
