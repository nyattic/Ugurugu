#include "brush/BrushPreset.hpp"
#include "brush/EraserPreset.hpp"
#include "document/DocumentLimits.hpp"
#include "document/SelectionOperation.hpp"
#include "document/SelectionVisibility.hpp"
#include "io/DocumentSerializer.hpp"
#include "render/PreviewRenderPolicy.hpp"
#include "render/RenderEngine.hpp"
#include "ui/BrushPopoverPanel.hpp"
#include "ui/BrushPresetButton.hpp"
#include "ui/BrushSizeRow.hpp"
#include "ui/CanvasSizeDialog.hpp"
#include "ui/CanvasWidget.hpp"
#include "ui/ColorSwatchRow.hpp"
#include "ui/EraserPopoverPanel.hpp"
#include "ui/EraserPresetButton.hpp"
#include "ui/FrameScrubber.hpp"
#include "ui/ImageSizeDialog.hpp"
#include "ui/LassoPopoverPanel.hpp"
#include "ui/LayerDock.hpp"
#include "ui/LayerThumbnailRenderer.hpp"
#include "ui/MainWindow.hpp"
#include "ui/SelectionActionBar.hpp"
#include "ui/SelectionShapeButton.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/ShortcutBinding.hpp"
#include "ui/StrokePropertiesDialog.hpp"
#include "ui/TimelineBar.hpp"
#include "ui/WandPopoverPanel.hpp"
#include "ui/WandReferenceButton.hpp"
#include "ui/WobblePreview.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QHideEvent>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPointingDevice>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QtTest>

#include <algorithm>
#include <limits>
#include <utility>

namespace wobble
{

class SettingValueGuard final
{
public:
    explicit SettingValueGuard(QString key)
        : m_key(std::move(key))
        , m_existed(m_settings.contains(m_key))
        , m_value(m_settings.value(m_key))
    {
    }

    ~SettingValueGuard()
    {
        if (m_existed)
        {
            m_settings.setValue(m_key, m_value);
        }
        else
        {
            m_settings.remove(m_key);
        }
    }

private:
    QSettings m_settings;
    QString m_key;
    bool m_existed = false;
    QVariant m_value;
};

class EnvironmentVariableGuard final
{
public:
    explicit EnvironmentVariableGuard(QByteArray name)
        : m_name(std::move(name))
        , m_existed(qEnvironmentVariableIsSet(m_name.constData()))
        , m_value(qgetenv(m_name.constData()))
    {
    }

    ~EnvironmentVariableGuard()
    {
        if (m_existed)
        {
            qputenv(m_name.constData(), m_value);
        }
        else
        {
            qunsetenv(m_name.constData());
        }
    }

private:
    QByteArray m_name;
    bool m_existed = false;
    QByteArray m_value;
};

class ApplicationVersionGuard final
{
public:
    ApplicationVersionGuard()
        : m_version(QApplication::applicationVersion())
    {
    }

    ~ApplicationVersionGuard()
    {
        QApplication::setApplicationVersion(m_version);
    }

private:
    QString m_version;
};

class UiTests final : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QSettings settings;
        settings.remove(QStringLiteral("drawingTools"));
        settings.remove(QStringLiteral("brush/recentColors"));
        settings.remove(QStringLiteral("canvas/strokeStabilization"));
        settings.sync();
    }

    void cleanup()
    {
        QSettings settings;
        settings.remove(QStringLiteral("drawingTools"));
        settings.remove(QStringLiteral("brush/recentColors"));
        settings.remove(QStringLiteral("canvas/strokeStabilization"));
        settings.sync();
    }

    void launchesAndEdits()
    {
        MainWindow window;
        window.resize(1100, 720);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        LayerDock *layerDock = window.findChild<LayerDock *>();
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QVERIFY(canvas);
        QVERIFY(layerDock);
        QVERIFY(undoAction);
        QVERIFY(!undoAction->isEnabled());
        const QList<BrushPresetButton *> presetButtons =
            window.findChildren<BrushPresetButton *>();
        QSpinBox *brushSizeSpin =
            window.findChild<QSpinBox *>(QStringLiteral("brushSizeSpin"));
        QVERIFY(brushSizeSpin);
        QCOMPARE(presetButtons.size(), BrushPresetCatalog::builtIns().size());
        BrushPresetButton *softAirbrushButton = nullptr;
        for (BrushPresetButton *button : presetButtons)
        {
            if (button->presetId() == QStringLiteral("soft-airbrush"))
            {
                softAirbrushButton = button;
            }
        }
        QVERIFY(softAirbrushButton);
        softAirbrushButton->click();
        QCOMPARE(canvas->brushPresetId(), QStringLiteral("soft-airbrush"));
        QVERIFY(softAirbrushButton->isChecked());
        QCOMPARE(brushSizeSpin->value(),
            qRound(BrushPresetCatalog::find(QStringLiteral("soft-airbrush"))
                    ->defaultSize));

        QSpinBox *currentFrameSpin =
            window.findChild<QSpinBox *>(QStringLiteral("currentFrameSpin"));
        QVERIFY(currentFrameSpin);
        QCOMPARE(currentFrameSpin->maximum(), 30);
        currentFrameSpin->setFocus(Qt::OtherFocusReason);
        QTest::mouseClick(currentFrameSpin,
            Qt::LeftButton,
            Qt::NoModifier,
            currentFrameSpin->rect().center());
        QTRY_VERIFY(!canvas->isAnimating());
        const int targetFrameValue = currentFrameSpin->value() == 1 ? 2 : 1;
        currentFrameSpin->setValue(targetFrameValue);
        QCOMPARE(canvas->currentFrame(), targetFrameValue - 1);

        const QPoint start = canvas->rect().center() - QPoint(50, 20);
        const QPoint end = canvas->rect().center() + QPoint(50, 20);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas, end, 10);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
        QTRY_VERIFY(undoAction->isEnabled());

        const QString screenshotPath =
            qEnvironmentVariable("WOBBLEPAINT_TEST_SCREENSHOT");
        if (!screenshotPath.isEmpty())
        {
            QVERIFY(window.grab().save(screenshotPath, "PNG"));
            QVERIFY(QFileInfo(screenshotPath).size() > 0);
        }

        const QString brushPanelScreenshotPath =
            qEnvironmentVariable("WOBBLEPAINT_BRUSH_PANEL_SCREENSHOT");
        if (!brushPanelScreenshotPath.isEmpty())
        {
            BrushPopoverPanel *brushPanel =
                window.findChild<BrushPopoverPanel *>();
            QVERIFY(brushPanel);
            QWidget *popover = brushPanel->window();
            popover->adjustSize();
            QVERIFY(popover->grab().save(brushPanelScreenshotPath, "PNG"));
            QVERIFY(QFileInfo(brushPanelScreenshotPath).size() > 0);
        }

        undoAction->trigger();
        QTRY_VERIFY(!undoAction->isEnabled());

        QListWidget *layerList = layerDock->findChild<QListWidget *>();
        QVERIFY(layerList);
        QCOMPARE(layerList->count(), 1);

        QToolButton *addButton = layerDock->findChild<QToolButton *>(
            QStringLiteral("layerAddButton"));
        QVERIFY(addButton);
        QTest::mouseClick(addButton, Qt::LeftButton);
        QTRY_COMPARE(layerList->count(), 2);
        QTRY_VERIFY(undoAction->isEnabled());

        undoAction->trigger();
        QTRY_COMPARE(layerList->count(), 1);

        QAction *settingsAction =
            window.findChild<QAction *>(QStringLiteral("settingsAction"));
        QAction *checkForUpdatesAction = window.findChild<QAction *>(
            QStringLiteral("checkForUpdatesAction"));
        QToolButton *settingsButton =
            window.findChild<QToolButton *>(QStringLiteral("settingsButton"));
        QVERIFY(settingsAction);
        QVERIFY(checkForUpdatesAction);
        QVERIFY(settingsButton);
        QCOMPARE(settingsButton->defaultAction(), settingsAction);
        QVERIFY(
            window.windowTitle().contains(QStringLiteral("WagleWaglePaint")));
    }

    void exposesBrushPresetNameToAccessibility()
    {
        const BrushPreset &preset = BrushPresetCatalog::defaultPreset();
        BrushPresetButton button(preset);
        QCOMPARE(
            button.accessibleName(), BrushPresetCatalog::displayName(preset));
    }

    void exposesEraserPresetNameToAccessibility()
    {
        const EraserPreset &preset = EraserPresetCatalog::defaultPreset();
        EraserPresetButton button(preset);
        QCOMPARE(
            button.accessibleName(), EraserPresetCatalog::displayName(preset));
    }

    void switchesEraserPresetFromCards()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        EraserPopoverPanel panel(&canvas);

        const QList<EraserPresetButton *> buttons =
            panel.findChildren<EraserPresetButton *>();
        QCOMPARE(buttons.size(), EraserPresetCatalog::builtIns().size());
        EraserPresetButton *softButton = nullptr;
        for (EraserPresetButton *button : buttons)
        {
            if (button->presetId() == QStringLiteral("soft-eraser"))
            {
                softButton = button;
                break;
            }
        }
        QVERIFY(softButton);
        softButton->click();
        QCOMPARE(canvas.eraserPresetId(), QStringLiteral("soft-eraser"));
        QVERIFY(softButton->isChecked());
        QCOMPARE(canvas.eraserWidth(),
            EraserPresetCatalog::find(QStringLiteral("soft-eraser"))
                ->defaultSize);
    }

    void switchesWandReferenceFromCards()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        WandPopoverPanel panel(&canvas);

        WandReferenceButton *activeButton =
            panel.findChild<WandReferenceButton *>(
                QStringLiteral("wandReferenceActiveButton"));
        WandReferenceButton *markedButton =
            panel.findChild<WandReferenceButton *>(
                QStringLiteral("wandReferenceMarkedButton"));
        WandReferenceButton *visibleButton =
            panel.findChild<WandReferenceButton *>(
                QStringLiteral("wandReferenceVisibleButton"));
        QVERIFY(activeButton);
        QVERIFY(markedButton);
        QVERIFY(visibleButton);
        QCOMPARE(panel.findChildren<WandReferenceButton *>().size(), 3);
        QVERIFY(activeButton->isChecked());

        markedButton->click();
        QCOMPARE(canvas.wandReference(),
            CanvasWidget::WandReference::ReferenceLayers);
        QVERIFY(markedButton->isChecked());

        canvas.setWandReference(CanvasWidget::WandReference::AllVisibleLayers);
        QVERIFY(visibleButton->isChecked());
    }

    void switchesSelectionShapeFromCards()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        LassoPopoverPanel panel(&canvas);

        SelectionShapeButton *freehandButton =
            panel.findChild<SelectionShapeButton *>(
                QStringLiteral("selectionShapeFreehandButton"));
        SelectionShapeButton *rectangleButton =
            panel.findChild<SelectionShapeButton *>(
                QStringLiteral("selectionShapeRectangleButton"));
        SelectionShapeButton *ellipseButton =
            panel.findChild<SelectionShapeButton *>(
                QStringLiteral("selectionShapeEllipseButton"));
        QVERIFY(freehandButton);
        QVERIFY(rectangleButton);
        QVERIFY(ellipseButton);
        QCOMPARE(panel.findChildren<SelectionShapeButton *>().size(), 3);
        QVERIFY(freehandButton->isChecked());

        rectangleButton->click();
        QCOMPARE(
            canvas.selectionShape(), CanvasWidget::SelectionShape::Rectangle);
        QVERIFY(rectangleButton->isChecked());

        canvas.setSelectionShape(CanvasWidget::SelectionShape::Ellipse);
        QVERIFY(ellipseButton->isChecked());
    }

    void tabsToFrameScrubberAndExposesCurrentFrame()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        FrameScrubber scrubber(&controller, &canvas);

        QCOMPARE(scrubber.focusPolicy(), Qt::StrongFocus);
        QVERIFY(scrubber.focusPolicy() & Qt::TabFocus);
        QCOMPARE(
            scrubber.accessibleDescription(), QStringLiteral("Frame 1 of 30"));
        QTest::keyClick(&scrubber, Qt::Key_Right);
        QCOMPARE(canvas.currentFrame(), 1);
        QCOMPARE(
            scrubber.accessibleDescription(), QStringLiteral("Frame 2 of 30"));
    }

    void togglesLayerVisibilityFromKeyboard()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        const QUuid layerId = controller.document().activeLayerId;
        LayerDock dock(&controller);
        QListWidget *list = dock.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->data(Qt::AccessibleDescriptionRole).toString(),
            QStringLiteral("Layer is visible"));

        QTest::keyClick(list, Qt::Key_Space);
        QTRY_VERIFY(!controller.document().layer(layerId)->visible);
        QCOMPARE(list->item(0)->data(Qt::AccessibleDescriptionRole).toString(),
            QStringLiteral("Layer is hidden"));

        QTest::keyClick(list, Qt::Key_Space);
        QTRY_VERIFY(controller.document().layer(layerId)->visible);
    }

    void stopsWobblePreviewWhileTimelineIsDisabled()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        TimelineBar timeline(&controller, &canvas);
        WobblePreview *preview = timeline.findChild<WobblePreview *>();
        QVERIFY(preview);
        QShowEvent showEvent;
        QApplication::sendEvent(preview, &showEvent);
        QVERIFY(preview->isAnimationActive());

        timeline.setEnabled(false);
        QVERIFY(!preview->isAnimationActive());
        timeline.setEnabled(true);
        QVERIFY(preview->isAnimationActive());
        QHideEvent hideEvent;
        QApplication::sendEvent(preview, &hideEvent);
        QVERIFY(!preview->isAnimationActive());
    }

    void debouncesRecentColorPersistence()
    {
        const QString key = QStringLiteral("brush/recentColors");
        QSettings().remove(key);
        ColorSwatchRow row;
        const QColor first(20, 40, 60, 80);
        const QColor second(30, 50, 70, 90);
        const QColor finalColor(40, 60, 80, 100);

        row.setActiveColor(first);
        row.setActiveColor(second);
        QVERIFY(!QSettings().contains(key));
        QTest::qWait(100);
        row.setActiveColor(finalColor);
        QTest::qWait(75);
        QVERIFY(!QSettings().contains(key));
        QTRY_COMPARE(QSettings().value(key).toStringList().value(0),
            finalColor.name(QColor::HexArgb));
    }

    void changesLayerBlendModeFromDock()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        LayerDock dock(&controller);
        QComboBox *blendModeCombo =
            dock.findChild<QComboBox *>(QStringLiteral("layerBlendModeCombo"));
        QVERIFY(blendModeCombo);
        QCOMPARE(blendModeCombo->count(), 4);
        QCOMPARE(blendModeCombo->currentData().toInt(),
            static_cast<int>(LayerBlendMode::Normal));

        blendModeCombo->setCurrentIndex(blendModeCombo->findData(
            static_cast<int>(LayerBlendMode::Overlay)));
        QCOMPARE(controller.document().layers.first().blendMode,
            LayerBlendMode::Overlay);
        QCOMPARE(controller.undoStack()->count(), 1);

        controller.undoStack()->undo();
        QCOMPARE(controller.document().layers.first().blendMode,
            LayerBlendMode::Normal);
        QCOMPARE(blendModeCombo->currentData().toInt(),
            static_cast<int>(LayerBlendMode::Normal));
        controller.undoStack()->redo();
        QCOMPARE(blendModeCombo->currentData().toInt(),
            static_cast<int>(LayerBlendMode::Overlay));
    }

    void restoresRejectedLayerRenameInDock()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        const QUuid layerId = controller.document().activeLayerId;
        const QString originalName = controller.document().layer(layerId)->name;
        LayerDock dock(&controller);
        QListWidget *list = dock.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 1);

        list->item(0)->setText(QString(
            DocumentLimits::maximumLayerNameLength + 1, QLatin1Char('x')));

        QCOMPARE(controller.document().layer(layerId)->name, originalName);
        QCOMPARE(list->item(0)->text(), originalName);
        QCOMPARE(controller.undoStack()->count(), 0);
    }

    void limitsLayerNameEditorLength()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        LayerDock dock(&controller);
        QListWidget *list = dock.findChild<QListWidget *>();
        QVERIFY(list);
        QCOMPARE(list->count(), 1);

        list->editItem(list->item(0));
        QLineEdit *editor = list->findChild<QLineEdit *>();
        QVERIFY(editor);
        QCOMPARE(editor->maxLength(), DocumentLimits::maximumLayerNameLength);
    }

    void reportsStrokeCommitFailureFromCanvas()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        const QUuid layerId = controller.document().activeLayerId;
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();
        QSignalSpy messages(&canvas, &CanvasWidget::interactionMessage);
        const QPoint center = canvas.rect().center();

        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, center);
        controller.removeLayer(layerId);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(20, 0));

        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first().first().toString(),
            QStringLiteral("The stroke could not be added because its layer is "
                           "no longer available."));
        QVERIFY(controller.document().layers.isEmpty());
    }

    void managesLayerGroupsAndClippingFromDock()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        const QUuid layerId = controller.document().activeLayerId;
        LayerDock dock(&controller);
        QToolButton *groupButton = dock.findChild<QToolButton *>(
            QStringLiteral("layerAddGroupButton"));
        QComboBox *parentCombo = dock.findChild<QComboBox *>(
            QStringLiteral("layerParentGroupCombo"));
        QCheckBox *clipCheck =
            dock.findChild<QCheckBox *>(QStringLiteral("layerClipCheck"));
        QCheckBox *referenceCheck =
            dock.findChild<QCheckBox *>(QStringLiteral("layerReferenceCheck"));
        QListWidget *list = dock.findChild<QListWidget *>();
        QVERIFY(groupButton);
        QVERIFY(parentCombo);
        QVERIFY(clipCheck);
        QVERIFY(referenceCheck);
        QVERIFY(list);

        groupButton->click();
        QTRY_COMPARE(list->count(), 2);
        const Layer *layer = controller.document().layer(layerId);
        QVERIFY(layer);
        QVERIFY(!layer->parentGroupId.isNull());
        QCOMPARE(parentCombo->currentData().toUuid(), layer->parentGroupId);

        clipCheck->click();
        QVERIFY(controller.document().layer(layerId)->clipToLayerBelow);
        controller.undoStack()->undo();
        QVERIFY(!controller.document().layer(layerId)->clipToLayerBelow);

        referenceCheck->click();
        QVERIFY(controller.document().layer(layerId)->reference);
        controller.undoStack()->undo();
        QVERIFY(!controller.document().layer(layerId)->reference);
    }

    void editsStrokePropertyDialogValues()
    {
        StrokePropertiesDialog::Values values;
        values.colorSupported = true;
        values.widthSupported = true;
        values.roughnessSupported = true;
        values.color = QColor(10, 20, 30);
        values.width = 8.0;
        values.roughness = 0.75;
        StrokePropertiesDialog dialog(values);

        QDoubleSpinBox *width = dialog.findChild<QDoubleSpinBox *>(
            QStringLiteral("strokeWidthSpin"));
        QDoubleSpinBox *roughness = dialog.findChild<QDoubleSpinBox *>(
            QStringLiteral("strokeRoughnessSpin"));
        QVERIFY(width);
        QVERIFY(roughness);
        width->setValue(18.0);
        roughness->setValue(140.0);
        QVERIFY(dialog.width().has_value());
        QVERIFY(dialog.roughness().has_value());
        QVERIFY(dialog.color().has_value());
        QCOMPARE(*dialog.width(), 18.0);
        QCOMPARE(*dialog.roughness(), 1.4);
        QCOMPARE(*dialog.color(), *values.color);
    }

    void editsAndRestoresShortcuts()
    {
        const QString brushKey = QStringLiteral("shortcuts/brushAction");
        const QString eraserKey = QStringLiteral("shortcuts/eraserAction");
        const QString folderKey = QStringLiteral("files/defaultSaveFolder");
        const QString languageKey = QStringLiteral("appearance/language");
        SettingValueGuard brushGuard(brushKey);
        SettingValueGuard eraserGuard(eraserKey);
        SettingValueGuard folderGuard(folderKey);
        SettingValueGuard languageGuard(languageKey);
        QTemporaryDir saveFolder;
        QVERIFY(saveFolder.isValid());
        QSettings settings;
        settings.remove(brushKey);
        settings.remove(eraserKey);
        settings.setValue(folderKey, saveFolder.path());
        settings.remove(languageKey);
        settings.sync();

        QAction brushAction(QStringLiteral("Brush"));
        brushAction.setObjectName(QStringLiteral("brushAction"));
        ShortcutBinding::initialize(
            &brushAction, QKeySequence(QStringLiteral("B")));

        QAction eraserAction(QStringLiteral("Eraser"));
        eraserAction.setObjectName(QStringLiteral("eraserAction"));
        ShortcutBinding::initialize(
            &eraserAction, QKeySequence(QStringLiteral("E")));

        SettingsDialog dialog(nullptr, {&brushAction, &eraserAction});
        QLineEdit *folderEdit = dialog.findChild<QLineEdit *>(
            QStringLiteral("defaultSaveFolderEdit"));
        QKeySequenceEdit *brushEditor = dialog.findChild<QKeySequenceEdit *>(
            QStringLiteral("brushActionShortcutEdit"));
        QComboBox *languageCombo =
            dialog.findChild<QComboBox *>(QStringLiteral("languageCombo"));
        QVERIFY(folderEdit);
        QVERIFY(brushEditor);
        QVERIFY(languageCombo);
        QCOMPARE(folderEdit->text(), saveFolder.path());
        QCOMPARE(SettingsDialog::defaultSaveFolder(), saveFolder.path());
        QCOMPARE(SettingsDialog::uiLanguage(), QStringLiteral("system"));

        languageCombo->setCurrentIndex(
            languageCombo->findData(QStringLiteral("ko")));
        QCOMPARE(SettingsDialog::uiLanguage(), QStringLiteral("ko"));

        brushEditor->setKeySequence(QKeySequence(QStringLiteral("V")));
        QTRY_COMPARE(brushAction.shortcut(), QKeySequence(QStringLiteral("V")));
        QCOMPARE(settings.value(brushKey).toString(), QStringLiteral("V"));

        brushEditor->setKeySequence(QKeySequence(QStringLiteral("E")));
        QTRY_COMPARE(
            brushEditor->keySequence(), QKeySequence(QStringLiteral("V")));
        QCOMPARE(brushAction.shortcut(), QKeySequence(QStringLiteral("V")));

        QDialogButtonBox *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QPushButton *restoreButton =
            buttons->button(QDialogButtonBox::RestoreDefaults);
        QVERIFY(restoreButton);
        QTest::mouseClick(restoreButton, Qt::LeftButton);
        QCOMPARE(brushAction.shortcut(), QKeySequence(QStringLiteral("B")));
        QVERIFY(!settings.contains(brushKey));
        QVERIFY(!settings.contains(folderKey));
        QVERIFY(!settings.contains(languageKey));
        QCOMPARE(folderEdit->text(), SettingsDialog::defaultSaveFolder());
        QCOMPARE(SettingsDialog::uiLanguage(), QStringLiteral("system"));
    }

    void preservesShortcutAliasesAcrossEditingAndRestore()
    {
        const QString zoomKey = QStringLiteral("shortcuts/zoomAction");
        const QString otherKey = QStringLiteral("shortcuts/otherAction");
        SettingValueGuard zoomGuard(zoomKey);
        SettingValueGuard otherGuard(otherKey);
        QSettings settings;
        settings.remove(zoomKey);
        settings.remove(otherKey);

        QAction zoomAction(QStringLiteral("Zoom in"));
        zoomAction.setObjectName(QStringLiteral("zoomAction"));
        const QKeySequence zoomDefault(QStringLiteral("Ctrl++"));
        const QKeySequence zoomAlias(QStringLiteral("Ctrl+="));
        ShortcutBinding::initialize(&zoomAction, zoomDefault, {zoomAlias});

        QAction otherAction(QStringLiteral("Other"));
        otherAction.setObjectName(QStringLiteral("otherAction"));
        const QKeySequence otherDefault(QStringLiteral("O"));
        ShortcutBinding::initialize(&otherAction, otherDefault);

        SettingsDialog dialog(nullptr, {&zoomAction, &otherAction});
        QKeySequenceEdit *zoomEditor = dialog.findChild<QKeySequenceEdit *>(
            QStringLiteral("zoomActionShortcutEdit"));
        QKeySequenceEdit *otherEditor = dialog.findChild<QKeySequenceEdit *>(
            QStringLiteral("otherActionShortcutEdit"));
        QVERIFY(zoomEditor);
        QVERIFY(otherEditor);
        QCOMPARE(zoomAction.shortcuts(),
            QList<QKeySequence>({zoomDefault, zoomAlias}));

        otherEditor->setKeySequence(zoomAlias);
        QTRY_COMPARE(otherEditor->keySequence(), otherDefault);
        QCOMPARE(otherAction.shortcuts(), QList<QKeySequence>({otherDefault}));

        const QKeySequence customPrimary(QStringLiteral("Ctrl+I"));
        zoomEditor->setKeySequence(customPrimary);
        QTRY_COMPARE(ShortcutBinding::primary(&zoomAction), customPrimary);
        QCOMPARE(zoomAction.shortcuts(),
            QList<QKeySequence>({customPrimary, zoomAlias}));
        QCOMPARE(settings.value(zoomKey).toString(),
            customPrimary.toString(QKeySequence::PortableText));

        QDialogButtonBox *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QPushButton *restoreButton =
            buttons->button(QDialogButtonBox::RestoreDefaults);
        QVERIFY(restoreButton);
        QTest::mouseClick(restoreButton, Qt::LeftButton);
        QCOMPARE(ShortcutBinding::primary(&zoomAction), zoomDefault);
        QCOMPARE(zoomAction.shortcuts(),
            QList<QKeySequence>({zoomDefault, zoomAlias}));
        QVERIFY(!settings.contains(zoomKey));
    }

    void preservesPersistedPrimaryShortcutsOverNewAliases()
    {
        const QString zoomKey = QStringLiteral("shortcuts/zoomInAction");
        const QString otherKey = QStringLiteral("shortcuts/actualSizeAction");
        SettingValueGuard zoomGuard(zoomKey);
        SettingValueGuard otherGuard(otherKey);
        const QKeySequence zoomAlias(QStringLiteral("Ctrl+="));
        const QKeySequence zoomPrimary(QStringLiteral("Ctrl+I"));
        QSettings settings;
        settings.setValue(
            zoomKey, zoomPrimary.toString(QKeySequence::PortableText));
        settings.setValue(
            otherKey, zoomAlias.toString(QKeySequence::PortableText));
        settings.sync();

        MainWindow window;
        QAction *zoomAction =
            window.findChild<QAction *>(QStringLiteral("zoomInAction"));
        QAction *otherAction =
            window.findChild<QAction *>(QStringLiteral("actualSizeAction"));
        QVERIFY(zoomAction);
        QVERIFY(otherAction);
        QCOMPARE(ShortcutBinding::primary(zoomAction), zoomPrimary);
        QCOMPARE(ShortcutBinding::primary(otherAction), zoomAlias);
        QCOMPARE(zoomAction->shortcuts(), QList<QKeySequence>({zoomPrimary}));
        QCOMPARE(otherAction->shortcuts(), QList<QKeySequence>({zoomAlias}));

        SettingsDialog dialog(nullptr, {zoomAction, otherAction});

        QKeySequenceEdit *otherEditor = dialog.findChild<QKeySequenceEdit *>(
            QStringLiteral("actualSizeActionShortcutEdit"));
        QVERIFY(otherEditor);
        const QKeySequence replacement(QStringLiteral("Ctrl+K"));
        otherEditor->setKeySequence(replacement);
        QTRY_COMPARE(ShortcutBinding::primary(otherAction), replacement);
        QCOMPARE(zoomAction->shortcuts(),
            QList<QKeySequence>({zoomPrimary, zoomAlias}));
    }

    void showsApplicationVersionInAboutTab()
    {
        const ApplicationVersionGuard versionGuard;
        QApplication::setApplicationVersion(QStringLiteral("9.8.7-test"));

        SettingsDialog dialog;
        QTabWidget *tabs = dialog.findChild<QTabWidget *>();
        QLabel *versionLabel = dialog.findChild<QLabel *>(
            QStringLiteral("applicationVersionLabel"));
        QWidget *aboutTab =
            dialog.findChild<QWidget *>(QStringLiteral("aboutTab"));
        QVERIFY(tabs);
        QVERIFY(versionLabel);
        QVERIFY(aboutTab);
        QCOMPARE(versionLabel->text(), QStringLiteral("Version 9.8.7-test"));
        QCOMPARE(
            tabs->tabText(tabs->indexOf(aboutTab)), QStringLiteral("About"));
    }

    void migratesGlobalStrokeStabilizationToDrawingTools()
    {
        const QString legacyKey = QStringLiteral("canvas/strokeStabilization");
        QSettings settings;
        settings.setValue(legacyKey, 0.65);
        settings.sync();

        MainWindow window;
        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QVERIFY(canvas);
        for (const BrushPreset &preset : BrushPresetCatalog::builtIns())
        {
            QCOMPARE(canvas->brushPresetStabilization(preset.id), 0.65);
            QVERIFY(settings.contains(
                QStringLiteral("drawingTools/brush/presetStabilizations/%1")
                    .arg(preset.id)));
        }
        for (const EraserPreset &preset : EraserPresetCatalog::builtIns())
        {
            QCOMPARE(canvas->eraserPresetStabilization(preset.id), 0.65);
            QVERIFY(settings.contains(
                QStringLiteral("drawingTools/eraser/presetStabilizations/%1")
                    .arg(preset.id)));
        }
        QVERIFY(!settings.contains(
            QStringLiteral("drawingTools/eraser/stabilization")));
        QVERIFY(!settings.contains(legacyKey));
    }

    void configuresCanvasSizeDialog()
    {
        CanvasSizeDialog dialog(QSize(640, 480));
        QCheckBox *relativeCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("canvasRelativeSizeCheck"));
        QSpinBox *widthSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("canvasWidthSpin"));
        QSpinBox *heightSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("canvasHeightSpin"));
        QSpinBox *offsetXSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("canvasOffsetXSpin"));
        QSpinBox *offsetYSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("canvasOffsetYSpin"));
        QToolButton *topLeft = dialog.findChild<QToolButton *>(
            QStringLiteral("canvasAnchorTopLeft"));
        QToolButton *center = dialog.findChild<QToolButton *>(
            QStringLiteral("canvasAnchorCenter"));
        QToolButton *bottomRight = dialog.findChild<QToolButton *>(
            QStringLiteral("canvasAnchorBottomRight"));
        QVERIFY(relativeCheck);
        QVERIFY(widthSpin);
        QVERIFY(heightSpin);
        QVERIFY(offsetXSpin);
        QVERIFY(offsetYSpin);
        QVERIFY(topLeft);
        QVERIFY(center);
        QVERIFY(bottomRight);

        QCOMPARE(dialog.canvasSize(), QSize(640, 480));
        QCOMPARE(dialog.contentOffset(), QPoint());
        QVERIFY(center->isChecked());

        widthSpin->setValue(800);
        heightSpin->setValue(600);
        QCOMPARE(dialog.canvasSize(), QSize(800, 600));
        QCOMPARE(dialog.contentOffset(), QPoint(80, 60));

        topLeft->click();
        QCOMPARE(dialog.contentOffset(), QPoint());
        bottomRight->click();
        QCOMPARE(dialog.contentOffset(), QPoint(160, 120));

        relativeCheck->setChecked(true);
        QCOMPARE(dialog.canvasSize(), QSize(800, 600));
        QCOMPARE(widthSpin->value(), 160);
        QCOMPARE(heightSpin->value(), 120);
        widthSpin->setValue(-100);
        heightSpin->setValue(20);
        QCOMPARE(dialog.canvasSize(), QSize(540, 500));
        QCOMPARE(dialog.contentOffset(), QPoint(-100, 20));

        center->click();
        QCOMPARE(dialog.contentOffset(), QPoint(-50, 10));
        offsetXSpin->setValue(37);
        offsetYSpin->setValue(-12);
        QCOMPARE(dialog.contentOffset(), QPoint(37, -12));
        QVERIFY(!center->isChecked());

        relativeCheck->setChecked(false);
        QCOMPARE(widthSpin->value(), 540);
        QCOMPARE(heightSpin->value(), 500);
        const CanvasSizeDialog::Result result = dialog.result();
        QCOMPARE(result.size, QSize(540, 500));
        QCOMPARE(result.contentOffset, QPoint(37, -12));
    }

    void configuresImageSizeDialog()
    {
        ImageSizeDialog dialog(QSize(640, 480));
        QSpinBox *widthSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("imageWidthSpin"));
        QSpinBox *heightSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("imageHeightSpin"));
        QDoubleSpinBox *percentageSpin = dialog.findChild<QDoubleSpinBox *>(
            QStringLiteral("imageScalePercentSpin"));
        QCheckBox *keepAspectCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("imageKeepAspectCheck"));
        QLabel *warningLabel = dialog.findChild<QLabel *>(
            QStringLiteral("imageDistortionWarningLabel"));
        QVERIFY(widthSpin);
        QVERIFY(heightSpin);
        QVERIFY(percentageSpin);
        QVERIFY(keepAspectCheck);
        QVERIFY(warningLabel);
        QVERIFY(keepAspectCheck->isChecked());

        widthSpin->setValue(1280);
        QCOMPARE(dialog.imageSize(), QSize(1280, 960));
        QVERIFY(qAbs(dialog.horizontalScale() - 2.0) < 0.0001);
        QVERIFY(qAbs(dialog.verticalScale() - 2.0) < 0.0001);

        percentageSpin->setValue(150.0);
        QCOMPARE(dialog.imageSize(), QSize(960, 720));
        keepAspectCheck->setChecked(false);
        widthSpin->setValue(800);
        heightSpin->setValue(900);
        const ImageSizeDialog::Result distorted = dialog.result();
        QCOMPARE(distorted.size, QSize(800, 900));
        QVERIFY(qAbs(distorted.horizontalScale - 1.25) < 0.0001);
        QVERIFY(qAbs(distorted.verticalScale - 1.875) < 0.0001);
        QVERIFY(warningLabel->text().contains(QStringLiteral("distorted")));

        keepAspectCheck->setChecked(true);
        QCOMPARE(dialog.imageSize(), QSize(800, 600));
        percentageSpin->setValue(200.0);
        QCOMPARE(dialog.imageSize(), QSize(1280, 960));
        const ImageSizeDialog::Result uniform = dialog.result();
        QVERIFY(qAbs(uniform.horizontalScale - 2.0) < 0.0001);
        QVERIFY(qAbs(uniform.verticalScale - 2.0) < 0.0001);
    }

    void unlocksImageSizeDialogForExtremeAspectRatios()
    {
        QCOMPARE(
            LayerThumbnailRenderer::renderSize(QSize(1, 4096)), QSize(1, 64));
        QCOMPARE(
            LayerThumbnailRenderer::renderSize(QSize(4096, 1)), QSize(96, 1));
        QCOMPARE(PreviewRenderPolicy::renderSize(QSize(4096, 4096), 16.0),
            QSize(4096, 4096));

        ImageSizeDialog dialog(QSize(4096, 1));
        QSpinBox *widthSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("imageWidthSpin"));
        QSpinBox *heightSpin =
            dialog.findChild<QSpinBox *>(QStringLiteral("imageHeightSpin"));
        QCheckBox *keepAspectCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("imageKeepAspectCheck"));
        QDialogButtonBox *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(widthSpin);
        QVERIFY(heightSpin);
        QVERIFY(keepAspectCheck);
        QVERIFY(buttons);
        QVERIFY(!keepAspectCheck->isChecked());
        QVERIFY(!keepAspectCheck->isEnabled());
        QCOMPARE(widthSpin->minimum(), 1);
        QCOMPARE(widthSpin->maximum(), 4096);
        QCOMPARE(heightSpin->minimum(), 1);
        QCOMPARE(heightSpin->maximum(), 4096);

        widthSpin->setValue(2048);
        QCOMPARE(dialog.imageSize(), QSize(2048, 1));
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());

        ImageSizeDialog tallDialog(QSize(1, 4096));
        QCheckBox *tallKeepAspect = tallDialog.findChild<QCheckBox *>(
            QStringLiteral("imageKeepAspectCheck"));
        QVERIFY(tallKeepAspect);
        QVERIFY(!tallKeepAspect->isChecked());
        QVERIFY(!tallKeepAspect->isEnabled());

        ImageSizeDialog narrowDialog(QSize(100, 1));
        QCheckBox *narrowKeepAspect = narrowDialog.findChild<QCheckBox *>(
            QStringLiteral("imageKeepAspectCheck"));
        QSpinBox *narrowWidthSpin = narrowDialog.findChild<QSpinBox *>(
            QStringLiteral("imageWidthSpin"));
        QVERIFY(narrowKeepAspect);
        QVERIFY(narrowWidthSpin);
        QVERIFY(narrowKeepAspect->isChecked());
        QVERIFY(narrowKeepAspect->isEnabled());
        narrowWidthSpin->setValue(400);
        QCOMPARE(narrowDialog.imageSize(), QSize(400, 4));
    }

    void handlesUnsavedChangesDialogShortcuts_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<bool>("closesWindow");
        QTest::addColumn<bool>("savesDocument");

        QTest::newRow("save") << int(Qt::Key_S) << true << true;
        QTest::newRow("discard") << int(Qt::Key_N) << true << false;
        QTest::newRow("cancel") << int(Qt::Key_Escape) << false << false;
    }

    void handlesUnsavedChangesDialogShortcuts()
    {
        QFETCH(int, key);
        QFETCH(bool, closesWindow);
        QFETCH(bool, savesDocument);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EnvironmentVariableGuard recoveryGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH",
            directory.filePath(QStringLiteral("recovery.wagle")).toUtf8());
        const QString filePath =
            directory.filePath(QStringLiteral("shortcuts.wagle"));
        QString error;
        QVERIFY2(
            DocumentSerializer::save(
                filePath, Document::createDefault(QSize(100, 100)), &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QToolButton *addLayerButton =
            window.findChild<QToolButton *>(QStringLiteral("layerAddButton"));
        QVERIFY(addLayerButton);
        addLayerButton->click();
        QTRY_VERIFY(window.isWindowModified());

        bool dialogInspected = false;
        QString saveText;
        QString discardText;
        QString cancelText;
        QTimer::singleShot(0,
            &window,
            [&]()
            {
                QDialog *dialog =
                    qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                {
                    return;
                }
                QPushButton *saveButton = dialog->findChild<QPushButton *>(
                    QStringLiteral("unsavedChangesSaveButton"));
                QPushButton *discardButton = dialog->findChild<QPushButton *>(
                    QStringLiteral("unsavedChangesDiscardButton"));
                QPushButton *cancelButton = dialog->findChild<QPushButton *>(
                    QStringLiteral("unsavedChangesCancelButton"));
                if (!saveButton || !discardButton || !cancelButton)
                {
                    QTest::keyClick(dialog, Qt::Key_Escape);
                    return;
                }
                saveText = saveButton->text();
                discardText = discardButton->text();
                cancelText = cancelButton->text();
                dialogInspected = true;
                QWidget *keyTarget = QApplication::focusWidget();
                QTest::keyClick(
                    keyTarget ? keyTarget : dialog, static_cast<Qt::Key>(key));
            });
        QTimer::singleShot(1000,
            &window,
            []()
            {
                QDialog *dialog =
                    qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (dialog)
                {
                    dialog->reject();
                }
            });

        QCOMPARE(window.close(), closesWindow);
        QVERIFY(dialogInspected);
        QCOMPARE(saveText, QStringLiteral("Save (S)"));
        QCOMPARE(discardText, QStringLiteral("Don't Save (N)"));
        QCOMPARE(cancelText, QStringLiteral("Cancel (ESC)"));

        const std::optional<Document> savedDocument =
            DocumentSerializer::load(filePath, &error);
        QVERIFY2(savedDocument.has_value(), qPrintable(error));
        QCOMPARE(savedDocument->layers.size(), savesDocument ? 2 : 1);
        if (!closesWindow)
        {
            QVERIFY(window.isVisible());
            QVERIFY(window.isWindowModified());
        }
    }

    void selectionParticipatesInUndoHistory()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QAction *redoAction =
            window.findChild<QAction *>(QStringLiteral("redoAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(undoAction);
        QVERIFY(redoAction);
        QVERIFY(!window.isWindowModified());

        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        const QPoint topLeft = center - QPoint(80, 60);
        const QPoint topRight = center + QPoint(80, -60);
        const QPoint bottomRight = center + QPoint(80, 60);
        const QPoint bottomLeft = center + QPoint(-80, 60);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(canvas, topRight, 5);
        QTest::mouseMove(canvas, bottomRight, 5);
        QTest::mouseMove(canvas, bottomLeft, 5);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);

        QTRY_VERIFY(canvas->hasSelection());
        QVERIFY(undoAction->isEnabled());
        QVERIFY(!window.isWindowModified());

        const QString screenshotPath =
            qEnvironmentVariable("WOBBLEPAINT_SELECTION_SCREENSHOT");
        if (!screenshotPath.isEmpty())
        {
            QVERIFY(window.grab().save(screenshotPath, "PNG"));
            QVERIFY(QFileInfo(screenshotPath).size() > 0);
        }

        undoAction->trigger();
        QTRY_VERIFY(!canvas->hasSelection());
        QVERIFY(redoAction->isEnabled());
        QVERIFY(!window.isWindowModified());

        redoAction->trigger();
        QTRY_VERIFY(canvas->hasSelection());
        QVERIFY(!window.isWindowModified());
    }

    void selectsAStrokeCrossingTheLasso()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(undoAction);

        const QPoint center = canvas->rect().center();
        const QPoint strokeStart = center - QPoint(150, 0);
        const QPoint strokeEnd = center + QPoint(150, 0);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, strokeStart);
        QTest::mouseMove(canvas, strokeEnd, 5);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, strokeEnd);
        QTRY_VERIFY(window.isWindowModified());

        lassoAction->trigger();
        const QPoint topLeft = center - QPoint(30, 30);
        const QPoint topRight = center + QPoint(30, -30);
        const QPoint bottomRight = center + QPoint(30, 30);
        const QPoint bottomLeft = center + QPoint(-30, 30);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(canvas, topRight, 5);
        QTest::mouseMove(canvas, bottomRight, 5);
        QTest::mouseMove(canvas, bottomLeft, 5);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::keyClick(canvas, Qt::Key_Delete);

        undoAction->trigger();
        undoAction->trigger();
        QVERIFY(window.isWindowModified());
        undoAction->trigger();
        QVERIFY(!window.isWindowModified());
    }

    void selectsWithRectangleAndEllipseShapes()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;

        Stroke centerStroke;
        centerStroke.width = 2.0;
        centerStroke.points = {
            {QPointF(45.0, 50.0), 1.0}, {QPointF(55.0, 50.0), 1.0}};
        centerStroke.brush.antialiasing = false;
        const QUuid centerId = centerStroke.id;

        Stroke cornerStroke;
        cornerStroke.width = 2.0;
        cornerStroke.points = {
            {QPointF(22.0, 22.0), 1.0}, {QPointF(24.0, 22.0), 1.0}};
        cornerStroke.brush.antialiasing = false;
        const QUuid cornerId = cornerStroke.id;

        document.layers.first().strokes = {centerStroke, cornerStroke};
        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.setZoomPercent(100);
        canvas.setTool(CanvasWidget::Tool::Lasso);

        const auto widgetPoint = [&canvas](const QPointF &documentPoint)
        {
            return (QPointF(canvas.rect().center()) + documentPoint
                    - QPointF(50.0, 50.0))
                .toPoint();
        };
        const QPoint topLeft = widgetPoint(QPointF(20.0, 20.0));
        const QPoint bottomRight = widgetPoint(QPointF(80.0, 80.0));

        canvas.setSelectionShape(CanvasWidget::SelectionShape::Rectangle);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, bottomRight);
        QTRY_VERIFY(canvas.hasTransformableSelection());
        const QVector<QUuid> rectangleIds = canvas.selectedStrokeIds();
        QVERIFY(rectangleIds.contains(centerId));
        QVERIFY(rectangleIds.contains(cornerId));

        canvas.deselectSelection();
        canvas.setSelectionShape(CanvasWidget::SelectionShape::Ellipse);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, bottomRight);
        QTRY_VERIFY(canvas.hasTransformableSelection());
        const QVector<QUuid> ellipseIds = canvas.selectedStrokeIds();
        QVERIFY(ellipseIds.contains(centerId));
        QVERIFY(!ellipseIds.contains(cornerId));
    }

    void selectsRectangleWithTabletPen()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.setZoomPercent(100);
        canvas.setTool(CanvasWidget::Tool::Lasso);
        canvas.setSelectionShape(CanvasWidget::SelectionShape::Rectangle);

        QPointingDevice stylus(QStringLiteral("Selection stylus"),
            2,
            QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position
                | QInputDevice::Capability::Pressure,
            1,
            1);
        const QPointF start = canvas.rect().center() - QPoint(30, 25);
        const QPointF end = canvas.rect().center() + QPoint(30, 25);
        const QPointF globalStart = canvas.mapToGlobal(start.toPoint());
        const QPointF globalEnd = canvas.mapToGlobal(end.toPoint());
        QTabletEvent tabletPress(QEvent::TabletPress,
            &stylus,
            start,
            globalStart,
            0.7,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::LeftButton,
            Qt::LeftButton);
        QApplication::sendEvent(&canvas, &tabletPress);
        QTabletEvent tabletRelease(QEvent::TabletRelease,
            &stylus,
            end,
            globalEnd,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::LeftButton,
            Qt::NoButton);
        QApplication::sendEvent(&canvas, &tabletRelease);

        QTRY_VERIFY(canvas.hasSelection());
        QVERIFY(controller.document().layers.first().strokes.isEmpty());
    }

    void keepsSelectionAcrossToolsAndTransformsIt()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *brushAction =
            window.findChild<QAction *>(QStringLiteral("brushAction"));
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *bucketAction =
            window.findChild<QAction *>(QStringLiteral("bucketAction"));
        QAction *scaleAction =
            window.findChild<QAction *>(QStringLiteral("scaleSelectionAction"));
        QAction *rotateAction = window.findChild<QAction *>(
            QStringLiteral("rotateSelectionAction"));
        QAction *duplicateAction = window.findChild<QAction *>(
            QStringLiteral("duplicateSelectionAction"));
        QAction *moveAction =
            window.findChild<QAction *>(QStringLiteral("moveSelectionAction"));
        QAction *applyTransformAction = window.findChild<QAction *>(
            QStringLiteral("applySelectionTransformAction"));
        QAction *cancelTransformAction = window.findChild<QAction *>(
            QStringLiteral("cancelSelectionTransformAction"));
        SelectionActionBar *actionBar =
            window.findChild<SelectionActionBar *>();
        QToolButton *moveButton = window.findChild<QToolButton *>(
            QStringLiteral("moveSelectionButton"));
        QToolButton *applyTransformButton = window.findChild<QToolButton *>(
            QStringLiteral("applySelectionTransformButton"));
        QToolButton *cancelTransformButton = window.findChild<QToolButton *>(
            QStringLiteral("cancelSelectionTransformButton"));
        QVERIFY(canvas);
        QVERIFY(brushAction);
        QVERIFY(lassoAction);
        QVERIFY(bucketAction);
        QVERIFY(scaleAction);
        QVERIFY(rotateAction);
        QVERIFY(duplicateAction);
        QVERIFY(moveAction);
        QVERIFY(applyTransformAction);
        QVERIFY(cancelTransformAction);
        QVERIFY(actionBar);
        QVERIFY(moveButton);
        QVERIFY(applyTransformButton);
        QVERIFY(cancelTransformButton);
        QVERIFY(!scaleAction->isEnabled());
        QVERIFY(!rotateAction->isEnabled());
        QVERIFY(!duplicateAction->isEnabled());
        QVERIFY(!moveAction->isEnabled());
        QVERIFY(!applyTransformAction->isEnabled());
        QVERIFY(!cancelTransformAction->isEnabled());
        QVERIFY(!actionBar->isVisible());

        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(60, 0));
        QTest::mouseMove(canvas, center + QPoint(60, 0), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(60, 0));

        lassoAction->trigger();
        const QPoint topLeft = center - QPoint(90, 50);
        const QPoint topRight = center + QPoint(90, -50);
        const QPoint bottomRight = center + QPoint(90, 50);
        const QPoint bottomLeft = center + QPoint(-90, 50);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(canvas, topRight, 5);
        QTest::mouseMove(canvas, bottomRight, 5);
        QTest::mouseMove(canvas, bottomLeft, 5);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);

        QTRY_VERIFY(canvas->hasSelection());
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QTRY_VERIFY(scaleAction->isEnabled());
        QTRY_VERIFY(rotateAction->isEnabled());
        QTRY_VERIFY(duplicateAction->isEnabled());
        QTRY_VERIFY(moveAction->isEnabled());
        QTRY_VERIFY(actionBar->isVisible());

        bucketAction->trigger();
        QVERIFY(canvas->hasSelection());
        brushAction->trigger();
        QVERIFY(canvas->hasSelection());
        QVERIFY(canvas->scaleSelection(0.75));
        QVERIFY(canvas->hasPendingSelectionTransform());
        QVERIFY(applyTransformAction->isEnabled());
        QVERIFY(cancelTransformAction->isEnabled());
        QVERIFY(canvas->rotateSelection(90.0));
        QVERIFY(canvas->hasPendingSelectionTransform());
        QTest::mouseClick(applyTransformButton, Qt::LeftButton);
        QTRY_VERIFY(!canvas->hasSelectionTransformSession());
        QVERIFY(!applyTransformAction->isEnabled());
        QVERIFY(!cancelTransformAction->isEnabled());
        duplicateAction->trigger();
        QVERIFY(canvas->hasTransformableSelection());

        QTest::mouseClick(moveButton, Qt::LeftButton);
        QVERIFY(canvas->selectionMoveMode());
        moveButton->setFocus();
        QVERIFY(moveButton->hasFocus());
        QTest::keyClick(moveButton, Qt::Key_Escape);
        QTRY_VERIFY(!canvas->selectionMoveMode());
        QVERIFY(canvas->hasSelection());
        QTest::keyClick(moveButton, Qt::Key_Escape);
        QTRY_VERIFY(!canvas->hasSelection());
    }

    void routesTransformApplyAndCancelThroughMainWindowShortcuts()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath =
            directory.filePath(QStringLiteral("transform-shortcuts.wagle"));
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke source;
        source.width = 10.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);
        QString error;
        QVERIFY2(DocumentSerializer::save(filePath, document, &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *applyAction = window.findChild<QAction *>(
            QStringLiteral("applySelectionTransformAction"));
        QAction *cancelAction = window.findChild<QAction *>(
            QStringLiteral("cancelSelectionTransformAction"));
        QAction *escapeAction =
            window.findChild<QAction *>(QStringLiteral("escapeCanvasAction"));
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QToolButton *applyButton = window.findChild<QToolButton *>(
            QStringLiteral("applySelectionTransformButton"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(applyAction);
        QVERIFY(cancelAction);
        QVERIFY(escapeAction);
        QVERIFY(undoAction);
        QVERIFY(applyButton);
        QCOMPARE(
            applyAction->shortcut(), QKeySequence(QStringLiteral("Return")));
        QCOMPARE(escapeAction->shortcut(), QKeySequence(Qt::Key_Escape));

        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        const QPoint topLeft = center - QPoint(100, 70);
        const QPoint topRight = center + QPoint(100, -70);
        const QPoint bottomRight = center + QPoint(100, 70);
        const QPoint bottomLeft = center + QPoint(-100, 70);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(canvas, topRight, 5);
        QTest::mouseMove(canvas, bottomRight, 5);
        QTest::mouseMove(canvas, bottomLeft, 5);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QVERIFY(!window.isWindowModified());
        const QString undoTextBeforeTransform = undoAction->text();

        QVERIFY(canvas->rotateSelection(15.0));
        QVERIFY(applyAction->isEnabled());
        QVERIFY(cancelAction->isEnabled());
        QVERIFY(canvas->hasPendingSelectionTransform());
        QSignalSpy escapeTriggered(escapeAction, &QAction::triggered);
        applyButton->setFocus();
        QVERIFY(applyButton->hasFocus());
        QTest::keyClick(applyButton, Qt::Key_Escape);
        QTRY_COMPARE(escapeTriggered.size(), 1);
        QVERIFY(!canvas->hasSelectionTransformSession());
        QVERIFY(canvas->hasTransformableSelection());
        QVERIFY(!window.isWindowModified());
        QCOMPARE(undoAction->text(), undoTextBeforeTransform);

        QVERIFY(canvas->scaleSelection(0.8));
        QVERIFY(canvas->hasPendingSelectionTransform());
        QSignalSpy applyTriggered(applyAction, &QAction::triggered);
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Return);
        QTRY_COMPARE(applyTriggered.size(), 1);
        QTRY_VERIFY(!canvas->hasSelectionTransformSession());
        QTRY_VERIFY(window.isWindowModified());
        QVERIFY(undoAction->text() != undoTextBeforeTransform);

        undoAction->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QCOMPARE(undoAction->text(), undoTextBeforeTransform);
    }

    void escapeDeselectsAfterConfirmedRotation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString filePath =
            directory.filePath(QStringLiteral("rotate-then-deselect.wagle"));
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke source;
        source.width = 10.0;
        source.points = {
            {QPointF(25.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);
        QString error;
        QVERIFY2(DocumentSerializer::save(filePath, document, &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *rotateAction = window.findChild<QAction *>(
            QStringLiteral("rotateSelectionAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(rotateAction);

        canvas->setSelectionShape(CanvasWidget::SelectionShape::Rectangle);
        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(120, 80));
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(120, 80));
        QTRY_VERIFY(canvas->hasTransformableSelection());

        bool rotationAccepted = false;
        QTimer::singleShot(0,
            &window,
            [&rotationAccepted]()
            {
                QInputDialog *dialog = qobject_cast<QInputDialog *>(
                    QApplication::activeModalWidget());
                if (!dialog)
                {
                    return;
                }
                dialog->setDoubleValue(90.0);
                rotationAccepted = true;
                dialog->accept();
            });
        QTimer::singleShot(1000,
            &window,
            []()
            {
                if (QWidget *dialog = QApplication::activeModalWidget())
                {
                    dialog->close();
                }
            });
        rotateAction->trigger();
        QVERIFY(rotationAccepted);
        QVERIFY(!canvas->hasSelectionTransformSession());
        QVERIFY(canvas->hasTransformableSelection());
        const QByteArray rotatedDocument = DocumentSerializer::toJson(
            canvas->documentWithPendingSelectionTransform());
        QVERIFY(rotatedDocument != DocumentSerializer::toJson(document));

        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Escape);
        QTRY_VERIFY(!canvas->hasSelection());
        QCOMPARE(DocumentSerializer::toJson(
                     canvas->documentWithPendingSelectionTransform()),
            rotatedDocument);
    }

    void marksPendingTransformUnsavedAndPromptsBeforeClose()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EnvironmentVariableGuard recoveryGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH",
            directory.filePath(QStringLiteral("recovery.wagle")).toUtf8());
        const QString filePath =
            directory.filePath(QStringLiteral("pending-close.wagle"));
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke source;
        source.width = 10.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);
        QString error;
        QVERIFY2(DocumentSerializer::save(filePath, document, &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);

        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTest::mouseMove(canvas, center + QPoint(100, -70), 5);
        QTest::mouseMove(canvas, center + QPoint(100, 70), 5);
        QTest::mouseMove(canvas, center + QPoint(-100, 70), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QVERIFY(!window.isWindowModified());

        QVERIFY(canvas->rotateSelection(15.0));
        QVERIFY(canvas->hasPendingSelectionTransform());
        QTRY_VERIFY(window.isWindowModified());

        bool promptShown = false;
        QTimer::singleShot(0,
            &window,
            [&promptShown]()
            {
                QDialog *dialog =
                    qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                {
                    return;
                }
                QPushButton *cancelButton = dialog->findChild<QPushButton *>(
                    QStringLiteral("unsavedChangesCancelButton"));
                if (!cancelButton)
                {
                    QTest::keyClick(dialog, Qt::Key_Escape);
                    return;
                }
                promptShown = true;
                cancelButton->click();
            });
        QTimer::singleShot(1000,
            &window,
            []()
            {
                QDialog *dialog =
                    qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (dialog)
                {
                    dialog->reject();
                }
            });
        QVERIFY(!window.close());
        QVERIFY(promptShown);
        QVERIFY(canvas->hasPendingSelectionTransform());
        QVERIFY(window.isWindowModified());

        canvas->cancelSelectionTransform();
        QTRY_VERIFY(!window.isWindowModified());
        QTimer::singleShot(1000,
            &window,
            []()
            {
                QDialog *dialog =
                    qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (dialog)
                {
                    dialog->reject();
                }
            });
        QVERIFY(window.close());
    }

    void savesPendingTransformAtomicallyAndBecomesClean()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EnvironmentVariableGuard recoveryGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH",
            directory.filePath(QStringLiteral("recovery.wagle")).toUtf8());
        const QString filePath =
            directory.filePath(QStringLiteral("pending-save.wagle"));
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke source;
        source.color = QColor(35, 95, 225);
        source.width = 10.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);
        QString error;
        QVERIFY2(DocumentSerializer::save(filePath, document, &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *saveAction =
            window.findChild<QAction *>(QStringLiteral("saveAction"));
        QAction *stackUndoAction =
            window.findChild<QAction *>(QStringLiteral("undoStackAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(saveAction);
        QVERIFY(stackUndoAction);

        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTest::mouseMove(canvas, center + QPoint(100, -70), 5);
        QTest::mouseMove(canvas, center + QPoint(100, 70), 5);
        QTest::mouseMove(canvas, center + QPoint(-100, 70), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTRY_VERIFY(canvas->hasTransformableSelection());

        QVERIFY(canvas->scaleSelection(0.8));
        QVERIFY(canvas->rotateSelection(20.0));
        QVERIFY(canvas->hasPendingSelectionTransform());
        const QImage preview = RenderEngine::render(
            canvas->documentWithPendingSelectionTransform(), 0);
        const QString stackTextBeforeSave = stackUndoAction->text();

        saveAction->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        QVERIFY(!canvas->hasPendingSelectionTransform());
        QVERIFY(!canvas->hasSelectionTransformSession());
        QVERIFY(stackUndoAction->isEnabled());
        QVERIFY(stackUndoAction->text() != stackTextBeforeSave);

        const std::optional<Document> saved =
            DocumentSerializer::load(filePath, &error);
        QVERIFY2(saved.has_value(), qPrintable(error));
        QCOMPARE(saved->layers.first().strokes.size(), 2);
        const Stroke &committed = saved->layers.first().strokes.last();
        QCOMPARE(committed.mode, StrokeMode::PixelSelection);
        QVERIFY(committed.pixelSelectionOp.has_value());
        QCOMPARE(RenderEngine::render(*saved, 0), preview);
        QCOMPARE(RenderEngine::render(
                     canvas->documentWithPendingSelectionTransform(), 0),
            preview);
    }

    void abortsSaveWhenPendingTransformCannotBeApplied()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EnvironmentVariableGuard recoveryGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH",
            directory.filePath(QStringLiteral("recovery.wagle")).toUtf8());
        const QString filePath =
            directory.filePath(QStringLiteral("pending-abort.wagle"));
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke source;
        source.width = 10.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);
        QString error;
        QVERIFY2(DocumentSerializer::save(filePath, document, &error),
            qPrintable(error));
        QFile savedFile(filePath);
        QVERIFY(savedFile.open(QIODevice::ReadOnly));
        const QByteArray savedBytes = savedFile.readAll();
        savedFile.close();

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *saveAction =
            window.findChild<QAction *>(QStringLiteral("saveAction"));
        QAction *stackUndoAction =
            window.findChild<QAction *>(QStringLiteral("undoStackAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(saveAction);
        QVERIFY(stackUndoAction);
        canvas->setZoomPercent(100);

        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(30, 30));
        QTest::mouseMove(canvas, center + QPoint(30, -30), 5);
        QTest::mouseMove(canvas, center + QPoint(30, 30), 5);
        QTest::mouseMove(canvas, center + QPoint(-30, 30), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(30, 30));
        QTRY_VERIFY(canvas->hasTransformableSelection());

        canvas->setSelectionMoveMode(true);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(canvas, center + QPoint(120, 0), 5);
        QTest::mouseMove(canvas, center + QPoint(250, 0), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(250, 0));
        QVERIFY(canvas->hasPendingSelectionTransform());
        const QTransform rejected = canvas->pendingSelectionTransform();
        QVERIFY(rejected.dx() > 89.0);
        const QString stackTextBeforeSave = stackUndoAction->text();
        const bool stackEnabledBeforeSave = stackUndoAction->isEnabled();

        bool failureShown = false;
        QTimer::singleShot(0,
            &window,
            [&failureShown]()
            {
                QDialog *dialog =
                    qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                {
                    return;
                }
                failureShown = true;
                QTest::keyClick(dialog, Qt::Key_Escape);
            });
        QTimer::singleShot(1000,
            &window,
            []()
            {
                QDialog *dialog =
                    qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (dialog)
                {
                    dialog->reject();
                }
            });
        saveAction->trigger();

        QVERIFY(failureShown);
        QVERIFY(canvas->hasPendingSelectionTransform());
        QCOMPARE(canvas->pendingSelectionTransform(), rejected);
        QVERIFY(window.isWindowModified());
        QCOMPARE(stackUndoAction->text(), stackTextBeforeSave);
        QCOMPARE(stackUndoAction->isEnabled(), stackEnabledBeforeSave);
        QFile unchangedFile(filePath);
        QVERIFY(unchangedFile.open(QIODevice::ReadOnly));
        QCOMPARE(unchangedFile.readAll(), savedBytes);
    }

    void autosavesPendingTransformSnapshotWithoutTouchingHistory()
    {
        const QString recoveryKey = QStringLiteral("recovery/sourcePath");
        SettingValueGuard recoveryValueGuard(recoveryKey);
        EnvironmentVariableGuard environmentGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString recoveryPath =
            directory.filePath(QStringLiteral("recovery.wagle"));
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH", recoveryPath.toUtf8());
        const QString filePath =
            directory.filePath(QStringLiteral("pending-autosave.wagle"));
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke source;
        source.width = 10.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);
        QString error;
        QVERIFY2(DocumentSerializer::save(filePath, document, &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *stackUndoAction =
            window.findChild<QAction *>(QStringLiteral("undoStackAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(stackUndoAction);

        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTest::mouseMove(canvas, center + QPoint(100, -70), 5);
        QTest::mouseMove(canvas, center + QPoint(100, 70), 5);
        QTest::mouseMove(canvas, center + QPoint(-100, 70), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTRY_VERIFY(canvas->hasTransformableSelection());

        QVERIFY(canvas->rotateSelection(25.0));
        QVERIFY(canvas->hasPendingSelectionTransform());
        const QString stackTextBefore = stackUndoAction->text();
        const QImage preview = RenderEngine::render(
            canvas->documentWithPendingSelectionTransform(), 0);

        QEvent deactivate(QEvent::ApplicationDeactivate);
        QApplication::sendEvent(qApp, &deactivate);
        QVERIFY(QFileInfo::exists(recoveryPath));
        QVERIFY(canvas->hasPendingSelectionTransform());
        QCOMPARE(stackUndoAction->text(), stackTextBefore);

        const std::optional<Document> recovered =
            DocumentSerializer::load(recoveryPath, &error);
        QVERIFY2(recovered.has_value(), qPrintable(error));
        QCOMPARE(recovered->layers.first().strokes.size(), 2);
        QCOMPARE(recovered->layers.first().strokes.last().mode,
            StrokeMode::PixelSelection);
        QCOMPARE(RenderEngine::render(*recovered, 0), preview);

        canvas->cancelSelectionTransform();
        QTRY_VERIFY(!QFileInfo::exists(recoveryPath));
        QVERIFY(!window.isWindowModified());
    }

    void routesUndoToPendingSessionBeforeHistory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EnvironmentVariableGuard recoveryGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH",
            directory.filePath(QStringLiteral("recovery.wagle")).toUtf8());
        const QString filePath =
            directory.filePath(QStringLiteral("undo-routing.wagle"));
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke source;
        source.width = 10.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);
        QString error;
        QVERIFY2(DocumentSerializer::save(filePath, document, &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *saveAction =
            window.findChild<QAction *>(QStringLiteral("saveAction"));
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QAction *redoAction =
            window.findChild<QAction *>(QStringLiteral("redoAction"));
        QAction *stackUndoAction =
            window.findChild<QAction *>(QStringLiteral("undoStackAction"));
        QAction *stackRedoAction =
            window.findChild<QAction *>(QStringLiteral("redoStackAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(saveAction);
        QVERIFY(undoAction);
        QVERIFY(redoAction);
        QVERIFY(stackUndoAction);
        QVERIFY(stackRedoAction);

        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTest::mouseMove(canvas, center + QPoint(100, -70), 5);
        QTest::mouseMove(canvas, center + QPoint(100, 70), 5);
        QTest::mouseMove(canvas, center + QPoint(-100, 70), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTRY_VERIFY(canvas->hasTransformableSelection());

        QVERIFY(canvas->rotateSelection(12.0));
        QVERIFY(canvas->applySelectionTransform());
        saveAction->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        QTRY_VERIFY(canvas->hasTransformableSelection());
        const QString stackUndoTextClean = stackUndoAction->text();
        const bool stackUndoEnabledClean = stackUndoAction->isEnabled();
        const bool stackRedoEnabledClean = stackRedoAction->isEnabled();
        const QByteArray cleanDocument = DocumentSerializer::toJson(
            canvas->documentWithPendingSelectionTransform());

        canvas->setSelectionMoveMode(true);
        QVERIFY(canvas->selectionMoveMode());
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(canvas, center + QPoint(40, 0), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(40, 0));
        QVERIFY(canvas->hasPendingSelectionTransform());
        QVERIFY(undoAction->isEnabled());
        QVERIFY(!redoAction->isEnabled());
        QVERIFY(undoAction->text() != stackUndoAction->text());
        QTRY_VERIFY(window.isWindowModified());

        undoAction->trigger();
        QVERIFY(!canvas->hasPendingSelectionTransform());
        QVERIFY(canvas->hasTransformableSelection());
        QVERIFY(!canvas->selectionMoveMode());
        QCOMPARE(stackUndoAction->text(), stackUndoTextClean);
        QCOMPARE(stackUndoAction->isEnabled(), stackUndoEnabledClean);
        QCOMPARE(stackRedoAction->isEnabled(), stackRedoEnabledClean);
        QCOMPARE(DocumentSerializer::toJson(
                     canvas->documentWithPendingSelectionTransform()),
            cleanDocument);
        QTRY_VERIFY(!window.isWindowModified());
        QCOMPARE(undoAction->text(), stackUndoAction->text());

        undoAction->trigger();
        QTRY_VERIFY(window.isWindowModified());
        QVERIFY(stackRedoAction->isEnabled());
        QVERIFY(redoAction->isEnabled());
        QTRY_VERIFY(canvas->hasTransformableSelection());

        QVERIFY(canvas->rotateSelection(8.0));
        QVERIFY(canvas->hasPendingSelectionTransform());
        QVERIFY(!redoAction->isEnabled());
        redoAction->trigger();
        QVERIFY(canvas->hasPendingSelectionTransform());
        QVERIFY(stackRedoAction->isEnabled());

        undoAction->trigger();
        QVERIFY(!canvas->hasPendingSelectionTransform());
        QVERIFY(stackRedoAction->isEnabled());
        QVERIFY(redoAction->isEnabled());
    }

    void enablesPendingUndoTextAndSingleUndoAfterApply()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EnvironmentVariableGuard recoveryGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH",
            directory.filePath(QStringLiteral("recovery.wagle")).toUtf8());
        const QString filePath =
            directory.filePath(QStringLiteral("pending-undo-text.wagle"));
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke source;
        source.width = 10.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);
        QString error;
        QVERIFY2(DocumentSerializer::save(filePath, document, &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QAction *stackUndoAction =
            window.findChild<QAction *>(QStringLiteral("undoStackAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(undoAction);
        QVERIFY(stackUndoAction);
        QVERIFY(!undoAction->isEnabled());

        const QImage originalFrame = RenderEngine::render(
            canvas->documentWithPendingSelectionTransform(), 0);

        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTest::mouseMove(canvas, center + QPoint(100, -70), 5);
        QTest::mouseMove(canvas, center + QPoint(100, 70), 5);
        QTest::mouseMove(canvas, center + QPoint(-100, 70), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QVERIFY(!window.isWindowModified());

        QVERIFY(canvas->rotateSelection(30.0));
        QVERIFY(canvas->hasPendingSelectionTransform());
        QVERIFY(undoAction->isEnabled());
        QVERIFY(undoAction->text() != stackUndoAction->text());

        undoAction->trigger();
        QVERIFY(!canvas->hasPendingSelectionTransform());
        QVERIFY(canvas->hasTransformableSelection());
        QTRY_VERIFY(!window.isWindowModified());
        QCOMPARE(undoAction->text(), stackUndoAction->text());
        QCOMPARE(RenderEngine::render(
                     canvas->documentWithPendingSelectionTransform(), 0),
            originalFrame);

        QVERIFY(canvas->rotateSelection(30.0));
        QVERIFY(canvas->applySelectionTransform());
        QTRY_VERIFY(window.isWindowModified());
        QVERIFY(RenderEngine::render(
                    canvas->documentWithPendingSelectionTransform(), 0)
                != originalFrame);

        undoAction->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        QCOMPARE(RenderEngine::render(
                     canvas->documentWithPendingSelectionTransform(), 0),
            originalFrame);
    }

    void commitsPendingTransformBeforeResizingCanvas()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EnvironmentVariableGuard recoveryGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH",
            directory.filePath(QStringLiteral("recovery.wagle")).toUtf8());
        const QString filePath =
            directory.filePath(QStringLiteral("pending-resize.wagle"));
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke source;
        source.width = 10.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);
        QString error;
        QVERIFY2(DocumentSerializer::save(filePath, document, &error),
            qPrintable(error));

        MainWindow window;
        window.resize(1000, 680);
        QVERIFY(window.openFile(filePath));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *resizeCanvasAction =
            window.findChild<QAction *>(QStringLiteral("resizeCanvasAction"));
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(resizeCanvasAction);
        QVERIFY(undoAction);

        const QImage originalFrame = RenderEngine::render(
            canvas->documentWithPendingSelectionTransform(), 0);

        lassoAction->trigger();
        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTest::mouseMove(canvas, center + QPoint(100, -70), 5);
        QTest::mouseMove(canvas, center + QPoint(100, 70), 5);
        QTest::mouseMove(canvas, center + QPoint(-100, 70), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(100, 70));
        QTRY_VERIFY(canvas->hasTransformableSelection());

        QVERIFY(canvas->rotateSelection(20.0));
        QVERIFY(canvas->hasPendingSelectionTransform());
        const QImage pendingFrame = RenderEngine::render(
            canvas->documentWithPendingSelectionTransform(), 0);

        bool dialogHandled = false;
        QTimer::singleShot(0,
            &window,
            [&]()
            {
                CanvasSizeDialog *dialog =
                    window.findChild<CanvasSizeDialog *>();
                if (!dialog)
                {
                    return;
                }
                QSpinBox *width = dialog->findChild<QSpinBox *>(
                    QStringLiteral("canvasWidthSpin"));
                if (!width)
                {
                    return;
                }
                width->setValue(width->value() + 20);
                dialogHandled = true;
                dialog->accept();
            });
        resizeCanvasAction->trigger();
        QVERIFY(dialogHandled);
        QVERIFY(!canvas->hasPendingSelectionTransform());
        QVERIFY(!canvas->hasSelectionTransformSession());
        QCOMPARE(canvas->documentWithPendingSelectionTransform().size,
            QSize(120, 100));
        const QImage resizedFrame = RenderEngine::render(
            canvas->documentWithPendingSelectionTransform(), 0);
        QCOMPARE(resizedFrame.copy(QRect(10, 0, 100, 100)), pendingFrame);

        undoAction->trigger();
        QCOMPARE(canvas->documentWithPendingSelectionTransform().size,
            QSize(100, 100));
        QCOMPARE(RenderEngine::render(
                     canvas->documentWithPendingSelectionTransform(), 0),
            pendingFrame);

        undoAction->trigger();
        QCOMPARE(RenderEngine::render(
                     canvas->documentWithPendingSelectionTransform(), 0),
            originalFrame);
        QTRY_VERIFY(!window.isWindowModified());
    }

    void keepsSelectionActionBarReachableInNarrowWindows()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke source;
        source.width = 10.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(300, 400);
        canvas.setAnimating(false);

        auto *bar = new SelectionActionBar(&canvas);
        const QStringList actionNames = {
            QStringLiteral("moveSelectionAction"),
            QStringLiteral("scaleSelectionAction"),
            QStringLiteral("rotateSelectionAction"),
            QStringLiteral("flipSelectionHorizontalAction"),
            QStringLiteral("flipSelectionVerticalAction"),
            QStringLiteral("applySelectionTransformAction"),
            QStringLiteral("cancelSelectionTransformAction"),
            QStringLiteral("duplicateSelectionAction"),
            QStringLiteral("deleteSelectionAction"),
            QStringLiteral("deselectSelectionAction"),
        };
        for (int index = 0; index < actionNames.size(); ++index)
        {
            auto *action = new QAction(actionNames[index], &canvas);
            action->setObjectName(actionNames[index]);
            bar->addAction(action);
            if (index == 2 || index == 4 || index == 6 || index == 8)
            {
                bar->addSeparator();
            }
        }
        canvas.setSelectionActionBar(bar);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        canvas.setTool(CanvasWidget::Tool::Lasso);
        const QPoint center = canvas.rect().center();
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(60, 60));
        QTest::mouseMove(&canvas, center + QPoint(60, -60), 5);
        QTest::mouseMove(&canvas, center + QPoint(60, 60), 5);
        QTest::mouseMove(&canvas, center + QPoint(-60, 60), 5);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(60, 60));
        QTRY_VERIFY(canvas.hasTransformableSelection());
        QTRY_VERIFY(bar->isVisible());

        QVERIFY(bar->width() > canvas.width());
        QCOMPARE(bar->x(), (canvas.width() - bar->width()) / 2);

        QToolButton *applyButton = bar->findChild<QToolButton *>(
            QStringLiteral("applySelectionTransformButton"));
        QToolButton *cancelButton = bar->findChild<QToolButton *>(
            QStringLiteral("cancelSelectionTransformButton"));
        QVERIFY(applyButton);
        QVERIFY(cancelButton);
        QVERIFY(canvas.rect().contains(
            applyButton->mapTo(&canvas, applyButton->rect().center())));
        QVERIFY(canvas.rect().contains(
            cancelButton->mapTo(&canvas, cancelButton->rect().center())));
    }

    void floatingSelectionTransformCommitsOnceAndCancelsLosslessly()
    {
        Document document = Document::createDefault(QSize(120, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke source;
        source.color = QColor(35, 95, 225);
        source.width = 12.0;
        source.points = {
            {QPointF(30.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(480, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint)
        {
            const QPointF center(canvas.rect().center());
            return (
                center + (documentPoint - QPointF(60.0, 50.0)) * canvas.zoom())
                .toPoint();
        };
        canvas.setTool(CanvasWidget::Tool::Lasso);
        const QPoint topLeft = widgetPoint(QPointF(20.0, 35.0));
        const QPoint topRight = widgetPoint(QPointF(80.0, 35.0));
        const QPoint bottomRight = widgetPoint(QPointF(80.0, 65.0));
        const QPoint bottomLeft = widgetPoint(QPointF(20.0, 65.0));
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(&canvas, topRight, 5);
        QTest::mouseMove(&canvas, bottomRight, 5);
        QTest::mouseMove(&canvas, bottomLeft, 5);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTRY_VERIFY(canvas.hasTransformableSelection());

        const QByteArray originalDocument =
            DocumentSerializer::toJson(controller.document());
        const QImage originalFrame =
            RenderEngine::render(controller.document(), 0);
        const int originalUndoCount = controller.undoStack()->count();
        const int originalUndoIndex = controller.undoStack()->index();

        QVERIFY(canvas.scaleSelection(1.2));
        const QTransform afterScale = canvas.pendingSelectionTransform();
        QVERIFY(canvas.rotateSelection(18.0));
        QVERIFY(canvas.pendingSelectionTransform() != afterScale);
        QVERIFY(canvas.flipSelectionHorizontally());
        QVERIFY(canvas.hasPendingSelectionTransform());
        QCOMPARE(DocumentSerializer::toJson(controller.document()),
            originalDocument);
        QCOMPARE(controller.document().layers.first().strokes.size(), 1);
        QCOMPARE(controller.undoStack()->count(), originalUndoCount);
        QCOMPARE(controller.undoStack()->index(), originalUndoIndex);

        canvas.handleEscape();
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(canvas.hasTransformableSelection());
        QCOMPARE(DocumentSerializer::toJson(controller.document()),
            originalDocument);
        QCOMPARE(controller.undoStack()->count(), originalUndoCount);
        QCOMPARE(controller.undoStack()->index(), originalUndoIndex);

        QVERIFY(canvas.scaleSelection(1.2));
        QVERIFY(canvas.rotateSelection(18.0));
        QVERIFY(canvas.flipSelectionHorizontally());
        const QTransform accumulated = canvas.pendingSelectionTransform();
        QCOMPARE(DocumentSerializer::toJson(controller.document()),
            originalDocument);
        QVERIFY(canvas.applySelectionTransform());
        QVERIFY(!canvas.hasSelectionTransformSession());
        QCOMPARE(controller.undoStack()->count(), originalUndoCount + 1);
        QCOMPARE(controller.undoStack()->index(), originalUndoIndex + 1);
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
        const Stroke &operation =
            controller.document().layers.first().strokes.last();
        QCOMPARE(operation.mode, StrokeMode::PixelSelection);
        QVERIFY(operation.pixelSelectionOp.has_value());
        QCOMPARE(operation.pixelSelectionOp->transform, accumulated);

        controller.undoStack()->undo();
        QCOMPARE(DocumentSerializer::toJson(controller.document()),
            originalDocument);
        QCOMPARE(RenderEngine::render(controller.document(), 0), originalFrame);

        QVERIFY(canvas.scaleSelection(1.1));
        QVERIFY(canvas.hasPendingSelectionTransform());
        canvas.setTool(CanvasWidget::Tool::Brush);
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(canvas.hasTransformableSelection());
        QCOMPARE(DocumentSerializer::toJson(controller.document()),
            originalDocument);

        QVERIFY(canvas.scaleSelection(1.1));
        const int beforeDuplicateIndex = controller.undoStack()->index();
        QVERIFY(canvas.duplicateSelection());
        QVERIFY(!canvas.hasSelectionTransformSession());
        QCOMPARE(controller.undoStack()->index(), beforeDuplicateIndex + 1);
        const Stroke &duplicateOperation =
            controller.document().layers.first().strokes.last();
        QCOMPARE(duplicateOperation.mode, StrokeMode::PixelSelection);
        QVERIFY(duplicateOperation.pixelSelectionOp.has_value());
        QVERIFY(qFuzzyCompare(
            duplicateOperation.pixelSelectionOp->transform.dx() + 1.0, 13.0));
        controller.undoStack()->undo();
        QCOMPARE(DocumentSerializer::toJson(controller.document()),
            originalDocument);

        QVERIFY(canvas.scaleSelection(1.1));
        QVERIFY(controller.resizeCanvas(QSize(130, 100), QPoint(5, 0)));
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(canvas.hasTransformableSelection());

        QVERIFY(canvas.rotateSelection(5.0));
        controller.loadDocument(document);
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(!canvas.hasSelection());
    }

    void clipsPartialFloatingTransformAndRetainsFailedSession()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::white;
        document.wobbleAmount = 0.0;
        Stroke source;
        source.color = QColor(35, 95, 225);
        source.width = 10.0;
        source.points = {{QPointF(5.0, 50.0), 1.0}, {QPointF(30.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(420, 420);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint)
        {
            const QPointF center(canvas.rect().center());
            return (
                center + (documentPoint - QPointF(50.0, 50.0)) * canvas.zoom())
                .toPoint();
        };
        const auto sample = [&canvas, &widgetPoint](const QPixmap &pixmap,
                                const QPointF &documentPoint)
        {
            const QImage image = pixmap.toImage();
            const QPoint widgetPosition = widgetPoint(documentPoint);
            const qreal xScale =
                static_cast<qreal>(image.width()) / canvas.width();
            const qreal yScale =
                static_cast<qreal>(image.height()) / canvas.height();
            return image.pixelColor(
                std::clamp(
                    qRound(widgetPosition.x() * xScale), 0, image.width() - 1),
                std::clamp(qRound(widgetPosition.y() * yScale),
                    0,
                    image.height() - 1));
        };
        canvas.setTool(CanvasWidget::Tool::Lasso);
        const QPoint topLeft = widgetPoint(QPointF(0.0, 38.0));
        const QPoint topRight = widgetPoint(QPointF(36.0, 38.0));
        const QPoint bottomRight = widgetPoint(QPointF(36.0, 62.0));
        const QPoint bottomLeft = widgetPoint(QPointF(0.0, 62.0));
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(&canvas, topRight, 5);
        QTest::mouseMove(&canvas, bottomRight, 5);
        QTest::mouseMove(&canvas, bottomLeft, 5);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTRY_VERIFY(canvas.hasTransformableSelection());

        const QByteArray beforeTransform =
            DocumentSerializer::toJson(controller.document());
        const QImage beforeFrame =
            RenderEngine::render(controller.document(), 0);
        const int undoCount = controller.undoStack()->count();
        const int undoIndex = controller.undoStack()->index();

        canvas.setSelectionMoveMode(true);
        const QPoint partialStart = widgetPoint(QPointF(10.0, 50.0));
        const QPoint partialEnd = widgetPoint(QPointF(-10.0, 50.0));
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, partialStart);
        QTest::mouseMove(&canvas, partialEnd, 5);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, partialEnd);
        QVERIFY(canvas.hasPendingSelectionTransform());
        QVERIFY(canvas.pendingSelectionTransform().dx() < -19.0);
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()), beforeTransform);
        const QPixmap partialPreview = canvas.grab();
        QVERIFY(canvas.applySelectionTransform());
        QApplication::processEvents();
        const QPixmap partialCommitted = canvas.grab();
        QCOMPARE(sample(partialPreview, QPointF(3.0, 50.0)),
            sample(partialCommitted, QPointF(3.0, 50.0)));
        QCOMPARE(controller.undoStack()->count(), undoCount + 1);
        QCOMPARE(controller.undoStack()->index(), undoIndex + 1);

        controller.undoStack()->undo();
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()), beforeTransform);
        QCOMPARE(RenderEngine::render(controller.document(), 0), beforeFrame);

        canvas.setSelectionMoveMode(true);
        const QPoint outsideStart = widgetPoint(QPointF(10.0, 50.0));
        const QPoint outsideEnd = widgetPoint(QPointF(-80.0, 50.0));
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, outsideStart);
        QTest::mouseMove(&canvas, outsideEnd, 5);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, outsideEnd);
        QVERIFY(canvas.hasPendingSelectionTransform());
        const QTransform rejected = canvas.pendingSelectionTransform();
        QVERIFY(rejected.dx() < -89.0);
        QVERIFY(!canvas.applySelectionTransform());
        QVERIFY(canvas.hasPendingSelectionTransform());
        QCOMPARE(canvas.pendingSelectionTransform(), rejected);
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()), beforeTransform);
        QCOMPARE(controller.undoStack()->index(), undoIndex);
        canvas.cancelSelectionTransform();
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(canvas.hasTransformableSelection());
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()), beforeTransform);
    }

    void requiresExplicitSelectionMoveMode()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);

        QAction moveAction(&canvas);
        moveAction.setObjectName(QStringLiteral("moveSelectionAction"));
        moveAction.setCheckable(true);
        connect(&moveAction,
            &QAction::toggled,
            &canvas,
            &CanvasWidget::setSelectionMoveMode);
        connect(&canvas,
            &CanvasWidget::selectionMoveModeChanged,
            &moveAction,
            &QAction::setChecked);

        auto *actionBar = new SelectionActionBar(&canvas);
        QToolButton *moveButton = actionBar->addAction(&moveAction);
        canvas.setSelectionActionBar(actionBar);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();
        QVERIFY(moveButton);
        QVERIFY(!actionBar->isVisible());

        const QPoint center = canvas.rect().center();
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(55, 0));
        QTest::mouseMove(&canvas, center + QPoint(55, 0), 5);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(55, 0));

        canvas.setTool(CanvasWidget::Tool::Lasso);
        const QPoint topLeft = center - QPoint(90, 55);
        const QPoint topRight = center + QPoint(90, -55);
        const QPoint bottomRight = center + QPoint(90, 55);
        const QPoint bottomLeft = center + QPoint(-90, 55);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(&canvas, topRight, 5);
        QTest::mouseMove(&canvas, bottomRight, 5);
        QTest::mouseMove(&canvas, bottomLeft, 5);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);

        QTRY_VERIFY(canvas.hasTransformableSelection());
        QTRY_VERIFY(actionBar->isVisible());
        QVERIFY(!moveAction.isChecked());
        QVERIFY(!canvas.selectionMoveMode());

        const QByteArray beforeInactiveDrag =
            DocumentSerializer::toJson(controller.document());
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(&canvas, center + QPoint(30, 10), 5);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(30, 10));
        QCOMPARE(DocumentSerializer::toJson(controller.document()),
            beforeInactiveDrag);

        controller.undoStack()->undo();
        QTRY_VERIFY(canvas.hasTransformableSelection());
        QTRY_VERIFY(actionBar->isVisible());
        QTest::mouseClick(moveButton, Qt::LeftButton);
        QVERIFY(moveAction.isChecked());
        QVERIFY(canvas.selectionMoveMode());

        const QByteArray beforeActiveDrag =
            DocumentSerializer::toJson(controller.document());
        const int undoCountBeforeActiveDrag = controller.undoStack()->count();
        const int undoIndexBeforeActiveDrag = controller.undoStack()->index();
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(&canvas, center + QPoint(35, 15), 5);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(35, 15));
        QCOMPARE(DocumentSerializer::toJson(controller.document()),
            beforeActiveDrag);
        QVERIFY(canvas.hasPendingSelectionTransform());
        QCOMPARE(controller.undoStack()->count(), undoCountBeforeActiveDrag);
        QCOMPARE(controller.undoStack()->index(), undoIndexBeforeActiveDrag);

        QVERIFY(canvas.applySelectionTransform());
        QVERIFY(!canvas.hasSelectionTransformSession());
        QVERIFY(DocumentSerializer::toJson(controller.document())
                != beforeActiveDrag);
        QCOMPARE(
            controller.undoStack()->count(), undoIndexBeforeActiveDrag + 1);
        QCOMPARE(
            controller.undoStack()->index(), undoIndexBeforeActiveDrag + 1);
    }

    void rejectsSelectionMoveJustOutsideTheCanvas()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 4.0;
        stroke.points = {{QPointF(1.0, 30.0), 1.0}, {QPointF(1.0, 70.0), 1.0}};
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint)
        {
            const QPointF center(canvas.rect().center());
            return (
                center + (documentPoint - QPointF(50.0, 50.0)) * canvas.zoom())
                .toPoint();
        };
        canvas.setTool(CanvasWidget::Tool::Lasso);
        const QPoint topLeft = widgetPoint(QPointF(0.0, 20.0));
        const QPoint topRight = widgetPoint(QPointF(15.0, 20.0));
        const QPoint bottomRight = widgetPoint(QPointF(15.0, 80.0));
        const QPoint bottomLeft = widgetPoint(QPointF(0.0, 80.0));
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(&canvas, topRight, 5);
        QTest::mouseMove(&canvas, bottomRight, 5);
        QTest::mouseMove(&canvas, bottomLeft, 5);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTRY_VERIFY(canvas.hasTransformableSelection());

        canvas.setSelectionMoveMode(true);
        QVERIFY(canvas.selectionMoveMode());
        const QByteArray before =
            DocumentSerializer::toJson(controller.document());
        QSignalSpy messages(&canvas, &CanvasWidget::interactionMessage);
        const QPoint justOutside = widgetPoint(QPointF(-0.25, 50.0));
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, justOutside);
        QTest::mouseMove(&canvas, justOutside + QPoint(20, 0), 5);
        QTest::mouseRelease(&canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            justOutside + QPoint(20, 0));

        QCOMPARE(DocumentSerializer::toJson(controller.document()), before);
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first().first().toString(),
            QStringLiteral("Drag inside the selection to move it."));
    }

    void selectionHitTestingRespectsClipAndLayerVisibility()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 5.0;
        stroke.points = {
            {QPointF(10.0, 50.0), 1.0}, {QPointF(90.0, 50.0), 1.0}};
        stroke.clipMask = QImage(document.size, QImage::Format_Grayscale8);
        stroke.clipMask.fill(0);
        for (int y = 0; y < stroke.clipMask.height(); ++y)
        {
            std::fill_n(stroke.clipMask.scanLine(y), 40, 255);
        }
        document.layers.first().strokes.append(stroke);
        const QUuid layerId = document.activeLayerId;

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint)
        {
            const QPointF center(canvas.rect().center());
            return (
                center + (documentPoint - QPointF(50.0, 50.0)) * canvas.zoom())
                .toPoint();
        };
        const auto lasso = [&canvas, &widgetPoint](const QRectF &documentRect)
        {
            const QPoint topLeft = widgetPoint(documentRect.topLeft());
            const QPoint topRight = widgetPoint(documentRect.topRight());
            const QPoint bottomRight = widgetPoint(documentRect.bottomRight());
            const QPoint bottomLeft = widgetPoint(documentRect.bottomLeft());
            QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
            QTest::mouseMove(&canvas, topRight, 5);
            QTest::mouseMove(&canvas, bottomRight, 5);
            QTest::mouseMove(&canvas, bottomLeft, 5);
            QTest::mouseRelease(
                &canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        };

        canvas.setTool(CanvasWidget::Tool::Lasso);
        lasso(QRectF(60.0, 40.0, 25.0, 20.0));
        QTRY_VERIFY(canvas.hasSelection());
        QVERIFY(!canvas.hasTransformableSelection());

        canvas.deselectSelection();
        controller.setLayerVisible(layerId, false);
        lasso(QRectF(10.0, 40.0, 25.0, 20.0));
        QTRY_VERIFY(canvas.hasSelection());
        QVERIFY(!canvas.hasTransformableSelection());
    }

    void selectionHitTestingUsesProceduralFillCoverage()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;

        Stroke boundary;
        boundary.color = Qt::black;
        boundary.width = 6.0;
        boundary.points = {
            {QPointF(50.0, 0.0), 1.0}, {QPointF(50.0, 99.0), 1.0}};
        boundary.brush.antialiasing = false;

        Stroke fill;
        fill.mode = StrokeMode::Fill;
        fill.color = QColor(30, 80, 220);
        fill.points = {{QPointF(70.0, 50.0), 1.0}};
        document.layers.first().strokes = {boundary, fill};

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint)
        {
            const QPointF center(canvas.rect().center());
            return (
                center + (documentPoint - QPointF(50.0, 50.0)) * canvas.zoom())
                .toPoint();
        };
        const auto lassoFillCoverage = [&]()
        {
            const QPoint topLeft = widgetPoint(QPointF(58.0, 38.0));
            const QPoint topRight = widgetPoint(QPointF(83.0, 38.0));
            const QPoint bottomRight = widgetPoint(QPointF(83.0, 63.0));
            const QPoint bottomLeft = widgetPoint(QPointF(58.0, 63.0));
            QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
            QTest::mouseMove(&canvas, topRight, 5);
            QTest::mouseMove(&canvas, bottomRight, 5);
            QTest::mouseMove(&canvas, bottomLeft, 5);
            QTest::mouseRelease(
                &canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        };

        canvas.setTool(CanvasWidget::Tool::Lasso);
        lassoFillCoverage();
        QTRY_VERIFY(canvas.hasTransformableSelection());

        canvas.deselectSelection();
        document.layers.first().strokes.last().points = {
            {QPointF(20.0, 50.0), 1.0}};
        controller.loadDocument(document);
        lassoFillCoverage();
        QTRY_VERIFY(canvas.hasSelection());
        QVERIFY(!canvas.hasTransformableSelection());
    }

    void selectionHitTestingUsesFinalLayerPixelsIncludingOperations()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;

        Stroke paint;
        paint.color = QColor(30, 90, 220);
        paint.width = 12.0;
        paint.points = {{QPointF(10.0, 50.0), 1.0}, {QPointF(35.0, 50.0), 1.0}};
        paint.brush.antialiasing = false;
        document.layers.first().strokes.append(paint);

        QImage sourceMask(document.size, QImage::Format_Grayscale8);
        sourceMask.fill(0);
        for (int y = 40; y < 61; ++y)
        {
            std::fill(
                sourceMask.scanLine(y) + 5, sourceMask.scanLine(y) + 41, 255);
        }
        QTransform shift;
        shift.translate(45.0, 0.0);
        const std::optional<PixelSelectionOp> operation =
            makePixelSelectionOp(sourceMask, shift, true, true);
        QVERIFY(operation.has_value());
        Stroke move;
        move.mode = StrokeMode::PixelSelection;
        move.pixelSelectionOp = *operation;
        document.layers.first().strokes.append(move);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint)
        {
            const QPointF center(canvas.rect().center());
            return (
                center + (documentPoint - QPointF(50.0, 50.0)) * canvas.zoom())
                .toPoint();
        };
        const auto lasso = [&canvas, &widgetPoint](const QRectF &documentRect)
        {
            const QPoint topLeft = widgetPoint(documentRect.topLeft());
            const QPoint topRight = widgetPoint(documentRect.topRight());
            const QPoint bottomRight = widgetPoint(documentRect.bottomRight());
            const QPoint bottomLeft = widgetPoint(documentRect.bottomLeft());
            QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
            QTest::mouseMove(&canvas, topRight, 5);
            QTest::mouseMove(&canvas, bottomRight, 5);
            QTest::mouseMove(&canvas, bottomLeft, 5);
            QTest::mouseRelease(
                &canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        };

        canvas.setTool(CanvasWidget::Tool::Lasso);
        QSignalSpy messages(&canvas, &CanvasWidget::interactionMessage);
        lasso(QRectF(10.0, 45.0, 20.0, 10.0));
        QTRY_VERIFY(canvas.hasSelection());
        QVERIFY(!canvas.hasTransformableSelection());
        QVERIFY(!messages.isEmpty());
        QCOMPARE(messages.last().first().toString(),
            QStringLiteral("No content in the selected area."));

        canvas.deselectSelection();
        lasso(QRectF(55.0, 45.0, 25.0, 10.0));
        QTRY_VERIFY(canvas.hasTransformableSelection());
        QCOMPARE(messages.last().first().toString(),
            QStringLiteral("Selected content. Use the action bar to transform "
                           "or remove it."));
    }

    void selectionAvailabilityChecksEveryAnimationFrame()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::transparent;
        document.animationFrames = 12;
        document.wobbleAmount = DocumentLimits::maximumWobbleAmount;
        Stroke animated;
        animated.seed = 0x7d1a2b3c4d5e6f70ULL;
        animated.color = QColor(35, 100, 225);
        animated.width = 2.0;
        animated.points = {
            {QPointF(35.0, 50.0), 1.0}, {QPointF(65.0, 50.0), 1.0}};
        animated.brush.antialiasing = false;
        document.layers.first().strokes.append(animated);

        QVector<QImage> frames;
        frames.reserve(document.animationFrames);
        for (int frame = 0; frame < document.animationFrames; ++frame)
        {
            QImage layerImage;
            QVERIFY(RenderEngine::renderStrokesOnLayer(layerImage,
                document,
                document.layers.first().strokes,
                frame,
                document.size));
            frames.append(std::move(layerImage));
        }

        QPoint laterFrameOnlyPixel(-1, -1);
        const QImage &first = frames.first();
        for (int y = 4; y < first.height() - 4 && laterFrameOnlyPixel.x() < 0;
            ++y)
        {
            for (int x = 4; x < first.width() - 4; ++x)
            {
                bool firstNeighborhoodIsTransparent = true;
                for (int offsetY = -3;
                    offsetY <= 3 && firstNeighborhoodIsTransparent;
                    ++offsetY)
                {
                    for (int offsetX = -3; offsetX <= 3; ++offsetX)
                    {
                        if (first.pixelColor(x + offsetX, y + offsetY).alpha()
                            != 0)
                        {
                            firstNeighborhoodIsTransparent = false;
                            break;
                        }
                    }
                }
                if (!firstNeighborhoodIsTransparent)
                {
                    continue;
                }
                const bool paintedLater = std::any_of(frames.cbegin() + 1,
                    frames.cend(),
                    [x, y](const QImage &frame)
                    {
                        return frame.pixelColor(x, y).alpha() != 0;
                    });
                if (paintedLater)
                {
                    laterFrameOnlyPixel = QPoint(x, y);
                    break;
                }
            }
        }
        QVERIFY(laterFrameOnlyPixel.x() >= 0);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();
        QCOMPARE(canvas.currentFrame(), 0);

        const auto widgetPoint = [&canvas](const QPointF &documentPoint)
        {
            const QPointF center(canvas.rect().center());
            return (
                center + (documentPoint - QPointF(50.0, 50.0)) * canvas.zoom())
                .toPoint();
        };
        const QPoint seed = widgetPoint(QPointF(
            laterFrameOnlyPixel.x() + 0.5, laterFrameOnlyPixel.y() + 0.5));
        canvas.setTool(CanvasWidget::Tool::Wand);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, seed);

        QTRY_VERIFY(canvas.hasTransformableSelection());
    }

    void wandReferencesActiveMarkedAndVisibleLayers()
    {
        const auto selectionMask = [](CanvasWidget::WandReference reference,
                                       bool marked) -> std::pair<QImage, bool>
        {
            Document document = Document::createDefault(QSize(100, 100));
            document.background = Qt::transparent;
            document.wobbleAmount = 0.0;
            Layer boundary;
            boundary.name = QStringLiteral("Boundary");
            boundary.initialCanvasSize = document.size;
            boundary.reference = marked;
            Stroke outline;
            outline.width = 4.0;
            outline.brush.antialiasing = false;
            outline.points = {{QPointF(20.0, 20.0), 1.0},
                {QPointF(80.0, 20.0), 1.0},
                {QPointF(80.0, 80.0), 1.0},
                {QPointF(20.0, 80.0), 1.0},
                {QPointF(20.0, 20.0), 1.0}};
            boundary.strokes.append(outline);
            document.layers.append(boundary);

            DocumentController controller;
            controller.loadDocument(document);
            CanvasWidget canvas(&controller);
            canvas.resize(400, 400);
            canvas.setAnimating(false);
            canvas.show();
            if (!QTest::qWaitForWindowExposed(&canvas))
            {
                return {{}, false};
            }
            canvas.fitToWindow();
            canvas.setWandReference(reference);
            canvas.setTool(CanvasWidget::Tool::Wand);
            QTest::mouseClick(&canvas,
                Qt::LeftButton,
                Qt::NoModifier,
                canvas.rect().center());
            if (!canvas.hasSelection())
            {
                return {{}, false};
            }
            canvas.setTool(CanvasWidget::Tool::Bucket);
            QTest::mouseClick(&canvas,
                Qt::LeftButton,
                Qt::NoModifier,
                canvas.rect().center());
            const Layer *active =
                controller.document().layer(document.activeLayerId);
            if (!active || active->strokes.isEmpty())
            {
                return {{}, false};
            }
            return {active->strokes.constLast().clipMask, true};
        };

        const auto activeLayer =
            selectionMask(CanvasWidget::WandReference::ActiveLayer, false);
        QVERIFY(activeLayer.second);
        QVERIFY(activeLayer.first.isNull());

        const auto visibleLayers =
            selectionMask(CanvasWidget::WandReference::AllVisibleLayers, false);
        QVERIFY(visibleLayers.second);
        const QImage &visibleLayersMask = visibleLayers.first;
        QVERIFY(!visibleLayersMask.isNull());
        QCOMPARE(visibleLayersMask.constScanLine(5)[5], static_cast<uchar>(0));
        QCOMPARE(
            visibleLayersMask.constScanLine(50)[50], static_cast<uchar>(255));

        const auto referenceLayers =
            selectionMask(CanvasWidget::WandReference::ReferenceLayers, true);
        QVERIFY(referenceLayers.second);
        const QImage &referenceLayersMask = referenceLayers.first;
        QVERIFY(!referenceLayersMask.isNull());
        QCOMPARE(
            referenceLayersMask.constScanLine(5)[5], static_cast<uchar>(0));
        QCOMPARE(
            referenceLayersMask.constScanLine(50)[50], static_cast<uchar>(255));

        const auto missingReference =
            selectionMask(CanvasWidget::WandReference::ReferenceLayers, false);
        QVERIFY(!missingReference.second);
    }

    void selectionVisibilitySkipsRedundantStaticFrames()
    {
        Document document = Document::createDefault(QSize(64, 64));
        document.background = Qt::transparent;
        document.animationFrames = 60;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.points = {
            {QPointF(24.0, 32.0), 1.0}, {QPointF(40.0, 32.0), 1.0}};
        document.layers.first().strokes.append(stroke);

        QImage mask(document.size, QImage::Format_Grayscale8);
        mask.fill(0);
        mask.scanLine(0)[0] = 255;

        SelectionVisibility::Result result = SelectionVisibility::evaluate(
            document, document.layers.first(), mask, 23);
        QVERIFY(result.renderSucceeded);
        QVERIFY(!result.hasVisiblePixels);
        QCOMPARE(result.renderedFrames, 1);

        document.wobbleAmount = 1.0;
        result = SelectionVisibility::evaluate(
            document, document.layers.first(), mask, 23);
        QVERIFY(result.renderSucceeded);
        QVERIFY(!result.hasVisiblePixels);
        QCOMPARE(result.renderedFrames, document.animationFrames);
    }

    void packedSelectionSnapshotRoundTripsWithin4kUndoBudget()
    {
        constexpr int edge = 4096;
        QImage mask(QSize(edge, edge), QImage::Format_Grayscale8);
        QVERIFY(!mask.isNull());
        quint32 random = 0x6d2b79f5U;
        for (int y = 0; y < edge; ++y)
        {
            uchar *line = mask.scanLine(y);
            for (int x = 0; x < edge; ++x)
            {
                random ^= random << 13U;
                random ^= random >> 17U;
                random ^= random << 5U;
                line[x] = (random & 1U) != 0U ? 255 : 0;
            }
        }
        mask.scanLine(0)[0] = 255;
        mask.scanLine(edge - 1)[edge - 1] = 255;

        const std::optional<PackedMaskRegion> snapshot = packBinaryMask(mask);
        QVERIFY(snapshot.has_value());
        QCOMPARE(snapshot->bounds, QRect(QPoint(), mask.size()));
        QVERIFY(snapshot->packedMask.size() <= qsizetype(2 * 1024 * 1024));
        QCOMPARE(unpackBinaryMask(*snapshot), mask);

        DocumentController controller;
        controller.newDocument(mask.size());
        const QUuid layerId = controller.document().activeLayerId;
        QImage restored;
        QObject::connect(&controller,
            &DocumentController::selectionHistoryStateRequested,
            &controller,
            [&restored](const QUuid &, const QImage &state)
            {
                restored = state;
            });
        controller.pushSelectionStateCommand(
            QStringLiteral("4K selection snapshot"), {}, {}, layerId, mask);
        QCOMPARE(restored, mask);
        controller.undoStack()->undo();
        QVERIFY(restored.isNull());
        controller.undoStack()->redo();
        QCOMPARE(restored, mask);

        mask.fill(0);
        QVERIFY(!packBinaryMask(mask).has_value());
    }

    void failedCanvasResizeMacroPreservesSelectionAndHistory()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.color = QColor(35, 95, 225);
        stroke.width = 12.0;
        stroke.points = {
            {QPointF(30.0, 50.0), 1.0}, {QPointF(70.0, 50.0), 1.0}};
        stroke.brush.antialiasing = false;
        document.layers.first().strokes.append(stroke);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.setAnimating(false);

        QImage selection(document.size, QImage::Format_Grayscale8);
        selection.fill(0);
        for (int y = 35; y <= 65; ++y)
        {
            std::fill(
                selection.scanLine(y) + 20, selection.scanLine(y) + 81, 255);
        }
        const QUuid layerId = controller.document().activeLayerId;
        controller.pushSelectionStateCommand(
            QStringLiteral("Select"), {}, {}, layerId, selection);
        QVERIFY(canvas.hasSelection());
        QVERIFY(canvas.hasTransformableSelection());

        const QByteArray beforeDocument =
            DocumentSerializer::toJson(controller.document());
        const int beforeCount = controller.undoStack()->count();
        const int beforeIndex = controller.undoStack()->index();

        controller.undoStack()->beginMacro(QStringLiteral("Resize canvas"));
        canvas.deselectSelection();
        // Selection changes are journaled while a macro is open; the UI is
        // updated only if the whole document transaction commits.
        QVERIFY(canvas.hasSelection());
        QVERIFY(!controller.resizeCanvas(controller.document().size,
            QPoint(static_cast<int>(
                       DocumentLimits::maximumStoredCoordinateMagnitude)
                       + 1,
                0)));
        controller.undoStack()->endMacro();

        QCOMPARE(
            DocumentSerializer::toJson(controller.document()), beforeDocument);
        QVERIFY(canvas.hasSelection());
        QVERIFY(canvas.hasTransformableSelection());
        QCOMPARE(controller.undoStack()->count(), beforeCount);
        QCOMPARE(controller.undoStack()->index(), beforeIndex);
    }

    void emptyLassoDoesNotCreateSelectionHistory()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const int undoCount = controller.undoStack()->count();
        const QPoint center = canvas.rect().center();
        canvas.setTool(CanvasWidget::Tool::Lasso);
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(60, 0));
        QTest::mouseMove(&canvas, center, 5);
        QTest::mouseMove(&canvas, center + QPoint(60, 0), 5);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(60, 0));

        QVERIFY(!canvas.hasSelection());
        QCOMPARE(controller.undoStack()->count(), undoCount);
    }

    void selectionMovePreviewMatchesCommittedPixelOperation()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.background = Qt::white;
        document.wobbleAmount = 0.0;

        Stroke destination;
        destination.color = QColor(220, 35, 35);
        destination.width = 20.0;
        destination.points = {
            {QPointF(60.0, 50.0), 1.0}, {QPointF(90.0, 50.0), 1.0}};
        destination.brush.antialiasing = false;

        Stroke source;
        source.color = QColor(25, 90, 220);
        source.width = 20.0;
        source.points = {
            {QPointF(10.0, 50.0), 1.0}, {QPointF(40.0, 50.0), 1.0}};
        source.brush.antialiasing = false;

        Stroke hole;
        hole.mode = StrokeMode::Erase;
        hole.width = 8.0;
        hole.points = {{QPointF(25.0, 50.0), 1.0}};
        hole.brush.antialiasing = false;
        document.layers.first().strokes = {destination, source, hole};

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(500, 500);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint)
        {
            const QPointF center(canvas.rect().center());
            return (
                center + (documentPoint - QPointF(50.0, 50.0)) * canvas.zoom())
                .toPoint();
        };
        const auto lasso = [&canvas, &widgetPoint](const QRectF &documentRect)
        {
            const QPoint topLeft = widgetPoint(documentRect.topLeft());
            const QPoint topRight = widgetPoint(documentRect.topRight());
            const QPoint bottomRight = widgetPoint(documentRect.bottomRight());
            const QPoint bottomLeft = widgetPoint(documentRect.bottomLeft());
            QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
            QTest::mouseMove(&canvas, topRight, 5);
            QTest::mouseMove(&canvas, bottomRight, 5);
            QTest::mouseMove(&canvas, bottomLeft, 5);
            QTest::mouseRelease(
                &canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        };
        const auto sample =
            [&canvas](const QPixmap &pixmap, const QPoint &widgetPosition)
        {
            const QImage image = pixmap.toImage();
            const qreal xScale =
                static_cast<qreal>(image.width()) / canvas.width();
            const qreal yScale =
                static_cast<qreal>(image.height()) / canvas.height();
            const int x = std::clamp(
                qRound(widgetPosition.x() * xScale), 0, image.width() - 1);
            const int y = std::clamp(
                qRound(widgetPosition.y() * yScale), 0, image.height() - 1);
            return image.pixelColor(x, y);
        };

        canvas.setTool(CanvasWidget::Tool::Lasso);
        lasso(QRectF(5.0, 38.0, 40.0, 24.0));
        QTRY_VERIFY(canvas.hasTransformableSelection());
        canvas.setSelectionMoveMode(true);
        const QByteArray beforeTransform =
            DocumentSerializer::toJson(controller.document());
        const int undoIndexBeforeTransform = controller.undoStack()->index();

        const QPoint dragStart = widgetPoint(QPointF(15.0, 50.0));
        const QPoint dragEnd = widgetPoint(QPointF(65.0, 50.0));
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, dragStart);
        QTest::mouseMove(&canvas, dragEnd, 5);
        QApplication::processEvents();
        const QPixmap preview = canvas.grab();

        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, dragEnd);
        QApplication::processEvents();
        QVERIFY(canvas.hasPendingSelectionTransform());
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()), beforeTransform);
        QCOMPARE(controller.undoStack()->index(), undoIndexBeforeTransform);
        QVERIFY(canvas.applySelectionTransform());
        QApplication::processEvents();
        const QPixmap committed = canvas.grab();
        QCOMPARE(controller.undoStack()->index(), undoIndexBeforeTransform + 1);

        const QPoint blueSample = widgetPoint(QPointF(65.0, 50.0));
        const QPoint holeSample = widgetPoint(QPointF(75.0, 50.0));
        QCOMPARE(sample(preview, blueSample), sample(committed, blueSample));
        QCOMPARE(sample(preview, holeSample), sample(committed, holeSample));
        QCOMPARE(sample(committed, holeSample), destination.color);

        controller.undoStack()->undo();
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()), beforeTransform);
    }

    void sequentialSmoothSelectionTransformLeavesNoSourceFringe()
    {
        Document document = Document::createDefault(QSize(160, 100));
        document.background = Qt::transparent;
        document.wobbleAmount = 0.0;
        Stroke source;
        source.color = QColor(35, 95, 225);
        source.width = 20.0;
        source.points = {
            {QPointF(20.0, 50.0), 1.0}, {QPointF(60.0, 50.0), 1.0}};
        source.brush.antialiasing = false;
        document.layers.first().strokes.append(source);

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(600, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const auto widgetPoint = [&canvas](const QPointF &documentPoint)
        {
            const QPointF center(canvas.rect().center());
            return (
                center + (documentPoint - QPointF(80.0, 50.0)) * canvas.zoom())
                .toPoint();
        };
        const QPoint topLeft = widgetPoint(QPointF(8.0, 35.0));
        const QPoint topRight = widgetPoint(QPointF(72.0, 35.0));
        const QPoint bottomRight = widgetPoint(QPointF(72.0, 65.0));
        const QPoint bottomLeft = widgetPoint(QPointF(8.0, 65.0));
        canvas.setTool(CanvasWidget::Tool::Lasso);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(&canvas, topRight, 5);
        QTest::mouseMove(&canvas, bottomRight, 5);
        QTest::mouseMove(&canvas, bottomLeft, 5);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTRY_VERIFY(canvas.hasTransformableSelection());

        const QByteArray beforeTransform =
            DocumentSerializer::toJson(controller.document());
        const int undoCountBeforeTransform = controller.undoStack()->count();
        const int undoIndexBeforeTransform = controller.undoStack()->index();
        QVERIFY(canvas.rotateSelection(27.0));
        QTRY_VERIFY(canvas.hasTransformableSelection());
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()), beforeTransform);
        QCOMPARE(controller.undoStack()->count(), undoCountBeforeTransform);

        canvas.setSelectionMoveMode(true);
        const QPoint dragStart = widgetPoint(QPointF(40.0, 50.0));
        const QPoint dragEnd = widgetPoint(QPointF(120.0, 50.0));
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, dragStart);
        QTest::mouseMove(&canvas, dragEnd, 5);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, dragEnd);

        QVERIFY(canvas.hasPendingSelectionTransform());
        QCOMPARE(
            DocumentSerializer::toJson(controller.document()), beforeTransform);
        QCOMPARE(controller.undoStack()->index(), undoIndexBeforeTransform);
        QVERIFY(canvas.applySelectionTransform());
        QCOMPARE(controller.undoStack()->count(), undoCountBeforeTransform + 1);
        QCOMPARE(controller.undoStack()->index(), undoIndexBeforeTransform + 1);
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
        QCOMPARE(controller.document().layers.first().strokes.last().mode,
            StrokeMode::PixelSelection);

        const QImage rendered = RenderEngine::render(controller.document(), 0);
        QVERIFY(!rendered.isNull());
        bool sourceFringeRemains = false;
        for (int y = 0; y < rendered.height() && !sourceFringeRemains; ++y)
        {
            const auto *line =
                reinterpret_cast<const QRgb *>(rendered.constScanLine(y));
            for (int x = 0; x < 80; ++x)
            {
                if (qAlpha(line[x]) != 0)
                {
                    sourceFringeRemains = true;
                    break;
                }
            }
        }
        QVERIFY(!sourceFringeRemains);
    }

    void clearsSelectionSafelyAcrossCanvasAndImageResizeUndo()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *lassoAction =
            window.findChild<QAction *>(QStringLiteral("lassoAction"));
        QAction *resizeCanvasAction =
            window.findChild<QAction *>(QStringLiteral("resizeCanvasAction"));
        QAction *resizeImageAction =
            window.findChild<QAction *>(QStringLiteral("resizeImageAction"));
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QVERIFY(canvas);
        QVERIFY(lassoAction);
        QVERIFY(resizeCanvasAction);
        QVERIFY(resizeImageAction);
        QVERIFY(undoAction);

        const auto expectedFitZoom = [canvas](const QSize &size)
        {
            return std::clamp(std::min((canvas->width() - 64.0) / size.width(),
                                  (canvas->height() - 64.0) / size.height()),
                0.01,
                16.0);
        };
        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(60, 0));
        QTest::mouseMove(canvas, center + QPoint(60, 0), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(60, 0));
        lassoAction->trigger();
        const QPoint topLeft = center - QPoint(90, 50);
        const QPoint topRight = center + QPoint(90, -50);
        const QPoint bottomRight = center + QPoint(90, 50);
        const QPoint bottomLeft = center + QPoint(-90, 50);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(canvas, topRight, 5);
        QTest::mouseMove(canvas, bottomRight, 5);
        QTest::mouseMove(canvas, bottomLeft, 5);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTRY_VERIFY(canvas->hasTransformableSelection());

        bool canvasDialogHandled = false;
        QSize originalCanvasSize;
        QSize resizedCanvasSize;
        canvas->setZoomPercent(200);
        QTimer::singleShot(0,
            &window,
            [&]()
            {
                CanvasSizeDialog *dialog =
                    window.findChild<CanvasSizeDialog *>();
                if (!dialog)
                {
                    return;
                }
                QSpinBox *width = dialog->findChild<QSpinBox *>(
                    QStringLiteral("canvasWidthSpin"));
                QSpinBox *height = dialog->findChild<QSpinBox *>(
                    QStringLiteral("canvasHeightSpin"));
                if (!width || !height)
                {
                    return;
                }
                originalCanvasSize = QSize(width->value(), height->value());
                width->setValue(width->value() + 24);
                resizedCanvasSize = QSize(width->value(), height->value());
                canvasDialogHandled = true;
                dialog->accept();
            });
        resizeCanvasAction->trigger();
        QVERIFY(canvasDialogHandled);
        QVERIFY(!canvas->hasSelection());
        QVERIFY(qAbs(canvas->zoom() - expectedFitZoom(resizedCanvasSize))
                < 0.000001);
        undoAction->trigger();
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QVERIFY(qAbs(canvas->zoom() - expectedFitZoom(originalCanvasSize))
                < 0.000001);

        bool imageDialogHandled = false;
        QSize resizedImageSize;
        canvas->setZoomPercent(175);
        QTimer::singleShot(0,
            &window,
            [&]()
            {
                ImageSizeDialog *dialog = window.findChild<ImageSizeDialog *>();
                if (!dialog)
                {
                    return;
                }
                QDoubleSpinBox *percentage =
                    dialog->findChild<QDoubleSpinBox *>(
                        QStringLiteral("imageScalePercentSpin"));
                QSpinBox *width = dialog->findChild<QSpinBox *>(
                    QStringLiteral("imageWidthSpin"));
                QSpinBox *height = dialog->findChild<QSpinBox *>(
                    QStringLiteral("imageHeightSpin"));
                if (!percentage || !width || !height)
                {
                    return;
                }
                percentage->setValue(110.0);
                resizedImageSize = QSize(width->value(), height->value());
                imageDialogHandled = true;
                dialog->accept();
            });
        resizeImageAction->trigger();
        QVERIFY(imageDialogHandled);
        QVERIFY(!canvas->hasSelection());
        QVERIFY(qAbs(canvas->zoom() - expectedFitZoom(resizedImageSize))
                < 0.000001);
        undoAction->trigger();
        QTRY_VERIFY(canvas->hasTransformableSelection());
        QVERIFY(qAbs(canvas->zoom() - expectedFitZoom(originalCanvasSize))
                < 0.000001);

        canvas->deselectSelection();
        QVERIFY(!canvas->hasSelection());
        lassoAction->trigger();
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(canvas, topRight, 5);
        QTest::mouseMove(canvas, bottomRight, 5);

        bool activeLassoDialogHandled = false;
        QSize lassoResizeSize;
        canvas->setZoomPercent(150);
        QTimer::singleShot(0,
            &window,
            [&]()
            {
                CanvasSizeDialog *dialog =
                    window.findChild<CanvasSizeDialog *>();
                if (!dialog)
                {
                    return;
                }
                QSpinBox *width = dialog->findChild<QSpinBox *>(
                    QStringLiteral("canvasWidthSpin"));
                QSpinBox *height = dialog->findChild<QSpinBox *>(
                    QStringLiteral("canvasHeightSpin"));
                if (!width || !height)
                {
                    return;
                }
                width->setValue(width->value() + 1);
                lassoResizeSize = QSize(width->value(), height->value());
                activeLassoDialogHandled = true;
                dialog->accept();
            });
        resizeCanvasAction->trigger();
        QVERIFY(activeLassoDialogHandled);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, bottomRight);
        QVERIFY(!canvas->hasSelection());
        QVERIFY(
            qAbs(canvas->zoom() - expectedFitZoom(lassoResizeSize)) < 0.000001);
    }

    void mirrorsTheCanvasAsAViewOnlyToggle()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *mirrorAction =
            window.findChild<QAction *>(QStringLiteral("mirrorCanvasAction"));
        QToolButton *mirrorButton = window.findChild<QToolButton *>(
            QStringLiteral("mirrorCanvasButton"));
        QVERIFY(canvas);
        QVERIFY(mirrorAction);
        QVERIFY(mirrorButton);
        QVERIFY(mirrorAction->isCheckable());
        QCOMPARE(mirrorAction->shortcut(), QKeySequence(QStringLiteral("M")));
        QVERIFY(!canvas->isCanvasMirrored());
        QVERIFY(!window.isWindowModified());

        mirrorAction->trigger();
        QVERIFY(canvas->isCanvasMirrored());
        QVERIFY(mirrorAction->isChecked());
        QVERIFY(canvas->zoom() > 0.0);
        QVERIFY(!window.isWindowModified());

        mirrorAction->trigger();
        QVERIFY(!canvas->isCanvasMirrored());
        QVERIFY(!mirrorAction->isChecked());
        QVERIFY(!window.isWindowModified());
    }

    void zoomsWithKeyboardActions()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QAction *zoomInAction =
            window.findChild<QAction *>(QStringLiteral("zoomInAction"));
        QAction *zoomOutAction =
            window.findChild<QAction *>(QStringLiteral("zoomOutAction"));
        QVERIFY(canvas);
        QVERIFY(zoomInAction);
        QVERIFY(zoomOutAction);

        const qreal initialZoom = canvas->zoom();
        zoomInAction->trigger();
        QVERIFY(canvas->zoom() > initialZoom);
        zoomOutAction->trigger();
        zoomOutAction->trigger();
        QVERIFY(canvas->zoom() < initialZoom);
        QVERIFY(!window.isWindowModified());
    }

    void distinguishesActualZoomFromFit()
    {
        DocumentController controller;
        controller.newDocument(QSize(1000, 800));
        CanvasWidget canvas(&controller);
        canvas.resize(500, 400);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        canvas.setZoomPercent(250);
        QVERIFY(qAbs(canvas.zoom() - 2.5) < 0.0001);
        canvas.resetZoom();
        QVERIFY(qAbs(canvas.zoom() - 1.0) < 0.0001);

        canvas.fitToWindow();
        const qreal fittedZoom = canvas.zoom();
        QVERIFY(qAbs(fittedZoom - 1.0) > 0.0001);
        QVERIFY(qAbs(fittedZoom - 2.5) > 0.0001);

        canvas.resetZoom();
        QVERIFY(qAbs(canvas.zoom() - 1.0) < 0.0001);
    }

    void syncsMainWindowZoomControls()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QSlider *zoomSlider =
            window.findChild<QSlider *>(QStringLiteral("zoomSlider"));
        QSpinBox *zoomSpin =
            window.findChild<QSpinBox *>(QStringLiteral("zoomPercentSpin"));
        QAction *actualSizeAction =
            window.findChild<QAction *>(QStringLiteral("actualSizeAction"));
        QVERIFY(canvas);
        QVERIFY(zoomSlider);
        QVERIFY(zoomSpin);
        QVERIFY(actualSizeAction);

        zoomSpin->setValue(250);
        QVERIFY(qAbs(canvas->zoom() - 2.5) < 0.0001);
        QCOMPARE(zoomSpin->value(), 250);
        const int sliderAt250 = zoomSlider->value();

        canvas->setZoomPercent(175);
        QCOMPARE(zoomSpin->value(), 175);
        QVERIFY(zoomSlider->value() != sliderAt250);

        int sliderTarget = zoomSlider->maximum() * 3 / 4;
        if (sliderTarget == zoomSlider->value())
        {
            sliderTarget = zoomSlider->maximum() / 2;
        }
        zoomSlider->setValue(sliderTarget);
        QCOMPARE(zoomSpin->value(), qRound(canvas->zoom() * 100.0));
        QVERIFY(qAbs(canvas->zoom() - 1.75) > 0.0001);

        actualSizeAction->trigger();
        QCOMPARE(zoomSpin->value(), 100);
        QVERIFY(qAbs(canvas->zoom() - 1.0) < 0.0001);
        QVERIFY(!window.isWindowModified());
    }

    void pausesAnimationWhilePanning()
    {
        Document document = Document::createDefault(QSize(128, 128));
        document.framesPerSecond = 50.0;
        document.animationFrames = 30;
        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        const QPoint center = canvas.rect().center();
        QTest::mousePress(&canvas, Qt::MiddleButton, Qt::NoModifier, center);
        const int heldFrame = canvas.currentFrame();
        QTest::qWait(100);
        QCOMPARE(canvas.currentFrame(), heldFrame);

        QSignalSpy frameChanges(&canvas, &CanvasWidget::currentFrameChanged);
        QTest::mouseRelease(&canvas, Qt::MiddleButton, Qt::NoModifier, center);
        QTRY_VERIFY_WITH_TIMEOUT(!frameChanges.isEmpty(), 1000);
    }

    void zoomsWithPanModifierCtrlDrag()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        const qreal initialZoom = canvas.zoom();
        canvas.setPanModifierActive(true);
        const QPoint start(200, 200);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::ControlModifier, start);
        QTest::mouseMove(&canvas, start + QPoint(120, 0), 5);
        QTest::mouseRelease(&canvas,
            Qt::LeftButton,
            Qt::ControlModifier,
            start + QPoint(120, 0));
        canvas.setPanModifierActive(false);
        QVERIFY(canvas.zoom() > initialZoom);

        const Layer &layer = controller.document().layers.first();
        QVERIFY(layer.strokes.isEmpty());
    }

    void picksBrushColorWithAltClick()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        canvas.setBrushColor(Qt::red);
        const QPoint center(200, 200);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::AltModifier, center);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::AltModifier, center);

        QCOMPARE(canvas.brushColor(), QColor(Qt::white));
        const Layer &layer = controller.document().layers.first();
        QVERIFY(layer.strokes.isEmpty());
    }

    void preservesAlphaWhenPickingBrushColor()
    {
        Document document = Document::createDefault(QSize(100, 100));
        const QColor translucent(255, 0, 0, 128);
        document.background = translucent;
        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);

        canvas.setBrushColor(Qt::black);
        const QPoint center = canvas.rect().center();
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::AltModifier, center);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::AltModifier, center);

        QCOMPARE(canvas.brushColor(), translucent);
    }

    void appliesBrushRoughnessToNewStrokes()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        canvas.setBrushRoughness(0.4);
        QCOMPARE(canvas.brushRoughness(), 0.4);

        const QPoint center(200, 200);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(&canvas, center + QPoint(40, 0), 5);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(40, 0));

        const Layer &layer = controller.document().layers.first();
        QCOMPARE(layer.strokes.size(), 1);
        QCOMPARE(layer.strokes.first().brush.wobbleScale, 0.4);
    }

    void keepsBrushAndEraserSizesIndependent()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        BrushSizeRow brushSizeRow(
            &canvas, BrushSizeRow::Target::Brush, QStringLiteral("testBrush"));
        BrushSizeRow eraserSizeRow(&canvas,
            BrushSizeRow::Target::Eraser,
            QStringLiteral("testEraser"));
        QSpinBox *brushSizeSpin =
            brushSizeRow.findChild<QSpinBox *>(QStringLiteral("testBrushSpin"));
        QSpinBox *eraserSizeSpin = eraserSizeRow.findChild<QSpinBox *>(
            QStringLiteral("testEraserSpin"));
        QVERIFY(brushSizeSpin);
        QVERIFY(eraserSizeSpin);

        brushSizeSpin->setValue(17);
        QCOMPARE(canvas.brushWidth(), 17.0);
        QCOMPARE(eraserSizeSpin->value(), qRound(canvas.eraserWidth()));

        eraserSizeSpin->setValue(41);
        QCOMPARE(canvas.eraserWidth(), 41.0);
        QCOMPARE(brushSizeSpin->value(), 17);

        canvas.setBrushWidth(23.0);
        QCOMPARE(brushSizeSpin->value(), 23);
        QCOMPARE(eraserSizeSpin->value(), 41);
        canvas.setEraserWidth(57.0);
        QCOMPARE(eraserSizeSpin->value(), 57);
        QCOMPARE(brushSizeSpin->value(), 23);

        const QString originalPreset = canvas.brushPresetId();
        canvas.setBrushPreset(QStringLiteral("soft-airbrush"));
        QCOMPARE(canvas.eraserWidth(), 57.0);
        canvas.setBrushPreset(originalPreset);
        QCOMPARE(canvas.brushWidth(), 23.0);
        QCOMPARE(canvas.eraserWidth(), 57.0);

        const QPoint center = canvas.rect().center();
        canvas.setTool(CanvasWidget::Tool::Brush);
        QTest::mouseClick(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(40, 0));
        canvas.setTool(CanvasWidget::Tool::Eraser);
        QTest::mouseClick(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(40, 0));

        canvas.setTool(CanvasWidget::Tool::Lasso);
        QPointingDevice eraserStylus(QStringLiteral("Test eraser stylus"),
            2,
            QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Eraser,
            QInputDevice::Capability::Position
                | QInputDevice::Capability::Pressure,
            1,
            1);
        const QPointF tabletPosition = QPointF(center) + QPointF(0.0, 40.0);
        const QPointF globalTabletPosition =
            canvas.mapToGlobal(tabletPosition.toPoint());
        QTabletEvent tabletHover(QEvent::TabletMove,
            &eraserStylus,
            tabletPosition,
            globalTabletPosition,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::NoButton,
            Qt::NoButton);
        QApplication::sendEvent(&canvas, &tabletHover);
        QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
        QTest::mouseMove(&canvas, center + QPoint(10, 40));
        QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor);
        QTabletEvent tabletPress(QEvent::TabletPress,
            &eraserStylus,
            tabletPosition,
            globalTabletPosition,
            0.8,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::LeftButton,
            Qt::LeftButton);
        QApplication::sendEvent(&canvas, &tabletPress);
        QTabletEvent tabletRelease(QEvent::TabletRelease,
            &eraserStylus,
            tabletPosition,
            globalTabletPosition,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::LeftButton,
            Qt::NoButton);
        QApplication::sendEvent(&canvas, &tabletRelease);

        const QVector<Stroke> &strokes =
            controller.document().layers.first().strokes;
        QCOMPARE(strokes.size(), 3);
        QVERIFY(strokes.at(0).mode == StrokeMode::Paint);
        QCOMPARE(strokes.at(0).width, 23.0);
        QVERIFY(strokes.at(1).mode == StrokeMode::Erase);
        QCOMPARE(strokes.at(1).width, 57.0);
        QVERIFY(strokes.at(2).mode == StrokeMode::Erase);
        QCOMPARE(strokes.at(2).width, 57.0);
    }

    void isolatesEraserBrushEngineFromActiveBrush()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        canvas.setBrushPreset(QStringLiteral("soft-airbrush"));
        canvas.setBrushRoughness(0.35);
        canvas.setBrushAntialiasing(true);
        canvas.setTool(CanvasWidget::Tool::Eraser);
        const QPoint center = canvas.rect().center();
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(30, 0));
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(30, 0));

        const Stroke &eraser =
            controller.document().layers.first().strokes.constLast();
        QCOMPARE(eraser.mode, StrokeMode::Erase);
        QCOMPARE(eraser.brush, EraserPresetCatalog::defaultPreset().settings);

        for (const QString &presetId :
            {QStringLiteral("soft-eraser"), QStringLiteral("kneaded-eraser")})
        {
            canvas.setEraserPreset(presetId);
            QTest::mouseClick(&canvas,
                Qt::LeftButton,
                Qt::NoModifier,
                center + QPoint(10, 20));
            const Stroke &presetStroke =
                controller.document().layers.first().strokes.constLast();
            QCOMPARE(presetStroke.mode, StrokeMode::Erase);
            QCOMPARE(presetStroke.brush,
                EraserPresetCatalog::find(presetId)->settings);
        }
    }

    void keepsEraserPresetSettingsIndependent()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);

        canvas.setEraserWidth(11.0);
        canvas.setEraserStabilization(0.15);
        canvas.setEraserPreset(QStringLiteral("soft-eraser"));
        canvas.setEraserWidth(62.0);
        canvas.setEraserStabilization(0.45);
        canvas.setEraserPreset(QStringLiteral("kneaded-eraser"));
        canvas.setEraserWidth(39.0);
        canvas.setEraserStabilization(0.75);

        QCOMPARE(canvas.eraserWidth(), 39.0);
        QCOMPARE(canvas.eraserStabilization(), 0.75);
        canvas.setEraserPreset(QStringLiteral("hard-eraser"));
        QCOMPARE(canvas.eraserWidth(), 11.0);
        QCOMPARE(canvas.eraserStabilization(), 0.15);
        canvas.setEraserPreset(QStringLiteral("soft-eraser"));
        QCOMPARE(canvas.eraserWidth(), 62.0);
        QCOMPARE(canvas.eraserStabilization(), 0.45);
    }

    void persistsDrawingToolSettings()
    {
        const QColor rememberedColor(18, 52, 86, 120);
        {
            MainWindow window;
            CanvasWidget *canvas = window.findChild<CanvasWidget *>();
            QVERIFY(canvas);

            canvas->setBrushPreset(QStringLiteral("monoline"));
            canvas->setBrushWidth(23.0);
            canvas->setBrushStabilization(0.23);
            canvas->setBrushPreset(QStringLiteral("soft-airbrush"));
            canvas->setBrushWidth(47.0);
            canvas->setBrushStabilization(0.64);
            canvas->setEraserPreset(QStringLiteral("hard-eraser"));
            canvas->setEraserWidth(57.0);
            canvas->setEraserStabilization(0.51);
            canvas->setEraserPreset(QStringLiteral("soft-eraser"));
            canvas->setEraserWidth(73.0);
            canvas->setEraserStabilization(0.26);
            canvas->setEraserPreset(QStringLiteral("kneaded-eraser"));
            canvas->setEraserWidth(49.0);
            canvas->setEraserStabilization(0.12);
            canvas->setBrushRoughness(0.37);
            canvas->setBrushAntialiasing(true);
            canvas->setBrushColor(rememberedColor);
            canvas->setWandReference(
                CanvasWidget::WandReference::AllVisibleLayers);
            canvas->setSelectionShape(CanvasWidget::SelectionShape::Ellipse);
            canvas->setTool(CanvasWidget::Tool::Eraser);

            QVERIFY(window.close());
            QCOMPARE(QSettings()
                         .value(QStringLiteral("drawingTools/brush/presetId"))
                         .toString(),
                QStringLiteral("soft-airbrush"));
        }

        QSettings persistedSettings;
        persistedSettings.sync();
        QCOMPARE(persistedSettings.status(), QSettings::NoError);
        MainWindow restoredWindow;
        CanvasWidget *restored = restoredWindow.findChild<CanvasWidget *>();
        QVERIFY(restored);
        QCOMPARE(restored->brushPresetId(), QStringLiteral("soft-airbrush"));
        QCOMPARE(restored->eraserPresetId(), QStringLiteral("kneaded-eraser"));
        QCOMPARE(restored->brushWidth(), 47.0);
        QCOMPARE(restored->brushStabilization(), 0.64);
        QCOMPARE(restored->eraserWidth(), 49.0);
        QCOMPARE(restored->eraserStabilization(), 0.12);
        QCOMPARE(restored->brushRoughness(), 0.37);
        QVERIFY(restored->brushAntialiasing());
        QCOMPARE(restored->brushColor(), rememberedColor);
        QCOMPARE(restored->tool(), CanvasWidget::Tool::Eraser);
        QCOMPARE(restored->wandReference(),
            CanvasWidget::WandReference::AllVisibleLayers);
        QCOMPARE(
            restored->selectionShape(), CanvasWidget::SelectionShape::Ellipse);

        QAction *eraserAction =
            restoredWindow.findChild<QAction *>(QStringLiteral("eraserAction"));
        QSpinBox *brushSizeSpin = restoredWindow.findChild<QSpinBox *>(
            QStringLiteral("brushSizeSpin"));
        QSpinBox *eraserSizeSpin = restoredWindow.findChild<QSpinBox *>(
            QStringLiteral("eraserSizeSpin"));
        QSpinBox *brushStabilizationSpin = restoredWindow.findChild<QSpinBox *>(
            QStringLiteral("brushStabilizationSpin"));
        QSpinBox *eraserStabilizationSpin =
            restoredWindow.findChild<QSpinBox *>(
                QStringLiteral("eraserStabilizationSpin"));
        WandReferenceButton *wandReferenceButton =
            restoredWindow.findChild<WandReferenceButton *>(
                QStringLiteral("wandReferenceVisibleButton"));
        SelectionShapeButton *selectionShapeButton =
            restoredWindow.findChild<SelectionShapeButton *>(
                QStringLiteral("selectionShapeEllipseButton"));
        QSpinBox *roughnessSpin = restoredWindow.findChild<QSpinBox *>(
            QStringLiteral("brushRoughnessSpin"));
        QCheckBox *antialiasingToggle = restoredWindow.findChild<QCheckBox *>(
            QStringLiteral("brushAntialiasingToggle"));
        QVERIFY(eraserAction);
        QVERIFY(brushSizeSpin);
        QVERIFY(eraserSizeSpin);
        QVERIFY(brushStabilizationSpin);
        QVERIFY(eraserStabilizationSpin);
        QVERIFY(wandReferenceButton);
        QVERIFY(selectionShapeButton);
        QVERIFY(roughnessSpin);
        QVERIFY(antialiasingToggle);
        QVERIFY(eraserAction->isChecked());
        QCOMPARE(brushSizeSpin->value(), 47);
        QCOMPARE(eraserSizeSpin->value(), 49);
        QCOMPARE(brushStabilizationSpin->value(), 64);
        QCOMPARE(eraserStabilizationSpin->value(), 12);
        QCOMPARE(wandReferenceButton->reference(),
            CanvasWidget::WandReference::AllVisibleLayers);
        QVERIFY(wandReferenceButton->isChecked());
        QCOMPARE(selectionShapeButton->shape(),
            CanvasWidget::SelectionShape::Ellipse);
        QVERIFY(selectionShapeButton->isChecked());
        QCOMPARE(roughnessSpin->value(), 37);
        QVERIFY(antialiasingToggle->isChecked());

        restored->setBrushPreset(QStringLiteral("monoline"));
        QCOMPARE(restored->brushWidth(), 23.0);
        QCOMPARE(restored->brushStabilization(), 0.23);
        QCOMPARE(brushSizeSpin->value(), 23);
        QCOMPARE(brushStabilizationSpin->value(), 23);
        restored->setBrushPreset(QStringLiteral("soft-airbrush"));
        QCOMPARE(restored->brushWidth(), 47.0);
        QCOMPARE(restored->brushStabilization(), 0.64);
        QCOMPARE(brushSizeSpin->value(), 47);
        QCOMPARE(brushStabilizationSpin->value(), 64);

        restored->setEraserPreset(QStringLiteral("hard-eraser"));
        QCOMPARE(restored->eraserWidth(), 57.0);
        QCOMPARE(restored->eraserStabilization(), 0.51);
        QCOMPARE(eraserSizeSpin->value(), 57);
        QCOMPARE(eraserStabilizationSpin->value(), 51);
        restored->setEraserPreset(QStringLiteral("soft-eraser"));
        QCOMPARE(restored->eraserWidth(), 73.0);
        QCOMPARE(restored->eraserStabilization(), 0.26);
        QCOMPARE(eraserSizeSpin->value(), 73);
        QCOMPARE(eraserStabilizationSpin->value(), 26);
        restored->setEraserPreset(QStringLiteral("kneaded-eraser"));

        BrushPresetButton *selectedPresetButton = nullptr;
        for (BrushPresetButton *button :
            restoredWindow.findChildren<BrushPresetButton *>())
        {
            if (button->presetId() == QStringLiteral("soft-airbrush"))
            {
                selectedPresetButton = button;
                break;
            }
        }
        QVERIFY(selectedPresetButton);
        QVERIFY(selectedPresetButton->isChecked());

        EraserPresetButton *selectedEraserPresetButton = nullptr;
        for (EraserPresetButton *button :
            restoredWindow.findChildren<EraserPresetButton *>())
        {
            if (button->presetId() == QStringLiteral("kneaded-eraser"))
            {
                selectedEraserPresetButton = button;
                break;
            }
        }
        QVERIFY(selectedEraserPresetButton);
        QVERIFY(selectedEraserPresetButton->isChecked());
    }

    void migratesActiveColorFromRecentColors()
    {
        const QColor recentColor(42, 91, 137, 173);
        QSettings settings;
        settings.setValue(QStringLiteral("brush/recentColors"),
            QStringList{QStringLiteral("not-a-color"),
                recentColor.name(QColor::HexArgb)});
        settings.sync();

        MainWindow window;
        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QVERIFY(canvas);
        QCOMPARE(canvas->brushColor(), recentColor);
        QVERIFY(window.close());
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
        QCOMPARE(settings.value(QStringLiteral("drawingTools/brush/color"))
                     .toString(),
            recentColor.name(QColor::HexArgb));
    }

    void sanitizesInvalidDrawingToolSettings()
    {
        QSettings settings;
        settings.setValue(QStringLiteral("drawingTools/activeTool"),
            QStringLiteral("transform"));
        settings.setValue(QStringLiteral("drawingTools/brush/presetId"),
            QStringLiteral("missing-brush"));
        settings.setValue(QStringLiteral("drawingTools/eraser/presetId"),
            QStringLiteral("missing-eraser"));
        settings.setValue(QStringLiteral("drawingTools/brush/color"),
            QStringLiteral("not-a-color"));
        settings.setValue(QStringLiteral("drawingTools/brush/roughness"),
            std::numeric_limits<double>::infinity());
        settings.setValue(QStringLiteral("drawingTools/brush/antialiasing"),
            QStringLiteral("sometimes"));
        settings.setValue(
            QStringLiteral("drawingTools/brush/presetWidths/ink-pen"), 9999.0);
        settings.setValue(
            QStringLiteral("drawingTools/brush/presetWidths/g-pen"),
            std::numeric_limits<double>::quiet_NaN());
        settings.setValue(QStringLiteral("drawingTools/eraser/width"), -100.0);
        settings.setValue(
            QStringLiteral("drawingTools/brush/presetStabilizations/ink-pen"),
            9999.0);
        settings.setValue(QStringLiteral("drawingTools/eraser/stabilization"),
            std::numeric_limits<double>::quiet_NaN());
        settings.setValue(QStringLiteral("drawingTools/wand/reference"),
            QStringLiteral("selection-set"));
        settings.setValue(QStringLiteral("drawingTools/selection/shape"),
            QStringLiteral("polygon"));
        settings.sync();

        MainWindow window;
        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QVERIFY(canvas);
        QCOMPARE(canvas->tool(), CanvasWidget::Tool::Brush);
        QCOMPARE(
            canvas->brushPresetId(), BrushPresetCatalog::defaultPreset().id);
        QCOMPARE(
            canvas->eraserPresetId(), EraserPresetCatalog::defaultPreset().id);
        QCOMPARE(canvas->brushWidth(), DocumentLimits::maximumStrokeWidth);
        QCOMPARE(canvas->eraserWidth(), 1.0);
        QCOMPARE(canvas->brushStabilization(), 1.0);
        QCOMPARE(canvas->eraserStabilization(), 0.0);
        QCOMPARE(canvas->brushRoughness(), 1.0);
        QVERIFY(!canvas->brushAntialiasing());
        QCOMPARE(canvas->brushColor(), QColor(Qt::black));
        QCOMPARE(
            canvas->wandReference(), CanvasWidget::WandReference::ActiveLayer);
        QCOMPARE(
            canvas->selectionShape(), CanvasWidget::SelectionShape::Freehand);

        canvas->setBrushPreset(QStringLiteral("g-pen"));
        QCOMPARE(canvas->brushWidth(),
            BrushPresetCatalog::find(QStringLiteral("g-pen"))->defaultSize);

        const qreal brushWidth = canvas->brushWidth();
        const qreal eraserWidth = canvas->eraserWidth();
        const qreal roughness = canvas->brushRoughness();
        const qreal brushStabilization = canvas->brushStabilization();
        const qreal eraserStabilization = canvas->eraserStabilization();
        canvas->setBrushWidth(std::numeric_limits<qreal>::quiet_NaN());
        canvas->setEraserWidth(std::numeric_limits<qreal>::infinity());
        canvas->setBrushRoughness(std::numeric_limits<qreal>::quiet_NaN());
        canvas->setBrushStabilization(std::numeric_limits<qreal>::infinity());
        canvas->setEraserStabilization(std::numeric_limits<qreal>::quiet_NaN());
        QCOMPARE(canvas->brushWidth(), brushWidth);
        QCOMPARE(canvas->eraserWidth(), eraserWidth);
        QCOMPARE(canvas->brushRoughness(), roughness);
        QCOMPARE(canvas->brushStabilization(), brushStabilization);
        QCOMPARE(canvas->eraserStabilization(), eraserStabilization);
    }

    void deletesTheLastLayerAndAddsOneBack()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QListWidget *layerList = window.findChild<QListWidget *>();
        QToolButton *addButton =
            window.findChild<QToolButton *>(QStringLiteral("layerAddButton"));
        QToolButton *deleteButton = window.findChild<QToolButton *>(
            QStringLiteral("layerDeleteButton"));
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QAction *redoAction =
            window.findChild<QAction *>(QStringLiteral("redoAction"));
        QAction *clearLayerAction =
            window.findChild<QAction *>(QStringLiteral("clearLayerAction"));
        QVERIFY(canvas);
        QVERIFY(layerList);
        QVERIFY(addButton);
        QVERIFY(deleteButton);
        QVERIFY(undoAction);
        QVERIFY(redoAction);
        QVERIFY(clearLayerAction);
        QCOMPARE(layerList->count(), 1);
        QVERIFY(deleteButton->isEnabled());
        QVERIFY(clearLayerAction->isEnabled());

        QTest::mouseClick(deleteButton, Qt::LeftButton);
        QTRY_COMPARE(layerList->count(), 0);
        QVERIFY(!deleteButton->isEnabled());
        QVERIFY(addButton->isEnabled());
        QVERIFY(!clearLayerAction->isEnabled());
        QVERIFY(window.isWindowModified());

        QSignalSpy interactionMessages(
            canvas, &CanvasWidget::interactionMessage);
        QTest::mouseClick(
            canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
        QCOMPARE(interactionMessages.size(), 1);
        QCOMPARE(interactionMessages.first().first().toString(),
            QStringLiteral("Add a layer before using this tool."));

        undoAction->trigger();
        QTRY_COMPARE(layerList->count(), 1);
        QVERIFY(deleteButton->isEnabled());
        QVERIFY(clearLayerAction->isEnabled());

        redoAction->trigger();
        QTRY_COMPARE(layerList->count(), 0);
        QVERIFY(addButton->isEnabled());
        QVERIFY(!clearLayerAction->isEnabled());

        QTest::mouseClick(addButton, Qt::LeftButton);
        QTRY_COMPARE(layerList->count(), 1);
        QVERIFY(deleteButton->isEnabled());
        QVERIFY(clearLayerAction->isEnabled());

        undoAction->trigger();
        QTRY_COMPARE(layerList->count(), 0);
        QVERIFY(addButton->isEnabled());
        QVERIFY(!clearLayerAction->isEnabled());
    }

    void mapsDrawingInputThroughTheMirroredCanvas()
    {
        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.setCanvasMirrored(true);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const QPoint leftOfCenter(100, 200);
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, leftOfCenter);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, leftOfCenter);

        const Layer &layer = controller.document().layers.first();
        QCOMPARE(layer.strokes.size(), 1);
        QVERIFY(layer.strokes.first().points.first().position.x() > 70.0);
    }

    void clipsDrawingToolsToPersistentLasso()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        canvas.fitToWindow();

        const QPoint center = canvas.rect().center();
        canvas.setTool(CanvasWidget::Tool::Lasso);
        const QPoint topLeft = center - QPoint(50, 50);
        const QPoint topRight = center + QPoint(50, -50);
        const QPoint bottomRight = center + QPoint(50, 50);
        const QPoint bottomLeft = center + QPoint(-50, 50);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(&canvas, topRight, 5);
        QTest::mouseMove(&canvas, bottomRight, 5);
        QTest::mouseMove(&canvas, bottomLeft, 5);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QVERIFY(canvas.hasSelection());

        canvas.setTool(CanvasWidget::Tool::Brush);
        QVERIFY(canvas.hasSelection());
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(120, 0));
        QTest::mouseMove(&canvas, center + QPoint(120, 0), 5);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(120, 0));
        QCOMPARE(controller.document().layers.first().strokes.size(), 1);
        QVERIFY(!controller.document()
                .layers.first()
                .strokes.first()
                .clipMask.isNull());
        QImage rendered = RenderEngine::render(controller.document(), 0);
        QCOMPARE(rendered.pixelColor(20, 50), QColor(Qt::white));
        QCOMPARE(rendered.pixelColor(50, 50), QColor(Qt::black));

        canvas.setBrushColor(QColor(220, 30, 40));
        canvas.setTool(CanvasWidget::Tool::Bucket);
        QVERIFY(canvas.hasSelection());
        QTest::mouseClick(
            &canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(0, 25));
        QCOMPARE(controller.document().layers.first().strokes.size(), 2);
        QVERIFY(!controller.document()
                .layers.first()
                .strokes.last()
                .clipMask.isNull());
        rendered = RenderEngine::render(controller.document(), 0);
        QCOMPARE(rendered.pixelColor(50, 42), canvas.brushColor());
        QCOMPARE(rendered.pixelColor(20, 42), QColor(Qt::white));
    }

    void autosavesModifiedWork()
    {
        const QString recoveryKey = QStringLiteral("recovery/sourcePath");
        SettingValueGuard recoveryGuard(recoveryKey);
        EnvironmentVariableGuard environmentGuard(
            QByteArrayLiteral("WAGLEWAGLEPAINT_RECOVERY_PATH"));
        QTemporaryDir recoveryDirectory;
        QVERIFY(recoveryDirectory.isValid());
        const QString recoveryPath =
            recoveryDirectory.filePath(QStringLiteral("recovery.wagle"));
        qputenv("WAGLEWAGLEPAINT_RECOVERY_PATH", recoveryPath.toUtf8());

        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QVERIFY(canvas);

        const QPoint center = canvas->rect().center();
        QTest::mousePress(
            canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(40, 0));
        QTest::mouseMove(canvas, center + QPoint(40, 0), 5);
        QTest::mouseRelease(
            canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(40, 0));
        QTRY_VERIFY(window.isWindowModified());

        QEvent deactivate(QEvent::ApplicationDeactivate);
        QApplication::sendEvent(qApp, &deactivate);
        QVERIFY(QFileInfo::exists(recoveryPath));

        QString error;
        const std::optional<Document> recovered =
            DocumentSerializer::load(recoveryPath, &error);
        QVERIFY2(recovered.has_value(), qPrintable(error));
        QCOMPARE(recovered->layers.first().strokes.size(), 1);
        QFile::remove(recoveryPath);
    }

    void deactivationCancelsMouseAndTabletInput()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *windowCanvas = window.findChild<CanvasWidget *>();
        QAction *undoAction =
            window.findChild<QAction *>(QStringLiteral("undoAction"));
        QVERIFY(windowCanvas);
        QVERIFY(undoAction);

        const QPoint windowCenter = windowCanvas->rect().center();
        QTest::mousePress(windowCanvas,
            Qt::LeftButton,
            Qt::NoModifier,
            windowCenter - QPoint(30, 0));
        QTest::mouseMove(windowCanvas, windowCenter + QPoint(30, 0), 5);
        QEvent deactivate(QEvent::ApplicationDeactivate);
        QApplication::sendEvent(qApp, &deactivate);
        QTest::mouseRelease(windowCanvas,
            Qt::LeftButton,
            Qt::NoModifier,
            windowCenter + QPoint(30, 0));
        QVERIFY(!undoAction->isEnabled());
        QVERIFY(!window.isWindowModified());

        DocumentController controller;
        controller.newDocument(QSize(100, 100));
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        QPointingDevice stylus(QStringLiteral("Test stylus"),
            1,
            QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position
                | QInputDevice::Capability::Pressure,
            1,
            1);
        const QPointF center = canvas.rect().center();
        const QPointF globalCenter = canvas.mapToGlobal(center.toPoint());
        QTabletEvent tabletPress(QEvent::TabletPress,
            &stylus,
            center,
            globalCenter,
            0.7,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::LeftButton,
            Qt::LeftButton);
        QApplication::sendEvent(&canvas, &tabletPress);

        QFocusEvent focusOut(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
        QApplication::sendEvent(&canvas, &focusOut);

        QTabletEvent tabletRelease(QEvent::TabletRelease,
            &stylus,
            center,
            globalCenter,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            Qt::NoModifier,
            Qt::LeftButton,
            Qt::NoButton);
        QApplication::sendEvent(&canvas, &tabletRelease);
        QVERIFY(controller.document().layers.first().strokes.isEmpty());

        QTest::mouseClick(
            &canvas, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        QCOMPARE(controller.document().layers.first().strokes.size(), 1);

        canvas.setPanModifierActive(true);
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        QCOMPARE(canvas.cursor().shape(), Qt::ClosedHandCursor);
        QEvent ungrabMouse(QEvent::UngrabMouse);
        QApplication::sendEvent(&canvas, &ungrabMouse);
        QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        QCOMPARE(controller.document().layers.first().strokes.size(), 1);
    }

    void deactivationRestoresLassoAndCancelsSelectionMove()
    {
        Document document = Document::createDefault(QSize(100, 100));
        document.wobbleAmount = 0.0;
        Stroke stroke;
        stroke.width = 8.0;
        stroke.points = {
            {QPointF(20.0, 50.0), 1.0}, {QPointF(80.0, 50.0), 1.0}};
        document.layers.first().strokes = {stroke};

        DocumentController controller;
        controller.loadDocument(document);
        CanvasWidget canvas(&controller);
        canvas.resize(400, 400);
        canvas.setAnimating(false);
        canvas.setTool(CanvasWidget::Tool::Lasso);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        const QPoint center = canvas.rect().center();
        const QPoint topLeft = center - QPoint(120, 80);
        const QPoint topRight = center + QPoint(120, -80);
        const QPoint bottomRight = center + QPoint(120, 80);
        const QPoint bottomLeft = center + QPoint(-120, 80);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QTest::mouseMove(&canvas, topRight, 5);
        QTest::mouseMove(&canvas, bottomRight, 5);
        QTest::mouseMove(&canvas, bottomLeft, 5);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, topLeft);
        QVERIFY(canvas.hasSelection());
        QVERIFY(canvas.hasTransformableSelection());

        const QByteArray documentBeforeMove =
            DocumentSerializer::toJson(controller.document());
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(&canvas, center + QPoint(50, 20), 5);
        QFocusEvent moveFocusOut(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
        QApplication::sendEvent(&canvas, &moveFocusOut);
        QTest::mouseRelease(
            &canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(50, 20));
        QCOMPARE(DocumentSerializer::toJson(controller.document()),
            documentBeforeMove);
        QVERIFY(canvas.hasSelection());
        QVERIFY(canvas.hasTransformableSelection());

        const QPoint outsideSelection = center + QPoint(140, 140);
        QTest::mousePress(
            &canvas, Qt::LeftButton, Qt::NoModifier, outsideSelection);
        QTest::mouseMove(&canvas, outsideSelection - QPoint(30, 0), 5);
        QFocusEvent lassoFocusOut(
            QEvent::FocusOut, Qt::ActiveWindowFocusReason);
        QApplication::sendEvent(&canvas, &lassoFocusOut);
        QTest::mouseRelease(&canvas,
            Qt::LeftButton,
            Qt::NoModifier,
            outsideSelection - QPoint(30, 0));
        QCOMPARE(DocumentSerializer::toJson(controller.document()),
            documentBeforeMove);
        QVERIFY(canvas.hasSelection());
        QVERIFY(canvas.hasTransformableSelection());
    }

    void spacePanIsLimitedToCanvas()
    {
        MainWindow window;
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        CanvasWidget *canvas = window.findChild<CanvasWidget *>();
        QListWidget *layerList = window.findChild<QListWidget *>();
        QToolButton *addButton =
            window.findChild<QToolButton *>(QStringLiteral("layerAddButton"));
        QVERIFY(canvas);
        QVERIFY(layerList);
        QVERIFY(addButton);

        canvas->setFocus(Qt::OtherFocusReason);
        QTest::keyPress(canvas, Qt::Key_Space);
        QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
        QTest::keyRelease(canvas, Qt::Key_Space);
        QCOMPARE(canvas->cursor().shape(), Qt::BlankCursor);

        const int layerCount = layerList->count();
        addButton->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(addButton, Qt::Key_Space);
        QTRY_COMPARE(layerList->count(), layerCount + 1);
        QCOMPARE(canvas->cursor().shape(), Qt::BlankCursor);

        layerList->setCurrentRow(0);
        layerList->clearSelection();
        QVERIFY(layerList->selectedItems().isEmpty());
        layerList->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(layerList, Qt::Key_Space);
        QCOMPARE(layerList->selectedItems().size(), 1);
        QCOMPARE(canvas->cursor().shape(), Qt::BlankCursor);

        QLineEdit editor(&window);
        editor.show();
        editor.setFocus(Qt::OtherFocusReason);
        QTest::keyClicks(&editor, QStringLiteral("Layer name"));
        QCOMPARE(editor.text(), QStringLiteral("Layer name"));
        QCOMPARE(canvas->cursor().shape(), Qt::BlankCursor);
    }
};

int runUiTests(int argc, char **argv)
{
    UiTests tests;
    return QTest::qExec(&tests, argc, argv);
}

}

#include "UiTests.moc"
