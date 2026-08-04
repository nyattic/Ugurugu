#pragma once

#include "document/Document.hpp"

namespace ugurugu
{

struct RenderExportMemoryEstimate final
{
    bool valid = false;
    long double hierarchyTransientBytes = 0.0L;
    long double workingBytes = 0.0L;
};

class RenderExportPolicy final
{
public:
    static RenderExportMemoryEstimate staticImage(const Document &document);
    static RenderExportMemoryEstimate animatedGif(const Document &document);
    static bool staticImageFitsMemoryBudget(const Document &document);
    static bool animatedGifFitsMemoryBudget(const Document &document);
};

}
