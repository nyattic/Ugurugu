#include "brush/BrushPreset.hpp"
#include "brush/EraserPreset.hpp"
#include "document/DocumentLimits.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/ColorSwatchRow.hpp"
#include "ui/DrawingToolSettings.hpp"
#include "ui/MainWindow.hpp"

#include <QSettings>

#include <algorithm>
#include <cmath>

namespace wobble
{

using namespace drawing_tool_settings;

void MainWindow::restoreDrawingToolSettings()
{
    QSettings settings;
    const bool hasLegacyStabilization =
        settings.contains(legacyStabilizationKey);
    const qreal legacyStabilization = realSetting(
        settings, QString::fromLatin1(legacyStabilizationKey), 0.0, 0.0, 1.0);
    for (const BrushPreset &preset : BrushPresetCatalog::builtIns())
    {
        m_canvas->setBrushPresetWidth(preset.id,
            realSetting(settings,
                presetWidthKey(preset.id),
                preset.defaultSize,
                minimumRememberedStrokeWidth,
                DocumentLimits::maximumStrokeWidth));
        const QString stabilizationKey = presetStabilizationKey(preset.id);
        m_canvas->setBrushPresetStabilization(preset.id,
            realSetting(
                settings, stabilizationKey, legacyStabilization, 0.0, 1.0));
        if (hasLegacyStabilization && !settings.contains(stabilizationKey))
        {
            settings.setValue(stabilizationKey, legacyStabilization);
        }
    }

    const BrushPreset &defaultPreset = BrushPresetCatalog::defaultPreset();
    const QString storedPresetId =
        settings.value(activePresetKey, defaultPreset.id).toString();
    m_canvas->setBrushPreset(BrushPresetCatalog::find(storedPresetId)
                                 ? storedPresetId
                                 : defaultPreset.id);

    const EraserPreset &defaultEraser = EraserPresetCatalog::defaultPreset();
    const bool hasLegacyEraserWidth = settings.contains(eraserWidthKey);
    const bool hasLegacyEraserStabilization =
        settings.contains(eraserStabilizationKey);
    const qreal legacyEraserWidth = realSetting(settings,
        QString::fromLatin1(eraserWidthKey),
        defaultEraser.defaultSize,
        minimumRememberedStrokeWidth,
        DocumentLimits::maximumStrokeWidth);
    const qreal legacyEraserStabilization = realSetting(settings,
        QString::fromLatin1(eraserStabilizationKey),
        legacyStabilization,
        0.0,
        1.0);
    for (const EraserPreset &preset : EraserPresetCatalog::builtIns())
    {
        const qreal widthFallback =
            preset.id == defaultEraser.id && hasLegacyEraserWidth
                ? legacyEraserWidth
                : preset.defaultSize;
        const QString widthKey = eraserPresetWidthKey(preset.id);
        m_canvas->setEraserPresetWidth(preset.id,
            realSetting(settings,
                widthKey,
                widthFallback,
                minimumRememberedStrokeWidth,
                DocumentLimits::maximumStrokeWidth));
        const QString stabilizationKey =
            eraserPresetStabilizationKey(preset.id);
        m_canvas->setEraserPresetStabilization(preset.id,
            realSetting(settings,
                stabilizationKey,
                legacyEraserStabilization,
                0.0,
                1.0));
        if (hasLegacyEraserWidth && preset.id == defaultEraser.id
            && !settings.contains(widthKey))
        {
            settings.setValue(widthKey, legacyEraserWidth);
        }
        if ((hasLegacyEraserStabilization || hasLegacyStabilization)
            && !settings.contains(stabilizationKey))
        {
            settings.setValue(stabilizationKey, legacyEraserStabilization);
        }
    }
    const QString storedEraserPresetId =
        settings.value(activeEraserPresetKey, defaultEraser.id).toString();
    m_canvas->setEraserPreset(EraserPresetCatalog::find(storedEraserPresetId)
                                  ? storedEraserPresetId
                                  : defaultEraser.id);
    m_canvas->setBrushRoughness(realSetting(settings,
        QString::fromLatin1(roughnessKey),
        1.0,
        DocumentLimits::minimumBrushWobbleScale,
        DocumentLimits::maximumBrushWobbleScale));
    m_canvas->setBrushAntialiasing(
        boolSetting(settings, QString::fromLatin1(antialiasingKey), false));

    QColor storedColor(settings.value(activeColorKey).toString());
    if (!storedColor.isValid())
    {
        const QStringList recentColors =
            settings.value(recentColorsKey).toStringList();
        for (const QString &name : recentColors)
        {
            const QColor recentColor(name);
            if (recentColor.isValid())
            {
                storedColor = recentColor;
                break;
            }
        }
    }
    m_canvas->setBrushColor(
        storedColor.isValid() ? storedColor : QColor(Qt::black));

    const std::optional<CanvasWidget::WandReference> storedReference =
        wandReferenceFromSettingsId(
            settings.value(wandReferenceKey).toString());
    m_canvas->setWandReference(
        storedReference.value_or(CanvasWidget::WandReference::ActiveLayer));

    const std::optional<CanvasWidget::SelectionShape> storedSelectionShape =
        selectionShapeFromSettingsId(
            settings.value(selectionShapeKey).toString());
    m_canvas->setSelectionShape(
        storedSelectionShape.value_or(CanvasWidget::SelectionShape::Freehand));

    const std::optional<CanvasWidget::Tool> storedTool =
        toolFromSettingsId(settings.value(activeToolKey).toString());
    m_canvas->setTool(storedTool.value_or(CanvasWidget::Tool::Brush));

    if (hasLegacyStabilization || hasLegacyEraserWidth
        || hasLegacyEraserStabilization)
    {
        settings.remove(legacyStabilizationKey);
        settings.remove(eraserWidthKey);
        settings.remove(eraserStabilizationKey);
        settings.sync();
    }
}

void MainWindow::connectDrawingToolSettings()
{
    const auto schedule = [this]()
    {
        scheduleDrawingToolSettingsSave();
    };
    connect(m_canvas,
        &CanvasWidget::toolChanged,
        this,
        [schedule](CanvasWidget::Tool)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushColorChanged,
        this,
        [schedule](const QColor &)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushWidthChanged,
        this,
        [schedule](qreal)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::eraserWidthChanged,
        this,
        [schedule](qreal)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushStabilizationChanged,
        this,
        [schedule](qreal)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::eraserStabilizationChanged,
        this,
        [schedule](qreal)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushRoughnessChanged,
        this,
        [schedule](qreal)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushAntialiasingChanged,
        this,
        [schedule](bool)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::brushPresetChanged,
        this,
        [schedule](const QString &)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::eraserPresetChanged,
        this,
        [schedule](const QString &)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::wandReferenceChanged,
        this,
        [schedule](CanvasWidget::WandReference)
        {
            schedule();
        });
    connect(m_canvas,
        &CanvasWidget::selectionShapeChanged,
        this,
        [schedule](CanvasWidget::SelectionShape)
        {
            schedule();
        });
}

void MainWindow::scheduleDrawingToolSettingsSave()
{
    m_drawingToolSettingsSaveTimer.start();
}

void MainWindow::saveDrawingToolSettings()
{
    m_drawingToolSettingsSaveTimer.stop();
    QSettings settings;
    settings.setValue(activeToolKey, toolSettingsId(m_canvas->tool()));
    settings.setValue(activePresetKey, m_canvas->brushPresetId());
    settings.setValue(activeEraserPresetKey, m_canvas->eraserPresetId());
    settings.setValue(
        activeColorKey, m_canvas->brushColor().name(QColor::HexArgb));
    settings.setValue(roughnessKey, m_canvas->brushRoughness());
    settings.setValue(antialiasingKey, m_canvas->brushAntialiasing());
    settings.setValue(
        wandReferenceKey, wandReferenceSettingsId(m_canvas->wandReference()));
    settings.setValue(selectionShapeKey,
        selectionShapeSettingsId(m_canvas->selectionShape()));
    for (const BrushPreset &preset : BrushPresetCatalog::builtIns())
    {
        settings.setValue(
            presetWidthKey(preset.id), m_canvas->brushPresetWidth(preset.id));
        settings.setValue(presetStabilizationKey(preset.id),
            m_canvas->brushPresetStabilization(preset.id));
    }
    for (const EraserPreset &preset : EraserPresetCatalog::builtIns())
    {
        settings.setValue(eraserPresetWidthKey(preset.id),
            m_canvas->eraserPresetWidth(preset.id));
        settings.setValue(eraserPresetStabilizationKey(preset.id),
            m_canvas->eraserPresetStabilization(preset.id));
    }
    settings.sync();
}
}
