#pragma once

namespace wobble
{

enum class CanvasTool
{
    Brush,
    Eraser,
    Lasso,
    Wand,
    Bucket
};

enum class CanvasWandReference
{
    ActiveLayer,
    ReferenceLayers,
    AllVisibleLayers
};

enum class CanvasSelectionShape
{
    Freehand,
    Rectangle,
    Ellipse
};

enum class CanvasSelectionCombine
{
    Replace,
    Add,
    Subtract
};

}
