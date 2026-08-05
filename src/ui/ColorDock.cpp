#include "ui/ColorDock.hpp"

#include "ui/CanvasWidget.hpp"
#include "ui/ColorPairSwatch.hpp"
#include "ui/ColorWheel.hpp"
#include "ui/PaletteDockTitleBar.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLatin1StringView>
#include <QSettings>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace ugurugu
{

namespace
{

constexpr QLatin1StringView colorWheelShapeKey("dock/colorWheelShape");

}

ColorDock::ColorDock(CanvasWidget *canvas, QWidget *parent)
    : QDockWidget(tr("Color"), parent)
    , m_canvas(canvas)
{
    setObjectName(QStringLiteral("ColorDock"));
    setFeatures(QDockWidget::DockWidgetMovable
                | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetClosable);
    setMinimumWidth(200);
    installCompactPaletteTitleBar(this);

    auto *body = new QWidget(this);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_colorWheel = new ColorWheel(body);
    layout->addWidget(m_colorWheel);

    auto *shapeRow = new QWidget(body);
    auto *shapeLayout = new QHBoxLayout(shapeRow);
    shapeLayout->setContentsMargins(0, 0, 0, 0);
    shapeLayout->setSpacing(4);
    auto *squareButton = new QToolButton(shapeRow);
    squareButton->setObjectName(QStringLiteral("colorWheelSquareButton"));
    squareButton->setText(tr("HSV Square"));
    squareButton->setCheckable(true);
    squareButton->setProperty("categoryTab", true);
    auto *triangleButton = new QToolButton(shapeRow);
    triangleButton->setObjectName(QStringLiteral("colorWheelTriangleButton"));
    triangleButton->setText(tr("HSV Triangle"));
    triangleButton->setCheckable(true);
    triangleButton->setProperty("categoryTab", true);
    auto *shapeGroup = new QButtonGroup(shapeRow);
    shapeGroup->setExclusive(true);
    shapeGroup->addButton(squareButton);
    shapeGroup->addButton(triangleButton);
    shapeLayout->addWidget(squareButton);
    shapeLayout->addWidget(triangleButton);
    shapeLayout->addStretch(1);
    layout->addWidget(shapeRow);

    auto *colorRow = new QWidget(body);
    auto *colorLayout = new QHBoxLayout(colorRow);
    colorLayout->setContentsMargins(0, 2, 0, 0);
    colorLayout->setSpacing(8);
    m_colorSwatch = new ColorPairSwatch(colorRow);
    colorLayout->addWidget(m_colorSwatch);
    auto *colorText = new QVBoxLayout;
    colorText->setContentsMargins(0, 0, 0, 0);
    colorText->setSpacing(1);
    auto *colorLabel = new QLabel(tr("Current color"), colorRow);
    colorLabel->setProperty("fieldLabel", true);
    colorText->addWidget(colorLabel);
    m_colorValue = new QLabel(colorRow);
    m_colorValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    colorText->addWidget(m_colorValue);
    colorLayout->addLayout(colorText);
    colorLayout->addStretch(1);
    layout->addWidget(colorRow);

    layout->addStretch(1);
    setWidget(body);

    connect(m_colorWheel,
        &ColorWheel::colorChanged,
        this,
        [this](const QColor &color)
        {
            if (!m_updatingColor)
            {
                m_canvas->setBrushColor(color);
            }
        });
    connect(m_canvas,
        &CanvasWidget::brushColorChanged,
        this,
        [this](const QColor &color)
        {
            m_updatingColor = true;
            m_colorWheel->setColor(color);
            m_colorSwatch->setCurrentColor(color);
            m_colorValue->setText(color.name(QColor::HexArgb).toUpper());
            m_updatingColor = false;
        });
    connect(m_colorSwatch,
        &ColorPairSwatch::colorSelected,
        m_canvas,
        &CanvasWidget::setBrushColor);

    connect(squareButton,
        &QToolButton::clicked,
        this,
        [this]()
        {
            m_colorWheel->setShape(ColorWheel::Shape::Square);
        });
    connect(triangleButton,
        &QToolButton::clicked,
        this,
        [this]()
        {
            m_colorWheel->setShape(ColorWheel::Shape::Triangle);
        });
    connect(m_colorWheel,
        &ColorWheel::shapeChanged,
        this,
        [squareButton, triangleButton](ColorWheel::Shape shape)
        {
            squareButton->setChecked(shape == ColorWheel::Shape::Square);
            triangleButton->setChecked(shape == ColorWheel::Shape::Triangle);
            QSettings().setValue(colorWheelShapeKey,
                shape == ColorWheel::Shape::Triangle
                    ? QStringLiteral("triangle")
                    : QStringLiteral("square"));
        });

    const bool triangle = QSettings().value(colorWheelShapeKey).toString()
                          == QStringLiteral("triangle");
    m_colorWheel->setShape(
        triangle ? ColorWheel::Shape::Triangle : ColorWheel::Shape::Square);
    squareButton->setChecked(!triangle);
    triangleButton->setChecked(triangle);
    m_colorWheel->setColor(m_canvas->brushColor());
    m_colorSwatch->setCurrentColor(m_canvas->brushColor());
    m_colorValue->setText(
        m_canvas->brushColor().name(QColor::HexArgb).toUpper());
}

}
