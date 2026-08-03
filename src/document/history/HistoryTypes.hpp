#pragma once

#include <QtGlobal>

namespace wobble
{
namespace history
{

struct StorageStats
{
    qsizetype retainedLayers = 0;
    qsizetype retainedStrokes = 0;
    qsizetype retainedPreparedDocuments = 0;
    qsizetype entryCount = 0;
    qsizetype stagedPreparedDocuments = 0;
    qsizetype peakTransientPreparedDocuments = 0;
    qsizetype macroPreparedDocuments = 0;
    qint64 retainedBytes = 0;
    bool residentBudgetSoftExceeded = false;
};

// Adjacent commands collapse into one undo entry only when they carry the
// same merge id, so every mergeable gesture needs an id distinct from all
// others. Reusing an id across unrelated properties would let two different
// edits merge into a single entry that cannot be undone separately.
constexpr int wobbleAmountMergeId = 1;
constexpr int animationFramesMergeId = 2;
constexpr int framesPerSecondMergeId = 3;
constexpr int layerOpacityMergeId = 4;

}

}
