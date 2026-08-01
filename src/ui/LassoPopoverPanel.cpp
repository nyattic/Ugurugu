#include "ui/LassoPopoverPanel.hpp"

#include "ui/CanvasWidget.hpp"
#include "ui/SelectionShapeButton.hpp"

#include <QButtonGroup>
#include <QLabel>
#include <QVBoxLayout>

#include <array>

namespace wobble
{

LassoPopoverPanel::LassoPopoverPanel(CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *label = new QLabel(tr("SHAPE"), this);
    label->setProperty("fieldLabel", true);
    layout->addWidget(label);

    struct Option final
    {
        CanvasWidget::SelectionShape shape;
        QString title;
        QString description;
        QString objectName;
    };

    const std::array<Option, 3> options{{
        {CanvasWidget::SelectionShape::Freehand,
            tr("Freehand"),
            tr("Draw freely around an area"),
            QStringLiteral("selectionShapeFreehandButton")},
        {CanvasWidget::SelectionShape::Rectangle,
            tr("Rectangle"),
            tr("Drag between opposite corners"),
            QStringLiteral("selectionShapeRectangleButton")},
        {CanvasWidget::SelectionShape::Ellipse,
            tr("Ellipse"),
            tr("Drag to fit an oval area"),
            QStringLiteral("selectionShapeEllipseButton")},
    }};

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    for (const Option &option : options)
    {
        auto *button = new SelectionShapeButton(
            option.shape, option.title, option.description, this);
        button->setObjectName(option.objectName);
        button->setChecked(option.shape == canvas->selectionShape());
        group->addButton(button, static_cast<int>(option.shape));
        layout->addWidget(button);
    }

    connect(group,
        &QButtonGroup::idClicked,
        this,
        [canvas](int id)
        {
            canvas->setSelectionShape(
                static_cast<CanvasWidget::SelectionShape>(id));
        });
    connect(canvas,
        &CanvasWidget::selectionShapeChanged,
        this,
        [group](CanvasWidget::SelectionShape shape)
        {
            if (QAbstractButton *button =
                    group->button(static_cast<int>(shape)))
            {
                button->setChecked(true);
            }
        });
}

}
