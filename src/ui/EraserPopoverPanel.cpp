#include "ui/EraserPopoverPanel.hpp"

#include "ui/BrushSizeRow.hpp"
#include "ui/StrokeStabilizationRow.hpp"

#include <QVBoxLayout>

namespace wobble
{

EraserPopoverPanel::EraserPopoverPanel(CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(new BrushSizeRow(canvas,
        BrushSizeRow::Target::Eraser,
        QStringLiteral("eraserSize"),
        this));
    layout->addWidget(new StrokeStabilizationRow(canvas,
        StrokeStabilizationRow::Target::Eraser,
        QStringLiteral("eraserStabilization"),
        this));
}

}
