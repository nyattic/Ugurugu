#include "ui/WobblePopoverPanel.hpp"

#include "document/DocumentController.hpp"
#include "document/DocumentLimits.hpp"
#include "ui/WobblePreview.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace wobble
{

namespace
{

struct PercentControls final
{
    QSlider *slider = nullptr;
    QSpinBox *spin = nullptr;
};

PercentControls addPercentControls(QWidget *parent,
    QFormLayout *form,
    const QString &label,
    const QString &objectName)
{
    auto *slider = new QSlider(Qt::Horizontal, parent);
    slider->setObjectName(objectName + QStringLiteral("Slider"));
    slider->setRange(0, 100);
    auto *spin = new QSpinBox(parent);
    spin->setObjectName(objectName + QStringLiteral("Spin"));
    spin->setRange(0, 100);
    spin->setSuffix(QStringLiteral("%"));
    auto *row = new QWidget(parent);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addWidget(slider, 1);
    rowLayout->addWidget(spin);
    form->addRow(label, row);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    return {slider, spin};
}

}

WobblePopoverPanel::WobblePopoverPanel(
    DocumentController *controller, QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(320);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *heading = new QLabel(tr("WOBBLE"), this);
    heading->setProperty("fieldLabel", true);
    layout->addWidget(heading);

    auto *amountRow = new QHBoxLayout;
    auto *preview = new WobblePreview(controller, this);
    preview->setObjectName(QStringLiteral("wobbleSettingsPreview"));
    amountRow->addWidget(preview);
    auto *amountSlider = new QSlider(Qt::Horizontal, this);
    amountSlider->setObjectName(QStringLiteral("wobbleSlider"));
    amountSlider->setRange(qRound(DocumentLimits::minimumWobbleAmount * 10.0),
        qRound(DocumentLimits::maximumWobbleAmount * 10.0));
    amountRow->addWidget(amountSlider, 1);
    auto *amountSpin = new QDoubleSpinBox(this);
    amountSpin->setObjectName(QStringLiteral("wobbleSpin"));
    amountSpin->setRange(DocumentLimits::minimumWobbleAmount,
        DocumentLimits::maximumWobbleAmount);
    amountSpin->setDecimals(1);
    amountSpin->setSingleStep(0.1);
    amountSpin->setSuffix(tr(" px"));
    amountRow->addWidget(amountSpin);
    layout->addLayout(amountRow);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);
    auto *style = new QComboBox(this);
    style->setObjectName(QStringLiteral("motionStyleCombo"));
    style->addItem(tr("Classic"), static_cast<int>(MotionStyle::Classic));
    style->addItem(tr("Smooth"), static_cast<int>(MotionStyle::Smooth));
    style->addItem(tr("Stepped"), static_cast<int>(MotionStyle::Stepped));
    form->addRow(tr("Motion style"), style);

    auto *poseCount = new QSpinBox(this);
    poseCount->setObjectName(QStringLiteral("motionPoseCountSpin"));
    poseCount->setRange(DocumentLimits::minimumMotionPoseCount,
        DocumentLimits::maximumMotionPoseCount);
    form->addRow(tr("Pose count"), poseCount);

    auto *detail = new QSpinBox(this);
    detail->setObjectName(QStringLiteral("motionDetailSpin"));
    detail->setRange(DocumentLimits::minimumMotionDetail,
        DocumentLimits::maximumMotionDetail);
    form->addRow(tr("Detail"), detail);

    const PercentControls linked = addPercentControls(
        this, form, tr("Linked"), QStringLiteral("motionLinked"));
    const PercentControls randomness = addPercentControls(
        this, form, tr("Randomness"), QStringLiteral("motionRandomness"));

    auto *brokenLine = new QCheckBox(tr("Broken line"), this);
    brokenLine->setObjectName(QStringLiteral("brokenLineCheckBox"));
    form->addRow(QString(), brokenLine);
    const PercentControls breakAmount = addPercentControls(
        this, form, tr("Break amount"), QStringLiteral("breakAmount"));
    auto *breakRange = new QDoubleSpinBox(this);
    breakRange->setObjectName(QStringLiteral("breakRangeSpin"));
    breakRange->setRange(
        DocumentLimits::minimumBreakRange, DocumentLimits::maximumBreakRange);
    breakRange->setDecimals(1);
    breakRange->setSuffix(tr(" px"));
    form->addRow(tr("Break range"), breakRange);
    layout->addLayout(form);

    connect(amountSlider,
        &QSlider::valueChanged,
        amountSpin,
        [amountSpin](int value)
        {
            amountSpin->setValue(value / 10.0);
        });
    connect(amountSpin,
        &QDoubleSpinBox::valueChanged,
        amountSlider,
        [amountSlider](double value)
        {
            amountSlider->setValue(qRound(value * 10.0));
        });
    connect(amountSpin,
        &QDoubleSpinBox::valueChanged,
        controller,
        &DocumentController::setWobbleAmount);
    connect(style,
        &QComboBox::currentIndexChanged,
        controller,
        [controller, style](int)
        {
            controller->setMotionStyle(
                static_cast<MotionStyle>(style->currentData().toInt()));
        });
    connect(poseCount,
        &QSpinBox::valueChanged,
        controller,
        &DocumentController::setMotionPoseCount);
    connect(detail,
        &QSpinBox::valueChanged,
        controller,
        &DocumentController::setMotionDetail);
    connect(linked.spin,
        &QSpinBox::valueChanged,
        controller,
        [controller](int value)
        {
            controller->setMotionLinked(value / 100.0);
        });
    connect(randomness.spin,
        &QSpinBox::valueChanged,
        controller,
        [controller](int value)
        {
            controller->setMotionRandomness(value / 100.0);
        });
    connect(brokenLine,
        &QCheckBox::toggled,
        controller,
        &DocumentController::setBrokenLineEnabled);
    connect(breakAmount.spin,
        &QSpinBox::valueChanged,
        controller,
        [controller](int value)
        {
            controller->setBreakAmount(value / 100.0);
        });
    connect(breakRange,
        &QDoubleSpinBox::valueChanged,
        controller,
        &DocumentController::setBreakRange);

    const auto sync = [controller,
                          amountSlider,
                          amountSpin,
                          style,
                          poseCount,
                          detail,
                          linked,
                          randomness,
                          brokenLine,
                          breakAmount,
                          breakRange]()
    {
        const QSignalBlocker amountSliderBlocker(amountSlider);
        const QSignalBlocker amountSpinBlocker(amountSpin);
        const QSignalBlocker styleBlocker(style);
        const QSignalBlocker poseCountBlocker(poseCount);
        const QSignalBlocker detailBlocker(detail);
        const QSignalBlocker linkedSliderBlocker(linked.slider);
        const QSignalBlocker linkedSpinBlocker(linked.spin);
        const QSignalBlocker randomnessSliderBlocker(randomness.slider);
        const QSignalBlocker randomnessSpinBlocker(randomness.spin);
        const QSignalBlocker brokenLineBlocker(brokenLine);
        const QSignalBlocker breakAmountSliderBlocker(breakAmount.slider);
        const QSignalBlocker breakAmountSpinBlocker(breakAmount.spin);
        const QSignalBlocker breakRangeBlocker(breakRange);
        const Document &document = controller->document();
        amountSlider->setValue(qRound(document.wobbleAmount * 10.0));
        amountSpin->setValue(document.wobbleAmount);
        style->setCurrentIndex(
            style->findData(static_cast<int>(document.motion.style)));
        poseCount->setMaximum(document.motion.style == MotionStyle::Classic
                                  ? DocumentLimits::maximumMotionPoseCount
                                  : document.animationFrames);
        poseCount->setValue(document.motion.poseCount);
        poseCount->setEnabled(document.motion.style != MotionStyle::Classic);
        detail->setValue(document.motion.detail);
        linked.spin->setValue(qRound(document.motion.linked * 100.0));
        randomness.spin->setValue(qRound(document.motion.randomness * 100.0));
        brokenLine->setChecked(document.motion.brokenLine);
        breakAmount.spin->setValue(qRound(document.motion.breakAmount * 100.0));
        breakRange->setValue(document.motion.breakRange);
        breakAmount.slider->setEnabled(document.motion.brokenLine);
        breakAmount.spin->setEnabled(document.motion.brokenLine);
        breakRange->setEnabled(document.motion.brokenLine);
    };
    connect(controller,
        &DocumentController::documentChanged,
        this,
        [sync]()
        {
            sync();
        });
    sync();
}

}
