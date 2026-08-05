#include "ui/EraserPopoverPanel.hpp"

#include "brush/EraserPreset.hpp"
#include "ui/BrushSizeRow.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/EraserPresetButton.hpp"
#include "ui/ResponsiveGrid.hpp"
#include "ui/StrokeStabilizationRow.hpp"

#include <QButtonGroup>
#include <QVBoxLayout>

namespace ugurugu
{

EraserPopoverPanel::EraserPopoverPanel(CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *presetGrid = new ResponsiveGrid(82, 3, 6, this);
    presetGrid->setObjectName(QStringLiteral("eraserPresetGrid"));
    auto *presetGroup = new QButtonGroup(this);
    presetGroup->setExclusive(true);
    for (const EraserPreset &preset : EraserPresetCatalog::builtIns())
    {
        auto *button = new EraserPresetButton(preset, this);
        button->setChecked(preset.id == canvas->eraserPresetId());
        presetGroup->addButton(button);
        presetGrid->addWidget(button);
        connect(button,
            &QAbstractButton::clicked,
            this,
            [canvas, button]()
            {
                canvas->setEraserPreset(button->presetId());
            });
    }
    connect(canvas,
        &CanvasWidget::eraserPresetChanged,
        this,
        [presetGroup](const QString &presetId)
        {
            for (QAbstractButton *abstractButton : presetGroup->buttons())
            {
                auto *button =
                    static_cast<EraserPresetButton *>(abstractButton);
                if (button->presetId() == presetId)
                {
                    button->setChecked(true);
                    break;
                }
            }
        });
    layout->addWidget(presetGrid);
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
