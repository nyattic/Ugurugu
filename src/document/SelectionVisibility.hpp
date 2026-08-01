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

    struct EditableStrokeStats
    {
        quint64 fullCanvasFallbacks = 0;
        quint64 regionalRenders = 0;
        quint64 pixelSelectionOperationsReplayed = 0;
        quint64 reframeOperationsReplayed = 0;
        quint64 eraseOperationsReplayed = 0;
        quint64 effectCandidatesExamined = 0;
        quint64 maximumExplicitImageBytes = 0;
    };

    static Result evaluate(const Document &document,
        const Layer &layer,
        const QImage &selectionMask,
        int preferredFrame = 0);
    static QVector<QUuid> editableStrokeIds(const Document &document,
        const Layer &layer,
        const QImage &selectionMask,
        int frameIndex,
        EditableStrokeStats *stats = nullptr);
};

}
