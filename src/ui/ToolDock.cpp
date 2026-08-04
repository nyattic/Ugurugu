#include "ui/ToolDock.hpp"

#include "document/DocumentController.hpp"
#include "ui/BrushPopoverPanel.hpp"
#include "ui/BucketPopoverPanel.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/CollapsibleSection.hpp"
#include "ui/ColorSwatchRow.hpp"
#include "ui/ColorWheel.hpp"
#include "ui/EraserPopoverPanel.hpp"
#include "ui/LassoPopoverPanel.hpp"
#include "ui/WandPopoverPanel.hpp"
#include "ui/WobblePopoverPanel.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QLabel>
#include <QLatin1StringView>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace ugurugu
{

namespace
{

constexpr QLatin1StringView toolSectionKey("dock/toolSectionExpanded");
constexpr QLatin1StringView colorSectionKey("dock/colorSectionExpanded");
constexpr QLatin1StringView wobbleSectionKey("dock/wobbleSectionExpanded");
constexpr QLatin1StringView colorWheelShapeKey("dock/colorWheelShape");
constexpr QLatin1StringView dockWidthKey("dock/toolWidth");

// A tool panel is a column of controls, not a canvas. Letting it take half a
// wide window stretches every preset button into a banner and pushes the
// drawing area into a corner, so it is held to a sane column.
constexpr int minimumDockWidth = 260;
constexpr int maximumDockWidth = 460;
constexpr int defaultDockWidth = 330;

}

ToolDock::ToolDock(
    DocumentController *controller, CanvasWidget *canvas, QWidget *parent)
    : QDockWidget(tr("Tool"), parent)
    , m_controller(controller)
    , m_canvas(canvas)
{
    setObjectName(QStringLiteral("ToolDock"));
    setFeatures(QDockWidget::DockWidgetMovable
                | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetClosable);
    setMinimumWidth(minimumDockWidth);
    setMaximumWidth(maximumDockWidth);
    buildSections();
    restoreState();
}

void ToolDock::buildSections()
{
    auto *host = new QWidget(this);
    auto *hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    // Tool options. One panel per tool, swapped by the canvas's selection so
    // the dock always shows the settings that are actually in effect.
    m_toolSection = new CollapsibleSection(tr("Tool options"), host);
    m_toolPanels = new QStackedWidget(m_toolSection);
    const auto addToolPanel = [this](CanvasTool tool, QWidget *panel)
    {
        m_toolPanelIndexes.insert(
            static_cast<int>(tool), m_toolPanels->addWidget(panel));
    };
    auto *brushPanel = new BrushPopoverPanel(m_canvas);
    brushPanel->setAnimationActive(true);
    addToolPanel(CanvasTool::Brush, brushPanel);
    addToolPanel(CanvasTool::Eraser, new EraserPopoverPanel(m_canvas));
    addToolPanel(CanvasTool::Lasso, new LassoPopoverPanel(m_canvas));
    addToolPanel(CanvasTool::Wand, new WandPopoverPanel(m_canvas));
    addToolPanel(CanvasTool::Bucket, new BucketPopoverPanel(m_canvas));
    // The eyedropper samples the canvas and has nothing to configure.
    auto *eyedropperPanel = new QLabel(
        tr("Click the canvas to pick up its color.\nAlt+click does the same "
           "from the brush, eraser or paint bucket."),
        m_toolPanels);
    eyedropperPanel->setWordWrap(true);
    eyedropperPanel->setProperty("fieldLabel", true);
    addToolPanel(CanvasTool::Eyedropper, eyedropperPanel);
    m_toolSection->setContentWidget(m_toolPanels);
    hostLayout->addWidget(m_toolSection);

    // Colour.
    m_colorSection = new CollapsibleSection(tr("Color"), host);
    auto *colorBody = new QWidget(m_colorSection);
    auto *colorLayout = new QVBoxLayout(colorBody);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    colorLayout->setSpacing(6);
    m_colorWheel = new ColorWheel(colorBody);
    colorLayout->addWidget(m_colorWheel);

    auto *shapeRow = new QWidget(colorBody);
    auto *shapeLayout = new QHBoxLayout(shapeRow);
    shapeLayout->setContentsMargins(0, 0, 0, 0);
    shapeLayout->setSpacing(4);
    auto *squareButton = new QToolButton(shapeRow);
    squareButton->setObjectName(QStringLiteral("colorWheelSquareButton"));
    squareButton->setText(tr("Square"));
    squareButton->setCheckable(true);
    squareButton->setProperty("categoryTab", true);
    auto *triangleButton = new QToolButton(shapeRow);
    triangleButton->setObjectName(QStringLiteral("colorWheelTriangleButton"));
    triangleButton->setText(tr("Triangle"));
    triangleButton->setCheckable(true);
    triangleButton->setProperty("categoryTab", true);
    auto *shapeGroup = new QButtonGroup(shapeRow);
    shapeGroup->setExclusive(true);
    shapeGroup->addButton(squareButton);
    shapeGroup->addButton(triangleButton);
    shapeLayout->addWidget(squareButton);
    shapeLayout->addWidget(triangleButton);
    shapeLayout->addStretch(1);
    colorLayout->addWidget(shapeRow);

    m_swatches = new ColorSwatchRow(colorBody);
    colorLayout->addWidget(m_swatches);
    m_colorSection->setContentWidget(colorBody);
    hostLayout->addWidget(m_colorSection);

    // Wobble, either for the whole drawing or for the active layer alone.
    m_wobbleSection = new CollapsibleSection(tr("Wobble"), host);
    auto *wobbleBody = new QWidget(m_wobbleSection);
    auto *wobbleLayout = new QVBoxLayout(wobbleBody);
    wobbleLayout->setContentsMargins(0, 0, 0, 0);
    wobbleLayout->setSpacing(8);
    m_wobbleScope = new QComboBox(wobbleBody);
    m_wobbleScope->setObjectName(QStringLiteral("wobbleScopeCombo"));
    m_wobbleScope->addItem(tr("Whole drawing"));
    m_wobbleScope->addItem(tr("This layer only"));
    wobbleLayout->addWidget(m_wobbleScope);
    m_wobblePanel = new WobblePopoverPanel(m_controller, wobbleBody);
    wobbleLayout->addWidget(m_wobblePanel);
    m_wobbleSection->setContentWidget(wobbleBody);
    hostLayout->addWidget(m_wobbleSection);
    hostLayout->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(host);
    setWidget(scroll);

    connect(m_canvas, &CanvasWidget::toolChanged, this, &ToolDock::showToolPanel);
    showToolPanel(m_canvas->tool());

    connect(m_colorWheel,
        &ColorWheel::colorChanged,
        this,
        [this](const QColor &color)
        {
            if (m_updatingColor)
            {
                return;
            }
            m_canvas->setBrushColor(color);
        });
    connect(m_canvas,
        &CanvasWidget::brushColorChanged,
        this,
        [this](const QColor &color)
        {
            // Guarded because the wheel is both a source and a sink here, and
            // echoing its own change back would fight the drag in progress.
            m_updatingColor = true;
            m_colorWheel->setColor(color);
            m_swatches->setActiveColor(color);
            m_updatingColor = false;
        });
    connect(m_swatches,
        &ColorSwatchRow::colorSelected,
        m_canvas,
        &CanvasWidget::setBrushColor);
    m_colorWheel->setColor(m_canvas->brushColor());
    m_swatches->setActiveColor(m_canvas->brushColor());

    connect(squareButton,
        &QToolButton::clicked,
        this,
        [this]() { m_colorWheel->setShape(ColorWheel::Shape::Square); });
    connect(triangleButton,
        &QToolButton::clicked,
        this,
        [this]() { m_colorWheel->setShape(ColorWheel::Shape::Triangle); });
    connect(m_colorWheel,
        &ColorWheel::shapeChanged,
        this,
        [squareButton, triangleButton](ColorWheel::Shape shape)
        {
            squareButton->setChecked(shape == ColorWheel::Shape::Square);
            triangleButton->setChecked(shape == ColorWheel::Shape::Triangle);
        });
    const bool triangle =
        QSettings().value(colorWheelShapeKey).toString()
        == QStringLiteral("triangle");
    m_colorWheel->setShape(
        triangle ? ColorWheel::Shape::Triangle : ColorWheel::Shape::Square);
    squareButton->setChecked(!triangle);
    triangleButton->setChecked(triangle);

    const auto applyWobbleScope = [this]()
    {
        m_wobblePanel->setScopeLayer(m_wobbleScope->currentIndex() == 1
                                         ? m_controller->document().activeLayerId
                                         : QUuid());
    };
    connect(m_wobbleScope, &QComboBox::currentIndexChanged, this, applyWobbleScope);
    connect(m_controller,
        &DocumentController::activeLayerChanged,
        this,
        [applyWobbleScope](const QUuid &) { applyWobbleScope(); });
    applyWobbleScope();
}

void ToolDock::showToolPanel(CanvasTool tool)
{
    const auto index = m_toolPanelIndexes.constFind(static_cast<int>(tool));
    if (index != m_toolPanelIndexes.cend())
    {
        m_toolPanels->setCurrentIndex(*index);
    }
}

void ToolDock::revealWobbleSettings()
{
    m_wobbleSection->setExpanded(true);
    if (auto *scroll = qobject_cast<QScrollArea *>(widget()))
    {
        scroll->ensureWidgetVisible(m_wobbleSection);
    }
}

int ToolDock::preferredWidth() const
{
    return std::clamp(QSettings().value(dockWidthKey, defaultDockWidth).toInt(),
        minimumDockWidth,
        maximumDockWidth);
}

void ToolDock::restoreState()
{
    const QSettings settings;
    m_toolSection->setExpanded(settings.value(toolSectionKey, true).toBool());
    m_colorSection->setExpanded(settings.value(colorSectionKey, true).toBool());
    m_wobbleSection->setExpanded(
        settings.value(wobbleSectionKey, false).toBool());
}

void ToolDock::saveState() const
{
    QSettings settings;
    settings.setValue(toolSectionKey, m_toolSection->isExpanded());
    settings.setValue(colorSectionKey, m_colorSection->isExpanded());
    settings.setValue(wobbleSectionKey, m_wobbleSection->isExpanded());
    settings.setValue(colorWheelShapeKey,
        m_colorWheel->shape() == ColorWheel::Shape::Triangle
            ? QStringLiteral("triangle")
            : QStringLiteral("square"));
    if (!isFloating() && width() > 0)
    {
        settings.setValue(dockWidthKey, width());
    }
}

}
