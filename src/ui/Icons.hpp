#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace wobble
{

enum class IconGlyph
{
    Brush,
    Eraser,
    Undo,
    Redo,
    Play,
    Pause,
    Add,
    Duplicate,
    Copy,
    Remove,
    MoveUp,
    MoveDown,
    EyeOpen,
    EyeClosed,
    FitView,
    MirrorHorizontal,
    Lasso,
    Wand,
    Bucket,
    Settings,
    Move,
    Scale,
    Rotate,
    MirrorVertical,
    Delete,
    Deselect,
    Confirm,
    Cancel
};

struct Icons final
{
    static QIcon icon(IconGlyph glyph);
    static QIcon toggleIcon(IconGlyph glyph);
    static QPixmap pixmap(IconGlyph glyph,
        int size,
        const QColor &color,
        qreal wobblePhase = 0.0,
        qreal devicePixelRatio = 1.0);
};

}
