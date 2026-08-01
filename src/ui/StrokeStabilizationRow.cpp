#include "ui/StrokeStabilizationRow.hpp"

#include "ui/CanvasWidget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

namespace wobble
{

StrokeStabilizationRow::StrokeStabilizationRow(CanvasWidget *canvas,
    Target target,
    const QString &objectNamePrefix,
    QWidget *parent)
    : QWidget(parent)
{
    const bool controlsEraser = target == Target::Eraser;
    const QString accessibleName = tr("Stroke stabilization");

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *label = new QLabel(tr("STABILIZATION"), this);
    label->setProperty("fieldLabel", true);
    layout->addWidget(label);

    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setObjectName(objectNamePrefix + QStringLiteral("Slider"));
    slider->setRange(0, 100);
    slider->setMinimumWidth(140);
    slider->setToolTip(accessibleName);
    slider->setAccessibleName(accessibleName);
    layout->addWidget(slider, 1);

    auto *spin = new QSpinBox(this);
    spin->setObjectName(objectNamePrefix + QStringLiteral("Spin"));
    spin->setRange(0, 100);
    spin->setSuffix(tr("%"));
    spin->setAccessibleName(accessibleName);
    label->setBuddy(spin);
    layout->addWidget(spin);

    const int initial = qRound((controlsEraser ? canvas->eraserStabilization()
                                               : canvas->brushStabilization())
                               * 100.0);
    slider->setValue(initial);
    spin->setValue(initial);

    connect(spin,
        &QSpinBox::valueChanged,
        this,
        [canvas, controlsEraser, slider](int value)
        {
            if (controlsEraser)
            {
                canvas->setEraserStabilization(value / 100.0);
            }
            else
            {
                canvas->setBrushStabilization(value / 100.0);
            }
            QSignalBlocker blocker(slider);
            slider->setValue(value);
        });
    connect(slider,
        &QSlider::valueChanged,
        spin,
        qOverload<int>(&QSpinBox::setValue));
    const auto syncControls = [spin, slider](qreal strength)
    {
        const int value = qRound(strength * 100.0);
        QSignalBlocker spinBlocker(spin);
        QSignalBlocker sliderBlocker(slider);
        spin->setValue(value);
        slider->setValue(value);
    };
    if (controlsEraser)
    {
        connect(canvas,
            &CanvasWidget::eraserStabilizationChanged,
            this,
            syncControls);
    }
    else
    {
        connect(canvas,
            &CanvasWidget::brushStabilizationChanged,
            this,
            syncControls);
    }
}

}
