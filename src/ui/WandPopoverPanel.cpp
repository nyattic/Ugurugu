#include "ui/WandPopoverPanel.hpp"

#include "ui/CanvasWidget.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

namespace wobble
{

WandPopoverPanel::WandPopoverPanel(CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *label = new QLabel(tr("REFERENCE"), this);
    label->setProperty("fieldLabel", true);
    layout->addWidget(label);

    auto *combo = new QComboBox(this);
    combo->setObjectName(QStringLiteral("wandReferenceCombo"));
    combo->setAccessibleName(tr("Selection reference"));
    combo->addItem(tr("Active layer"),
        static_cast<int>(CanvasWidget::WandReference::ActiveLayer));
    combo->addItem(tr("Reference layers"),
        static_cast<int>(CanvasWidget::WandReference::ReferenceLayers));
    combo->addItem(tr("All visible layers"),
        static_cast<int>(CanvasWidget::WandReference::AllVisibleLayers));
    combo->setCurrentIndex(
        combo->findData(static_cast<int>(canvas->wandReference())));
    label->setBuddy(combo);
    layout->addWidget(combo, 1);

    connect(combo,
        &QComboBox::currentIndexChanged,
        this,
        [canvas, combo](int index)
        {
            canvas->setWandReference(static_cast<CanvasWidget::WandReference>(
                combo->itemData(index).toInt()));
        });
    connect(canvas,
        &CanvasWidget::wandReferenceChanged,
        this,
        [combo](CanvasWidget::WandReference reference)
        {
            QSignalBlocker blocker(combo);
            combo->setCurrentIndex(
                combo->findData(static_cast<int>(reference)));
        });
}

}
