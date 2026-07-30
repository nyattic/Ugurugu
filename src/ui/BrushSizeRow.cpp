#include "ui/BrushSizeRow.hpp"

#include "document/DocumentLimits.hpp"
#include "ui/CanvasWidget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

#include <algorithm>
#include <cmath>

namespace wobble {

BrushSizeRow::BrushSizeRow(
    CanvasWidget *canvas,
    const QString &objectNamePrefix,
    QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *label = new QLabel(tr("SIZE"), this);
    label->setProperty("fieldLabel", true);
    layout->addWidget(label);

    const int minimum =
        static_cast<int>(std::ceil(DocumentLimits::minimumStrokeWidth));

    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setObjectName(objectNamePrefix + QStringLiteral("Slider"));
    slider->setRange(minimum, 128);
    slider->setMinimumWidth(140);
    slider->setToolTip(tr("Brush size"));
    slider->setAccessibleName(tr("Brush size"));
    layout->addWidget(slider, 1);

    auto *spin = new QSpinBox(this);
    spin->setObjectName(objectNamePrefix + QStringLiteral("Spin"));
    spin->setRange(
        minimum,
        static_cast<int>(std::floor(DocumentLimits::maximumStrokeWidth)));
    spin->setSuffix(tr(" px"));
    spin->setAccessibleName(tr("Brush size"));
    label->setBuddy(spin);
    layout->addWidget(spin);

    const int initial = qRound(canvas->brushWidth());
    spin->setValue(initial);
    slider->setValue(
        std::clamp(initial, slider->minimum(), slider->maximum()));

    connect(
        spin,
        &QSpinBox::valueChanged,
        this,
        [canvas, slider](int value) {
            canvas->setBrushWidth(value);
            QSignalBlocker blocker(slider);
            slider->setValue(
                std::clamp(value, slider->minimum(), slider->maximum()));
        });
    connect(
        slider,
        &QSlider::valueChanged,
        spin,
        qOverload<int>(&QSpinBox::setValue));
    connect(
        canvas,
        &CanvasWidget::brushWidthChanged,
        this,
        [spin, slider](qreal width) {
            const int value = qRound(width);
            QSignalBlocker spinBlocker(spin);
            QSignalBlocker sliderBlocker(slider);
            spin->setValue(value);
            slider->setValue(
                std::clamp(value, slider->minimum(), slider->maximum()));
        });
}

}
