#include "ui/BrushPopoverPanel.hpp"

#include "brush/BrushPreset.hpp"
#include "document/DocumentLimits.hpp"
#include "ui/BrushPresetButton.hpp"
#include "ui/BrushSizeRow.hpp"
#include "ui/CanvasWidget.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace wobble
{

namespace
{

const QVector<BrushCategory> &orderedCategories()
{
    static const QVector<BrushCategory> categories{BrushCategory::Pen,
        BrushCategory::Marker,
        BrushCategory::Airbrush,
        BrushCategory::Spray};
    return categories;
}

}

BrushPopoverPanel::BrushPopoverPanel(CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent)
    , m_canvas(canvas)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *tabRow = new QHBoxLayout;
    tabRow->setSpacing(4);
    m_tabGroup = new QButtonGroup(this);
    m_tabGroup->setExclusive(true);
    m_stack = new QStackedWidget(this);

    auto *presetGroup = new QButtonGroup(this);
    presetGroup->setExclusive(true);

    for (int categoryIndex = 0; categoryIndex < orderedCategories().size();
        ++categoryIndex)
    {
        const BrushCategory category = orderedCategories().at(categoryIndex);

        auto *tab = new QToolButton(this);
        tab->setText(BrushPresetCatalog::categoryName(category));
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setProperty("categoryTab", true);
        m_tabGroup->addButton(tab, categoryIndex);
        tabRow->addWidget(tab);

        auto *page = new QWidget(m_stack);
        auto *grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(6);
        int cell = 0;
        for (const BrushPreset &preset : BrushPresetCatalog::builtIns())
        {
            if (preset.category != category)
            {
                continue;
            }
            auto *button = new BrushPresetButton(preset, page);
            presetGroup->addButton(button);
            grid->addWidget(button, cell / 2, cell % 2);
            connect(button,
                &QAbstractButton::clicked,
                this,
                [this, button]()
                {
                    m_canvas->setBrushPreset(button->presetId());
                });
            m_presetButtons.append(button);
            ++cell;
        }
        grid->setRowStretch(grid->rowCount(), 1);
        m_stack->addWidget(page);
    }
    tabRow->addStretch(1);
    connect(m_tabGroup,
        &QButtonGroup::idClicked,
        m_stack,
        &QStackedWidget::setCurrentIndex);

    layout->addLayout(tabRow);
    layout->addWidget(m_stack);
    layout->addWidget(new BrushSizeRow(canvas,
        BrushSizeRow::Target::Brush,
        QStringLiteral("brushSize"),
        this));

    auto *roughnessRow = new QWidget(this);
    auto *roughnessLayout = new QHBoxLayout(roughnessRow);
    roughnessLayout->setContentsMargins(0, 0, 0, 0);
    roughnessLayout->setSpacing(8);

    auto *roughnessLabel = new QLabel(tr("ROUGHNESS"), roughnessRow);
    roughnessLabel->setProperty("fieldLabel", true);
    roughnessLayout->addWidget(roughnessLabel);

    const int maximumPercent =
        static_cast<int>(DocumentLimits::maximumBrushWobbleScale * 100.0);

    auto *roughnessSlider = new QSlider(Qt::Horizontal, roughnessRow);
    roughnessSlider->setObjectName(QStringLiteral("brushRoughnessSlider"));
    roughnessSlider->setRange(0, maximumPercent);
    roughnessSlider->setMinimumWidth(140);
    roughnessSlider->setToolTip(tr("Line roughness"));
    roughnessSlider->setAccessibleName(tr("Line roughness"));
    roughnessLayout->addWidget(roughnessSlider, 1);

    auto *roughnessSpin = new QSpinBox(roughnessRow);
    roughnessSpin->setObjectName(QStringLiteral("brushRoughnessSpin"));
    roughnessSpin->setRange(0, maximumPercent);
    roughnessSpin->setSuffix(tr("%"));
    roughnessSpin->setAccessibleName(tr("Line roughness"));
    roughnessLabel->setBuddy(roughnessSpin);
    roughnessLayout->addWidget(roughnessSpin);

    const int initialRoughness = qRound(canvas->brushRoughness() * 100.0);
    roughnessSpin->setValue(initialRoughness);
    roughnessSlider->setValue(initialRoughness);

    connect(roughnessSpin,
        &QSpinBox::valueChanged,
        this,
        [this, roughnessSlider](int value)
        {
            m_canvas->setBrushRoughness(value / 100.0);
            QSignalBlocker blocker(roughnessSlider);
            roughnessSlider->setValue(value);
        });
    connect(roughnessSlider,
        &QSlider::valueChanged,
        roughnessSpin,
        qOverload<int>(&QSpinBox::setValue));
    connect(canvas,
        &CanvasWidget::brushRoughnessChanged,
        this,
        [roughnessSpin, roughnessSlider](qreal roughness)
        {
            const int value = qRound(roughness * 100.0);
            QSignalBlocker spinBlocker(roughnessSpin);
            QSignalBlocker sliderBlocker(roughnessSlider);
            roughnessSpin->setValue(value);
            roughnessSlider->setValue(value);
        });

    layout->addWidget(roughnessRow);

    auto *antialiasRow = new QWidget(this);
    auto *antialiasLayout = new QHBoxLayout(antialiasRow);
    antialiasLayout->setContentsMargins(0, 0, 0, 0);
    antialiasLayout->setSpacing(8);

    auto *antialiasLabel = new QLabel(tr("ANTI-ALIASING"), antialiasRow);
    antialiasLabel->setProperty("fieldLabel", true);
    antialiasLayout->addWidget(antialiasLabel);
    antialiasLayout->addStretch(1);

    auto *antialiasToggle = new QCheckBox(antialiasRow);
    antialiasToggle->setObjectName(QStringLiteral("brushAntialiasingToggle"));
    antialiasToggle->setToolTip(tr("Smooth stroke edges"));
    antialiasToggle->setAccessibleName(tr("Anti-aliasing"));
    antialiasToggle->setChecked(canvas->brushAntialiasing());
    antialiasLayout->addWidget(antialiasToggle);

    connect(antialiasToggle,
        &QCheckBox::toggled,
        this,
        [this](bool checked)
        {
            m_canvas->setBrushAntialiasing(checked);
        });
    connect(canvas,
        &CanvasWidget::brushAntialiasingChanged,
        this,
        [antialiasToggle](bool antialiasing)
        {
            QSignalBlocker blocker(antialiasToggle);
            antialiasToggle->setChecked(antialiasing);
        });

    layout->addWidget(antialiasRow);

    m_previewTimer.setInterval(120);
    connect(&m_previewTimer,
        &QTimer::timeout,
        this,
        &BrushPopoverPanel::advancePreviews);

    connect(canvas,
        &CanvasWidget::brushPresetChanged,
        this,
        &BrushPopoverPanel::syncToPreset);
    syncToPreset(canvas->brushPresetId());
}

void BrushPopoverPanel::setAnimationActive(bool active)
{
    if (active)
    {
        m_previewTimer.start();
    }
    else
    {
        m_previewTimer.stop();
    }
}

void BrushPopoverPanel::syncToPreset(const QString &presetId)
{
    const BrushPreset *preset = BrushPresetCatalog::find(presetId);
    if (!preset)
    {
        return;
    }
    for (BrushPresetButton *button : m_presetButtons)
    {
        if (button->presetId() == presetId)
        {
            button->setChecked(true);
        }
    }
    const int categoryIndex =
        static_cast<int>(orderedCategories().indexOf(preset->category));
    if (categoryIndex >= 0)
    {
        m_tabGroup->button(categoryIndex)->setChecked(true);
        m_stack->setCurrentIndex(categoryIndex);
    }
}

void BrushPopoverPanel::advancePreviews()
{
    m_previewFrame =
        (m_previewFrame + 1) % BrushPresetButton::previewFrameCount;
    for (BrushPresetButton *button : m_presetButtons)
    {
        button->setPreviewFrame(m_previewFrame);
    }
}

}
