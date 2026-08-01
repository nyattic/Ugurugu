#pragma once

#include "document/Document.hpp"

#include <QPixmap>

namespace wobble
{

class LayerThumbnailRenderer final
{
public:
    static constexpr QSize targetSize{96, 64};

    static QSize renderSize(const QSize &documentSize);
    static QPixmap render(const Document &document, const Layer &layer);
};

}
