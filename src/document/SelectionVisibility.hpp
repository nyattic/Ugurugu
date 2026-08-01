#pragma once

#include "document/Document.hpp"

namespace wobble
{

class SelectionVisibility final
{
public:
    struct Result
    {
        bool hasVisiblePixels = false;
        bool renderSucceeded = false;
        int renderedFrames = 0;
    };

    static Result evaluate(const Document &document,
        const Layer &layer,
        const QImage &selectionMask,
        int preferredFrame = 0);
};

}
